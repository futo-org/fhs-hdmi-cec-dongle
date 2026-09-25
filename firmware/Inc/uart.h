#ifndef UART_H
#define UART_H

#include <stdint.h>

/* Console UART (board.h), wired to the ST-LINK VCP. Bytes only, no line editing. */

void uart_init(void);

/* Next received byte, -1 if none. */
int uart_getc(void);

/* Called by the vector in stm32f0xx_it.c. */
void uart_irq_handler(void);

/* stdio glue. _write overrides the weak one in syscall.c. */
int _write(int file, char *ptr, int len);
int __io_putchar(int ch);
int __io_getchar(void);

#endif
