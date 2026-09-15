#include "shell.h"
#include "board.h"
#include "led.h"

#include <stdio.h>
#include <string.h>

#define PROMPT "> "

typedef void (*command_fn)(const char *args);

typedef struct {
    const char *name;
    const char *usage;              /* "" = takes no args */
    const char *summary;
    command_fn run;
} command_t;

static void cmd_help(const char *args);
static void cmd_led(const char *args);
static void cmd_status(const char *args);
static void cmd_reset(const char *args);

static const command_t commands[] = {
    { "help",   "",             "this text",          cmd_help },
    { "led",    "on|off|blink", "user LED",           cmd_led },
    { "status", "",             "clock, uptime, led", cmd_status },
    { "reset",  "",             "software reset",     cmd_reset },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static const char *skip_spaces(const char *s)
{
    while (*s == ' ') {
        s++;
    }
    return s;
}

/* Match one word, advance past it. */
static int match_word(const char **cursor, const char *word)
{
    const char *s = *cursor;
    size_t n = strlen(word);

    if (strncmp(s, word, n) != 0) {
        return 0;
    }
    if (s[n] != '\0' && s[n] != ' ') {
        return 0;
    }
    *cursor = skip_spaces(s + n);
    return 1;
}

static void usage(const command_t *c)
{
    if (c->usage[0] == '\0') {
        printf("%s takes no arguments\n", c->name);
    } else {
        printf("usage: %s %s\n", c->name, c->usage);
    }
}

static void cmd_help(const char *args)
{
    (void)args;

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        printf("  %-6s %-14s %s\n",
               commands[i].name, commands[i].usage, commands[i].summary);
    }
}

static void cmd_led(const char *args)
{
    led_mode_t mode;

    if (led_mode_from_name(args, &mode)) {
        led_set_mode(mode);
    } else {
        usage(&commands[1]);
    }
}

static void cmd_status(const char *args)
{
    (void)args;

    printf("sysclk  %lu Hz\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("pclk1   %lu Hz\n", (unsigned long)HAL_RCC_GetPCLK1Freq());
    printf("uptime  %lu ms\n", (unsigned long)HAL_GetTick());
    printf("led     %s\n", led_mode_name(led_get_mode()));
}

static void cmd_reset(const char *args)
{
    (void)args;

    printf("resetting\n");
    HAL_Delay(10);                  /* let the line drain */
    NVIC_SystemReset();
}

static void dispatch(const char *cursor)
{
    if (*cursor == '\0') {
        return;
    }

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        const char *args = cursor;

        if (!match_word(&args, commands[i].name)) {
            continue;
        }
        if (commands[i].usage[0] == '\0' && *args != '\0') {
            usage(&commands[i]);     /* takes none, got some */
            return;
        }
        commands[i].run(args);
        return;
    }

    printf("unknown: '%s' (try 'help')\n", cursor);
}

void shell_begin(void)
{
    printf("\n\nfhs-hdmi-cec on NUCLEO-F072RB, SYSCLK %lu Hz\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("type 'help' for commands\n");
    printf(PROMPT);
}

void shell_execute(const char *line)
{
    dispatch(skip_spaces(line));
    printf(PROMPT);
}
