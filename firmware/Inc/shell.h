#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* Debug console over USB CDC: banner when a terminal opens the port, then
   one command per line. Main loop. */
void shell_task(void);

/* Argument parsers, shared with the command modules. Each advances the cursor
   past what it consumed and returns 0 if the next token is not that shape. */
const char *shell_skip_spaces(const char *s);
int shell_match_word(const char **cursor, const char *word);
int shell_take_nibble(const char **cursor, uint8_t *out);
int shell_take_hex_byte(const char **cursor, uint8_t *out);

#endif
