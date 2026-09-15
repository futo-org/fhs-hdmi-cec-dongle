#include "console.h"
#include "uart.h"

#include <stdio.h>

#define LINE_MAX 64

static char line[LINE_MAX];
static uint16_t fill;

char *console_take_line(void)
{
    int ch;

    while ((ch = uart_getc()) >= 0) {
        char c = (char)ch;

        if (c == '\r' || c == '\n') {
            __io_putchar('\n');
            while (fill > 0U && line[fill - 1U] == ' ') {
                fill--;                 /* trailing space breaks arg match */
            }
            line[fill] = '\0';
            fill = 0;
            return line;
        }
        if (c == '\b' || c == 0x7F) {
            if (fill > 0U) {
                fill--;
                printf("\b \b");
            }
        } else if (fill < (LINE_MAX - 1U) && c >= ' ' && c < 0x7F) {
            line[fill++] = c;
            __io_putchar(c);            /* terminal is raw, we echo */
        }
    }
    return NULL;
}
