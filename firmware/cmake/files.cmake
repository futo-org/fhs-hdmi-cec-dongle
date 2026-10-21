# Add sources to executable/library
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/syscall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/adapter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/bootloader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/cec.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/ddc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/led.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/settings.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32f0xx_it.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/system_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/usb.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/startup_stm32f0x2xx.S"
)

if(FHS_CEC_DEBUG_SHELL)
    target_sources(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/cec_diag.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/cec_log.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/console.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/shell.c"
    )
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/stm32f072xb_flash.ld" "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stm32f072xb_flash.ld")
