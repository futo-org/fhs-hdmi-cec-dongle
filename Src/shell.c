#include "shell.h"
#include "board.h"
#include "cec.h"
#include "cec_diag.h"
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
static void cmd_cec(const char *args);
static void cmd_diag(const char *args);
static void cmd_status(const char *args);
static void cmd_reset(const char *args);

static const command_t commands[] = {
    { "help",   "",             "this text",          cmd_help },
    { "led",    "on|off|blink", "user LED",           cmd_led },
    { "cec",    "addr|send ...", "hdmi-cec bus",       cmd_cec },
    { "diag",   "line|noise|...", "cec line instrumentation", cmd_diag },
    { "status", "",             "clock, uptime, led, cec", cmd_status },
    { "reset",  "",             "software reset",     cmd_reset },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

const char *shell_skip_spaces(const char *s)
{
    while (*s == ' ') {
        s++;
    }
    return s;
}

/* Match one word, advance past it. */
int shell_match_word(const char **cursor, const char *word)
{
    const char *s = *cursor;
    size_t n = strlen(word);

    if (strncmp(s, word, n) != 0) {
        return 0;
    }
    if (s[n] != '\0' && s[n] != ' ') {
        return 0;
    }
    *cursor = shell_skip_spaces(s + n);
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
    printf("cec     %s, addr %c, rx %lu, tx %lu, err %lu\n",
           cec_state_name(),
           cec_get_address() == CEC_ADDR_NONE
               ? '-' : "0123456789abcdef"[cec_get_address()],
           (unsigned long)cec_rx_count(),
           (unsigned long)cec_tx_count(),
           (unsigned long)cec_err_count());
}

static void cmd_reset(const char *args)
{
    (void)args;

    printf("resetting\n");
    HAL_Delay(10);                  /* let the line drain */
    NVIC_SystemReset();
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* One hex digit, advance past it. Logical addresses are written 0..f, the way
   the spec and cec-ctl write them, so decimal would be the surprising choice. */
int shell_take_nibble(const char **cursor, uint8_t *out)
{
    const char *s = *cursor;
    int v = hex_digit(*s);

    if (v < 0) {
        return 0;
    }
    s++;
    if (*s != '\0' && *s != ' ') {
        return 0;
    }
    *cursor = shell_skip_spaces(s);
    *out = (uint8_t)v;
    return 1;
}

/* One or two hex digits, advance past them. 0 if there is not one. */
int shell_take_hex_byte(const char **cursor, uint8_t *out)
{
    const char *s = *cursor;
    unsigned v = 0;
    int digits = 0;

    while (digits < 2 && hex_digit(*s) >= 0) {
        v = (v << 4) | (unsigned)hex_digit(*s);
        s++;
        digits++;
    }
    if (digits == 0 || (*s != '\0' && *s != ' ')) {
        return 0;
    }
    *cursor = shell_skip_spaces(s);
    *out = (uint8_t)v;
    return 1;
}

static void cec_usage(void)
{
    printf("usage: cec addr <0-f|none>\n");
    printf("       cec tol <std|ext>        rx timing tolerance\n");
    printf("       cec reset                re-init the peripheral\n");
    printf("       cec dump                 registers and isr counters\n");
    printf("       cec listen <own|all>     print own+broadcast, or every frame\n");
    printf("       cec listen reduced       hardware address filter (wedges, see README)\n");
    printf("       cec auto <on|off>        auto-answer TV queries\n");
    printf("       cec send <dest 0-f> [hex bytes]\n");
    printf("  e.g. cec send f 36        broadcast Standby\n");
    printf("       cec send 0           poll the TV\n");
    printf("  line diagnostics live under 'diag'\n");
}

static void cmd_cec(const char *args)
{
    const char *cursor = args;
    uint8_t payload[CEC_FRAME_MAX - 1];
    uint8_t dest;
    size_t len = 0;

    if (shell_match_word(&cursor, "addr")) {
        uint8_t addr;

        if (shell_match_word(&cursor, "none")) {
            cec_set_address(CEC_ADDR_NONE);
            printf("addr none\n");
            return;
        }
        if (!shell_take_nibble(&cursor, &addr) || *cursor != '\0') {
            cec_usage();
            return;
        }
        if (!cec_set_address(addr)) {
            printf("addr rejected\n");
            return;
        }
        printf("addr %x\n", addr);
        return;
    }

    if (shell_match_word(&cursor, "dump")) {
        cec_dump();
        return;
    }

    if (shell_match_word(&cursor, "reset")) {
        if (*cursor != '\0') {
            cec_usage();
            return;
        }
        cec_recover();
        printf("cec re-init, state %s\n", cec_state_name());
        return;
    }

    if (shell_match_word(&cursor, "tol")) {
        int on;

        if (shell_match_word(&cursor, "ext")) {
            on = 1;
        } else if (shell_match_word(&cursor, "std")) {
            on = 0;
        } else {
            cec_usage();
            return;
        }
        if (*cursor != '\0' || !cec_set_extended_tolerance(on)) {
            cec_usage();
            return;
        }
        printf("tol %s\n", on ? "ext" : "std");
        return;
    }

    if (shell_match_word(&cursor, "auto")) {
        int on;

        if (shell_match_word(&cursor, "on")) {
            on = 1;
        } else if (shell_match_word(&cursor, "off")) {
            on = 0;
        } else {
            cec_usage();
            return;
        }
        if (*cursor != '\0') {
            cec_usage();
            return;
        }
        cec_set_auto_respond(on);
        printf("auto %s\n", on ? "on" : "off");
        return;
    }

    if (shell_match_word(&cursor, "listen")) {
        int hw_all;
        int show;
        const char *name;

        if (shell_match_word(&cursor, "all")) {
            hw_all = 1;
            show = 1;
            name = "all";
        } else if (shell_match_word(&cursor, "own")) {
            hw_all = 1;
            show = 0;
            name = "own";
        } else if (shell_match_word(&cursor, "reduced")) {
            hw_all = 0;
            show = 0;
            name = "reduced (hardware filter)";
        } else {
            cec_usage();
            return;
        }
        if (*cursor != '\0' || !cec_set_listen_all(hw_all)) {
            cec_usage();
            return;
        }
        cec_set_show_all(show);
        printf("listen %s\n", name);
        return;
    }






    if (shell_match_word(&cursor, "send")) {
        if (!shell_take_nibble(&cursor, &dest)) {
            cec_usage();
            return;
        }
        while (*cursor != '\0') {
            if (len >= sizeof(payload) ||
                !shell_take_hex_byte(&cursor, &payload[len])) {
                cec_usage();
                return;
            }
            len++;
        }
        if (!cec_send(dest, payload, len)) {
            /* The only two reasons worth naming; the rest are argument bugs
               the parser above already caught. */
            printf("send rejected: %s\n",
                   cec_get_address() == CEC_ADDR_NONE ? "no own address (cec addr <n>)"
                                                      : "busy");
            return;
        }
        return;
    }

    cec_usage();
}

static void cmd_diag(const char *args)
{
    cec_diag_command(args);
}

static void dispatch(const char *cursor)
{
    if (*cursor == '\0') {
        return;
    }

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        const char *args = cursor;

        if (!shell_match_word(&args, commands[i].name)) {
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
    dispatch(shell_skip_spaces(line));
    printf(PROMPT);
}
