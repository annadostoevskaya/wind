cmake_minimum_required(VERSION 3.22)

project(wind LANGUAGES C ASM)

option(WIND_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WIND_CORE_SOURCES
    Core/Src/app_logic.c
    Core/Src/main.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/stm32f4xx_it.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    Core/Src/system_stm32f4xx.c
)

set(WIND_HAL_SOURCES
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_adc.c
)

add_executable(wind
    ${WIND_CORE_SOURCES}
    ${WIND_HAL_SOURCES}
    Core/Startup/startup_stm32f407vgtx.s
)

set_target_properties(wind PROPERTIES
    OUTPUT_NAME wind
    SUFFIX .elf
)

target_include_directories(wind PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
)

target_compile_definitions(wind PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

set(WIND_MCU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

target_compile_options(wind PRIVATE
    ${WIND_MCU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -fstack-usage
    -Wall
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Release>:-g1>
)

set_source_files_properties(${WIND_CORE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wextra;-Wformat=2;-Wshadow"
)

if(WIND_WARNINGS_AS_ERRORS)
    target_compile_options(wind PRIVATE -Werror)
endif()

target_link_options(wind PRIVATE
    ${WIND_MCU_FLAGS}
    -T${CMAKE_SOURCE_DIR}/STM32F407VGTX_FLASH.ld
    --specs=nosys.specs
    --specs=nano.specs
    -static
    -Wl,--gc-sections
    -Wl,--cref
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/wind.map
    -Wl,--start-group
    -lc
    -lm
    -Wl,--end-group
)

add_custom_command(TARGET wind POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:wind>
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:wind> ${CMAKE_CURRENT_BINARY_DIR}/wind.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:wind> ${CMAKE_CURRENT_BINARY_DIR}/wind.bin
    BYPRODUCTS
        ${CMAKE_CURRENT_BINARY_DIR}/wind.hex
        ${CMAKE_CURRENT_BINARY_DIR}/wind.bin
    COMMENT "Generating production firmware images"
)
