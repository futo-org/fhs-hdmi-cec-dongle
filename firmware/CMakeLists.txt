# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.20)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# Set target definition variables required by the toolchain file
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/target.cmake)

# Set the toolchain file for cross-compilation
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

# Project definition
project(fhs-hdmi-controller)

# Enable CMake support for ASM and C/CPP languages
enable_language(C ASM)

set(PROJECT_NAME fhs-hdmi-controller)

# USB CDC debug console, bench diagnostics and fault injection. The presets
# turn it on for Debug and off for Release.
option(FHS_CEC_DEBUG_SHELL "Build the USB debug console" OFF)

# Create an executable object type
add_executable(${PROJECT_NAME})

# Include additional CMake files for flags, components, and files
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/components.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/files.cmake)

# CMSIS device + the HAL modules enabled in Inc/stm32f0xx_hal_conf.h.
# Enabling another module there means adding its .c here too.
set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver)
target_sources(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F0xx/Source/Templates/system_stm32f0xx.c
  ${HAL_DIR}/Src/stm32f0xx_hal.c
  ${HAL_DIR}/Src/stm32f0xx_hal_cortex.c
  ${HAL_DIR}/Src/stm32f0xx_hal_rcc.c
  ${HAL_DIR}/Src/stm32f0xx_hal_rcc_ex.c
  ${HAL_DIR}/Src/stm32f0xx_hal_gpio.c
  ${HAL_DIR}/Src/stm32f0xx_hal_exti.c
  ${HAL_DIR}/Src/stm32f0xx_hal_pwr.c
  ${HAL_DIR}/Src/stm32f0xx_hal_pwr_ex.c
  ${HAL_DIR}/Src/stm32f0xx_hal_flash.c
  ${HAL_DIR}/Src/stm32f0xx_hal_flash_ex.c
  ${HAL_DIR}/Src/stm32f0xx_hal_dma.c
  ${HAL_DIR}/Src/stm32f0xx_hal_cec.c
  ${HAL_DIR}/Src/stm32f0xx_hal_i2c.c
  ${HAL_DIR}/Src/stm32f0xx_hal_i2c_ex.c
  ${HAL_DIR}/Src/stm32f0xx_hal_uart.c
  ${HAL_DIR}/Src/stm32f0xx_hal_uart_ex.c
)

# TinyUSB device stack, vendored like the HAL: device core, CDC, STM32 FSDEV port.
set(TUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/tinyusb/src)
set(TUSB_SRCS
  ${TUSB_DIR}/tusb.c
  ${TUSB_DIR}/common/tusb_fifo.c
  ${TUSB_DIR}/device/usbd.c
  ${TUSB_DIR}/class/cdc/cdc_device.c
  ${TUSB_DIR}/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
  ${TUSB_DIR}/portable/st/stm32_fsdev/fsdev_common.c
)
target_sources(${PROJECT_NAME} PRIVATE ${TUSB_SRCS})

# Vendor code, not ours to lint.
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F0xx/Source/Templates/system_stm32f0xx.c
  TARGET_DIRECTORY ${PROJECT_NAME} PROPERTIES COMPILE_OPTIONS "-w")
file(GLOB HAL_SRCS ${HAL_DIR}/Src/*.c)
set_source_files_properties(${HAL_SRCS} ${TUSB_SRCS}
  TARGET_DIRECTORY ${PROJECT_NAME} PROPERTIES COMPILE_OPTIONS "-w")

# Add include directories
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Core/Include
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F0xx/Include
  ${HAL_DIR}/Inc
  ${TUSB_DIR}
  # fhs-cec-proto.h, the wire protocol shared with the kernel driver
  ${CMAKE_CURRENT_SOURCE_DIR}/../driver
)

# Compiler options
target_compile_options(${PROJECT_NAME} PRIVATE
  # User defined compiler options
)

# Add additional defined symbols
target_compile_definitions(${PROJECT_NAME} PRIVATE
  STM32F072xB
  USE_HAL_DRIVER
  FHS_CEC_DEBUG_SHELL=$<BOOL:${FHS_CEC_DEBUG_SHELL}>
)

# Linker options
target_link_options(${PROJECT_NAME} PRIVATE 
  # User defined linker options
)

# Add additional linker search directories
target_link_directories(${PROJECT_NAME} PRIVATE
  # Add additional linker search directories here
)

# Add additional linker libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
  # Add additional linker libraries here
)
