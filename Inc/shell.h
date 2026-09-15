#ifndef SHELL_H
#define SHELL_H

/* Banner + first prompt. */
void shell_begin(void);

/* Run one line, then re-prompt. */
void shell_execute(const char *line);

#endif
