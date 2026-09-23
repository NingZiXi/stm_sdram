/**
 * @file    main.c
 * @brief   sdram 组件板级初始化和最小使用示例
 */
#include "stm_sdram.h"
#include "sdram_fmc.h"
#include "main.h"
#include "gpio.h"
#include "fmc.h"

volatile stm_err_t example_result = STM_OK;
static void board_init(void);
static sdram_fmc_context_t controller;

// 初始化设备、访问数据并释放组件对象
int main(void)
{
    board_init();
    sdram_handle_t device = NULL;
    const sdram_config_t config = {
        .controller =
            sdram_fmc_bind(&controller, &hsdram1,
                           __HAL_RCC_GET_FMC_SOURCE() == RCC_FMCCLKSOURCE_HCLK ? HAL_RCC_GetHCLKFreq() : 0U),
        .device = &sdram_device_w9825g6kh_6,
    };
    example_result = sdram_create(&config, &device);
    const uint16_t tx[] = {0x1234U, 0xABCDU};
    uint16_t rx[2] = {0};
    // 覆盖 SDRAM 起始 4 字节。
    if (example_result == STM_OK)
    {
        example_result = sdram_write16(device, 0U, tx, 2U);
    }
    if (example_result == STM_OK)
    {
        example_result = sdram_read16(device, 0U, rx, 2U);
    }
    if (example_result == STM_OK && (rx[0] != tx[0] || rx[1] != tx[1]))
    {
        example_result = STM_ERR_VERIFY;
    }
    stm_err_t cleanup = sdram_delete(&device);
    if (example_result == STM_OK)
    {
        example_result = cleanup;
    }
    for (;;)
    {
        __WFI();
    }
}

// 初始化本板 HAL、时钟、GPIO、FMC 和 MPU
static void board_init(void)
{
    MPU_Region_InitTypeDef region = {0};

    // Region 0 禁止访问未配置的外部地址区。
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = 0x00000000UL;
    region.Size = MPU_REGION_SIZE_4GB;
    region.SubRegionDisable = 0x87U;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    // 初始化期间保持中断开启，HAL tick 须正常运行。
    if (HAL_Init() != HAL_OK)
    {
        Error_Handler();
    }

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    // HSE=25 MHz，PLL 输出 550 MHz。
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 2;
    RCC_OscInitStruct.PLL.PLLN = 44;
    RCC_OscInitStruct.PLL.PLLP = 1;
    RCC_OscInitStruct.PLL.PLLQ = 3;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    // CPU=550 MHz，HCLK=275 MHz，APB=137.5 MHz。
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }

    // CubeMX 配置：Bank2、13 行/9 列、16 位、4 Bank、CAS3、SDCLK=HCLK/3。
    MX_GPIO_Init();
    MX_FMC_Init();

    // Region 1：32 MiB，Normal non-cacheable、不可执行。
    // sdram_create() 成功前不可读写 SDRAM。
    HAL_MPU_Disable();
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = 0xD0000000UL;
    region.Size = MPU_REGION_SIZE_32MB;
    region.SubRegionDisable = 0U;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

// 板级初始化失败时停机
void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
    }
}
