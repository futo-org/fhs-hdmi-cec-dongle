#ifndef BOOTLOADER_H
#define BOOTLOADER_H

/* Reset into the ROM USB DFU bootloader. Does not return. */
void bootloader_enter(void);

/* Call first thing in main: finishes a bootloader_enter() from before the reset. */
void bootloader_check(void);

#endif
