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
  ${HAL_DIR}/Src/stm32f0xx_hal_uart.c
  ${HAL_DIR}/Src/stm32f0xx_hal_uart_ex.c
)

# Vendor code, not ours to lint.
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F0xx/Source/Templates/system_stm32f0xx.c
  TARGET_DIRECTORY ${PROJECT_NAME} PROPERTIES COMPILE_OPTIONS "-w")
file(GLOB HAL_SRCS ${HAL_DIR}/Src/*.c)
set_source_files_properties(${HAL_SRCS}
  TARGET_DIRECTORY ${PROJECT_NAME} PROPERTIES COMPILE_OPTIONS "-w")

# Add include directories
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Core/Include
  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F0xx/Include
  ${HAL_DIR}/Inc
)

# Compiler options
target_compile_options(${PROJECT_NAME} PRIVATE
  # User defined compiler options
)

# Add additional defined symbols
target_compile_definitions(${PROJECT_NAME} PRIVATE
  STM32F072xB
  USE_HAL_DRIVER
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
