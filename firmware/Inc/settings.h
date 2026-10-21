#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* HDMI input 1 of the display, until the host stores the real one. */
#define SETTINGS_DEFAULT_PHYS_ADDR 0x1000U

/* Physical address kept in the last flash page across power cycles. */
uint16_t settings_phys_addr(void);

/* Erases and rewrites the page: stalls the CPU for tens of ms. Returns 0 if
   the flash did not take it. */
int settings_store_phys_addr(uint16_t phys_addr);

#endif
