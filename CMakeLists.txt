cmake_minimum_required(VERSION 3.22)
project(stm_sdram C)

# 独立静态库，公开头文件，并可自动传递 CubeMX HAL 配置。
option(STM_SDRAM_LINK_CUBEMX "Link stm32cubemx target automatically" ON)
add_library(stm_sdram STATIC stm_sdram.c private/sdram_devices.c private/sdram_fmc_h7.c)
target_compile_features(stm_sdram PUBLIC c_std_11)
target_include_directories(stm_sdram PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(STM_SDRAM_LINK_CUBEMX AND TARGET stm32cubemx)
    target_link_libraries(stm_sdram PUBLIC stm32cubemx)
endif()

if(NOT TARGET stm_common)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../stm_common/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../stm_common" "${CMAKE_CURRENT_BINARY_DIR}/stm_common")
    else()
        option(STM_COMMON_FETCH "Download stm_common when no local target or sibling exists" ON)
        set(STM_COMMON_GIT_REPOSITORY "https://github.com/NingZiXi/stm_common.git"
            CACHE STRING "stm_common Git repository (GitHub or Gitee mirror)")
        if(NOT STM_COMMON_FETCH)
            message(FATAL_ERROR "stm_common is missing. Provide its target or sibling directory, or enable STM_COMMON_FETCH.")
        endif()
        include(FetchContent)
        # v1.0.0：固定提交；两个驱动使用相同依赖名，只创建一个 target。
        FetchContent_Declare(stm_common
            GIT_REPOSITORY "${STM_COMMON_GIT_REPOSITORY}"
            GIT_TAG ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1
        )
        FetchContent_MakeAvailable(stm_common)
    endif()
endif()
if(NOT TARGET stm_common)
    message(FATAL_ERROR "stm_common dependency did not provide the stm_common target")
endif()
target_link_libraries(stm_sdram PUBLIC stm_common)
