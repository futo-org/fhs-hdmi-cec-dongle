#include "settings.h"
#include "main.h"

#include <stddef.h>

#define SETTINGS_MAGIC 0x43454331UL     /* "CEC1" */

typedef struct {
    uint32_t magic;
    uint16_t phys_addr;
    uint16_t phys_addr_inverted;        /* a torn write never matches */
} settings_t;

/* Linker script: the last flash page, outside the image. */
extern const uint8_t _settings_page[];

static const settings_t *stored(void)
{
    return (const settings_t *)_settings_page;
}

uint16_t settings_phys_addr(void)
{
    const settings_t *s = stored();

    if (s->magic != SETTINGS_MAGIC ||
        s->phys_addr != (uint16_t)~s->phys_addr_inverted) {
        return SETTINGS_DEFAULT_PHYS_ADDR;
    }
    return s->phys_addr;
}

int settings_store_phys_addr(uint16_t phys_addr)
{
    const settings_t want = {
        .magic = SETTINGS_MAGIC,
        .phys_addr = phys_addr,
        .phys_addr_inverted = (uint16_t)~phys_addr,
    };
    const uint16_t *src = (const uint16_t *)&want;
    uint32_t dst = (uint32_t)_settings_page;
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = dst,
        .NbPages = 1,
    };
    uint32_t bad_page;
    int ok = 1;

    if (settings_phys_addr() == phys_addr && stored()->magic == SETTINGS_MAGIC) {
        return 1;                   /* spare the flash a write cycle */
    }

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &bad_page) != HAL_OK) {
        ok = 0;
    }
    for (size_t i = 0; ok && i < sizeof(want) / sizeof(*src); i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, dst + i * sizeof(*src),
                              src[i]) != HAL_OK) {
            ok = 0;
        }
    }
    HAL_FLASH_Lock();

    return ok && settings_phys_addr() == phys_addr;
}
