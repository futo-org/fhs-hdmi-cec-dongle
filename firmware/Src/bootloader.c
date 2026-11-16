#include "bootloader.h"
#include "main.h"

#define SYSTEM_MEMORY_BASE  0x1FFFC800U
#define DFU_REQUEST_MAGIC   0xDF00B007U

/* Jumping from a reset leaves the ROM a chip with default clocks and no
   peripherals running, which is what it expects. */
static uint32_t dfu_request __attribute__((section(".noinit")));

void bootloader_enter(void)
{
    dfu_request = DFU_REQUEST_MAGIC;
    NVIC_SystemReset();
}

/* Runs before RAM is initialised, so it touches no variables. The ROM's DFU
   "leave" jumps to us with its own memory still mapped at address 0, so our
   interrupts would land in ROM handlers. */
void bootloader_map_flash(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_FLASH();
}

void bootloader_check(void)
{
    const uint32_t *vectors = (const uint32_t *)SYSTEM_MEMORY_BASE;
    void (*rom_reset)(void);

    __enable_irq();

    if (dfu_request != DFU_REQUEST_MAGIC) {
        return;
    }
    dfu_request = 0;

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

    rom_reset = (void (*)(void))vectors[1];
    __set_MSP(vectors[0]);
    rom_reset();
}
