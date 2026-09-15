#ifndef UART_H
#define UART_H

/* USART2 on PA2/PA3 -> ST-LINK VCP (/dev/ttyACM0), 115200 8N1.
   printf goes out here; lines typed in the terminal come back via uart_take_line(). */

void uart_init(void);

/* Drains whatever arrived, echoes it, and returns a NUL-terminated line once the
   user pressed Enter, else NULL. Call it from the main loop. The buffer is reused,
   so consume it before the next call. */
char *uart_take_line(void);

/* stdio glue, defined here and called by syscall.c. */
int __io_putchar(int ch);
int __io_getchar(void);

#endif
