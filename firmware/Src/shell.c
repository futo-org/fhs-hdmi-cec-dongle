#include "shell.h"
#include "adapter.h"
#include "board.h"
#include "cec.h"
#include "cec_diag.h"
#include "cec_log.h"
#include "console.h"
#include "led.h"
#include "settings.h"
#include "version.h"

#include "tusb.h"

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
static void cmd_fault(const char *args);
static void cmd_diag(const char *args);
static void cmd_status(const char *args);
static void cmd_reset(const char *args);

static const command_t commands[] = {
    { "help",   "",               "this text",                cmd_help },
    { "led",    "on|off|blink",   "status LED",               cmd_led },
    { "cec",    "send|listen|...", "hdmi-cec bus",            cmd_cec },
    { "fault",  "nack|late|...",  "break the next host exchange", cmd_fault },
    { "diag",   "line|noise|...", "cec line instrumentation", cmd_diag },
    { "status", "",               "clock, usb, adapter, cec", cmd_status },
    { "reset",  "",               "software reset",           cmd_reset },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static const struct {
    adapter_fault_t fault;
    const char *name;
    const char *summary;
} fault_names[] = {
    { ADAPTER_FAULT_NONE,         "none",  "cancel a pending fault" },
    { ADAPTER_FAULT_NACK,         "nack",  "next host frame reports NACK, unsent" },
    { ADAPTER_FAULT_LATE_TX_DONE, "late",  "next host result arrives after the kernel timeout" },
    { ADAPTER_FAULT_DROP_RX,      "drop",  "next received frame never reaches the host" },
    { ADAPTER_FAULT_WEDGE_TX,     "wedge", "next host result is never reported" },
};

#define FAULT_COUNT (sizeof(fault_names) / sizeof(fault_names[0]))

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
        printf("  %-6s %-15s %s\n",
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

static const char *fault_name(adapter_fault_t f)
{
    for (size_t i = 0; i < FAULT_COUNT; i++) {
        if (fault_names[i].fault == f) {
            return fault_names[i].name;
        }
    }
    return "?";
}

static void cmd_status(const char *args)
{
    cec_stats_t s;
    uint16_t pa = settings_phys_addr();

    (void)args;

    cec_get_stats(&s);
    printf("firmware %u.%u.%u, sysclk %lu Hz, uptime %lu ms\n",
           FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
           (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)HAL_GetTick());
    printf("usb      %s, adapter %s, ack mask %04x\n",
           tud_mounted() ? "configured" : "not configured",
           adapter_enabled() ? "enabled" : "idle", cec_get_ack_mask());
    printf("stored   physical address %x.%x.%x.%x\n",
           (pa >> 12) & 0xFU, (pa >> 8) & 0xFU, (pa >> 4) & 0xFU, pa & 0xFU);
    printf("cec      %s, rx %lu, tx %lu, recoveries %lu\n", cec_state_name(),
           (unsigned long)s.rx_frames, (unsigned long)s.tx_frames,
           (unsigned long)s.recoveries);
    printf("fault    %s pending\n", fault_name(adapter_pending_fault()));
    printf("led      %s\n", led_mode_name(led_get_mode()));
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
    printf("usage: cec send <dest 0-f> [hex bytes]  from the host's address, else 15\n");
    printf("       cec listen <own|all>     print own+broadcast, or every frame\n");
    printf("       cec raw <on|off>         raw wire bytes before each frame (default on)\n");
    printf("       cec tol <std|ext>        rx timing tolerance\n");
    printf("       cec reset                re-init the peripheral\n");
    printf("       cec dump                 registers and isr counters\n");
    printf("  e.g. cec send 0           poll the TV\n");
    printf("  a local send aborts a host frame in flight\n");
}

static int take_on_off(const char **cursor, int *on)
{
    if (shell_match_word(cursor, "on")) {
        *on = 1;
    } else if (shell_match_word(cursor, "off")) {
        *on = 0;
    } else {
        return 0;
    }
    return **cursor == '\0';
}

static void cmd_cec(const char *args)
{
    const char *cursor = args;
    uint8_t payload[CEC_FRAME_MAX - 1];
    uint8_t dest;
    uint8_t len = 0;
    int on;

    if (shell_match_word(&cursor, "dump") && *cursor == '\0') {
        cec_dump();
    } else if (shell_match_word(&cursor, "raw") && take_on_off(&cursor, &on)) {
        cec_log_set_show_raw(on);
        printf("raw %s\n", on ? "on" : "off");
    } else if (shell_match_word(&cursor, "reset") && *cursor == '\0') {
        cec_recover();
        printf("cec re-init, state %s\n", cec_state_name());
    } else if (shell_match_word(&cursor, "tol")) {
        if (shell_match_word(&cursor, "ext") && *cursor == '\0') {
            cec_set_extended_tolerance(1);
        } else if (shell_match_word(&cursor, "std") && *cursor == '\0') {
            cec_set_extended_tolerance(0);
        } else {
            cec_usage();
            return;
        }
        printf("tol %s\n", cec_get_extended_tolerance() ? "ext" : "std");
    } else if (shell_match_word(&cursor, "listen")) {
        if (shell_match_word(&cursor, "all") && *cursor == '\0') {
            cec_log_set_show_all(1);
        } else if (shell_match_word(&cursor, "own") && *cursor == '\0') {
            cec_log_set_show_all(0);
        } else {
            cec_usage();
            return;
        }
        printf("listen %s\n", cec_log_get_show_all() ? "all" : "own");
    } else if (shell_match_word(&cursor, "send") && shell_take_nibble(&cursor, &dest)) {
        while (*cursor != '\0') {
            if (len >= sizeof(payload) || !shell_take_hex_byte(&cursor, &payload[len])) {
                cec_usage();
                return;
            }
            len++;
        }
        if (!adapter_send_local(dest, payload, len)) {
            printf("send rejected\n");
        }
    } else {
        cec_usage();
    }
}

static void cmd_fault(const char *args)
{
    const char *cursor = args;

    for (size_t i = 0; i < FAULT_COUNT; i++) {
        if (shell_match_word(&cursor, fault_names[i].name) && *cursor == '\0') {
            adapter_inject_fault(fault_names[i].fault);
            printf("fault %s armed\n", fault_names[i].name);
            return;
        }
    }
    for (size_t i = 0; i < FAULT_COUNT; i++) {
        printf("  fault %-6s %s\n", fault_names[i].name, fault_names[i].summary);
    }
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

static void banner(void)
{
    printf("\nfhs-cec debug console, firmware %u.%u.%u\n",
           FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    printf("type 'help' for commands\n");
    printf(PROMPT);
}

void shell_task(void)
{
    static bool was_connected;
    bool connected = tud_cdc_connected();
    const char *line;

    /* Output before a terminal opens the port goes nowhere. */
    if (connected && !was_connected) {
        banner();
    }
    was_connected = connected;

    line = console_take_line();
    if (line != NULL) {
        dispatch(shell_skip_spaces(line));
        printf(PROMPT);
    }
}
