#ifndef CONSOLE_H
#define CONSOLE_H

/* Line editing on top of the USB debug console: echo, backspace, trim. */

/* NUL-terminated line once Enter arrives, else NULL. Buffer reused. */
char *console_take_line(void);

#endif
