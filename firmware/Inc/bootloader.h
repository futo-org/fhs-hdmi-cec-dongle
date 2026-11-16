#ifndef BOOTLOADER_H
#define BOOTLOADER_H

/* Reset into the ROM USB DFU bootloader. Does not return. */
void bootloader_enter(void);

/* Called from Reset_Handler with interrupts masked: maps main flash at
   address 0, where the ROM bootloader leaves its own. */
void bootloader_map_flash(void);

/* Call first thing in main: unmasks interrupts and finishes a
   bootloader_enter() from before the reset. */
void bootloader_check(void);

#endif
