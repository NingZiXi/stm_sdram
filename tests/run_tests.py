"""Compile actual driver for Cortex-M7 and execute against mocked HAL in Unicorn.

Requires arm-none-eabi-gcc, Python packages unicorn and pyelftools.
Run from any directory: python Lib/stm_sdram/tests/run_tests.py
No physical device is accessed; timing/electrical behavior is not simulated.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import argparse

COMPONENT = Path(__file__).resolve().parents[1]


def run(elf, entry, fault=None, interrupt=False):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_IPSR
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.mem_map(0x10000, 0x100000)
    uc.mem_map(0x20000000, 0x200000)
    uc.mem_map(0xD0000000, 0x2000000)
    for segment in elf.iter_segments():
        if segment["p_type"] == "PT_LOAD":
            uc.mem_write(segment["p_vaddr"], segment.data())
    symbol = elf.get_section_by_name(".symtab").get_symbol_by_name(entry)[0]["st_value"]
    uc.reg_write(UC_ARM_REG_SP, 0x201FFFF0)
    uc.reg_write(UC_ARM_REG_LR, 0x10001)
    if interrupt:
        uc.reg_write(UC_ARM_REG_IPSR, 15)  # SysTick handler context.

    reads = 0
    def corrupt(machine, access, address, size, value, user):
        nonlocal reads
        reads += 1
        if fault == "stuck_d0":
            data = int.from_bytes(machine.mem_read(address, size), "little") & ~1
            machine.mem_write(address, data.to_bytes(size, "little"))
        elif fault == "address_alias" and reads > 32 and address < 0xD0000800:
            # Simulate lower half reading the upper half (missing address line).
            machine.mem_write(address, bytes(machine.mem_read(address + 0x800, size)))

    if fault:
        uc.hook_add(UC_HOOK_MEM_READ, corrupt, begin=0xD0000000, end=0xD0000FFF)
    uc.emu_start(symbol | 1, 0x10000, timeout=30000000, count=100000000)
    from unicorn.arm_const import UC_ARM_REG_PC
    if uc.reg_read(UC_ARM_REG_PC) != 0x10000:
        raise AssertionError("emulation did not finish within its instruction/time limit")
    result = uc.reg_read(UC_ARM_REG_R0)
    if result:
        raise AssertionError(f"{entry}/{fault}: test_sdram.c line {result}")


def main():
    parser = argparse.ArgumentParser(description="Build and run SDRAM software tests without hardware")
    parser.add_argument("--project-root", type=Path, help="CubeMX H7 project providing HAL/CMSIS headers")
    parser.add_argument("--include", action="append", type=Path, help="Explicit HAL/config/CMSIS include directory; repeatable")
    parser.add_argument("--mcu", default="STM32H723xx", help="STM32H7 Cortex-M7 device macro")
    parser.add_argument("--compiler", default="arm-none-eabi-gcc", help="ARM GCC executable name or path")
    parser.add_argument("--build-dir", type=Path, help="Output directory")
    parser.add_argument("--python-deps", type=Path, help="Optional directory containing installed Python dependencies")
    args = parser.parse_args()
    if args.python_deps:
        sys.path.insert(0, str(args.python_deps.resolve()))
    try:
        from elftools.elf.elffile import ELFFile
        import unicorn
    except ImportError as exc:
        raise SystemExit(f"Missing dependency: {exc}. Install tests/requirements.txt in a virtual environment.")
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise SystemExit(f"ARM GCC not found: {args.compiler}")
    project = args.project_root.resolve() if args.project_root else None
    if not project and not args.include:
        project = next((p for p in COMPONENT.parents if (p / "Core/Inc").is_dir()
                        and (p / "Drivers/STM32H7xx_HAL_Driver/Inc").is_dir()), None)
    includes = [COMPONENT]
    if project:
        includes += [project / path for path in ("Core/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc",
                     "Drivers/CMSIS/Device/ST/STM32H7xx/Include", "Drivers/CMSIS/Include")]
    elif not args.include:
        parser.error("Provide --project-root or --include paths for STM32H7 HAL/config/CMSIS headers")
    includes += [p.resolve() for p in (args.include or [])]
    for path in includes:
        if not path.is_dir():
            parser.error(f"Include directory not found: {path}")
    output = (args.build_dir or ((project / "build/sdram-tests") if project
                               else COMPONENT / "build/tests")).resolve()
    output.mkdir(parents=True, exist_ok=True)
    print(subprocess.check_output([compiler, "--version"], text=True).splitlines()[0])
    print(f"Unicorn {unicorn.__version__}; source: {COMPONENT}; output: {output}")
    for optimize in ["-O0", "-O2", "-Os"]:
        binary = output / f"test{optimize}.elf"
        command = [compiler, "-mcpu=cortex-m7", "-mthumb", "-mfloat-abi=soft",
                   "-std=c11", optimize, "-g", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                   "-DUSE_HAL_DRIVER", f"-D{args.mcu}", "-nostdlib",
                   "-Wl,-Ttext=0x11000,-Tdata=0x20000000,-e,test_entry"]
        command += [f"-I{path}" for path in includes]
        command += [str(COMPONENT / "stm_sdram.c"), str(Path(__file__).with_name("test_sdram.c")),
                    "-lgcc", "-o", str(binary)]
        subprocess.run(command, check=True)
        with binary.open("rb") as stream:
            elf = ELFFile(stream)
            run(elf, "test_entry")
            run(elf, "test_lifecycle_entry")
            run(elf, "test_interrupt_entry", interrupt=True)
            run(elf, "test_fault_entry", "stuck_d0")
            run(elf, "test_address_fault_entry", "address_alias")
            run(elf, "test_progress_fault_entry", "address_alias")
        print(f"PASS {optimize}: init/retry, context guards, HAL failures, bounds, data, disable/recovery, progress/cancel, self-test, injected faults")


if __name__ == "__main__":
    main()
