#include "cec_diag.h"
#include "adapter.h"
#include "board.h"
#include "cec.h"
#include "main.h"
#include "shell.h"

#include <stdio.h>

/* SysTick already runs at SYSCLK for the 1 ms HAL tick, so tick plus its
   current value is a microsecond clock with no timer peripheral. */
static uint32_t micros(void)
{
    uint32_t reload = SysTick->LOAD + 1U;
    uint32_t t1;
    uint32_t t2;
    uint32_t val;

    /* Re-read across a possible tick rollover. */
    do {
        t1 = HAL_GetTick();
        val = SysTick->VAL;
        t2 = HAL_GetTick();
    } while (t1 != t2);

    return (t1 * 1000U) + ((reload - val) / (reload / 1000U));
}

static uint8_t pin_level(void)
{
    return (BOARD_CEC_PORT->IDR & BOARD_CEC_PIN) ? 1U : 0U;
}

static void pin_mode(uint32_t mode)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = BOARD_CEC_PIN;
    gpio.Mode = mode;
    gpio.Pull = BOARD_CEC_PULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_CEC_PORT, &gpio);
}

/* --- edge capture --- */

#define TRACE_MAX 128U

typedef struct {
    uint32_t us;
    uint8_t level;
} edge_t;

static edge_t trace[TRACE_MAX];

/* Sample until the buffer fills or the window closes. The caller owns the
   pin mode: cec_trace takes it off the peripheral, tx_trace leaves it on. */
static uint32_t sample_edges(uint32_t window_ms)
{
    uint32_t n = 1U;
    uint32_t start;
    uint8_t last = pin_level();

    trace[0].us = micros();
    trace[0].level = last;

    start = HAL_GetTick();
    while (n < TRACE_MAX && (HAL_GetTick() - start) < window_ms) {
        uint8_t now = pin_level();

        if (now != last) {
            trace[n].us = micros();
            trace[n].level = now;
            n++;
            last = now;
        }
    }
    return n;
}

/* CEC bit shapes, for reading the dump: start bit 3.7 ms low then high to
   4.5 ms total; a 0 is 1.5 ms low, a 1 is 0.6 ms low, both 2.4 ms total. */
static const char *edge_hint(uint8_t level, uint32_t us)
{
    if (level != 0U) {
        return "";
    }
    if (us > 3400U && us < 4000U) {
        return "  start bit";
    }
    if (us > 1300U && us < 1700U) {
        return "  data 0";
    }
    if (us > 400U && us < 800U) {
        return "  data 1";
    }
    if (us < 100U) {
        return "  GLITCH, too short for CEC";
    }
    return "";
}

static void print_edges(uint32_t edges)
{
    if (edges < 2U) {
        printf("line quiet at %s for the whole window\n",
               trace[0].level ? "high" : "low");
        return;
    }

    printf("%lu edges\n", (unsigned long)edges);
    for (uint32_t i = 1U; i < edges; i++) {
        uint32_t us = trace[i].us - trace[i - 1U].us;

        printf("  %-4s %6lu us%s\n",
               trace[i - 1U].level ? "high" : "low",
               (unsigned long)us,
               edge_hint(trace[i - 1U].level, us));
    }
}

/* --- measurements --- */

/* ~3 ms of polling at 48 MHz: far longer than any real pull-up needs. */
#define LINE_PROBE_LOOPS 20000U

static void diag_line(void)
{
    uint32_t n = 0;
    int idle;
    int driven_low;

    cec_bus_suspend();

    pin_mode(GPIO_MODE_INPUT);
    idle = pin_level();

    /* Hold low, well short of a 3.7 ms start bit so no one decodes it. */
    pin_mode(GPIO_MODE_OUTPUT_OD);
    HAL_GPIO_WritePin(BOARD_CEC_PORT, BOARD_CEC_PIN, GPIO_PIN_RESET);
    for (volatile uint32_t d = 0; d < 2000U; d++) {
    }

    /* Read the pin back while driving it. Open drain sinking into the bus
       pull-up must read low; if it reads high we cannot assert the bus at
       all, which looks exactly like a receive-only link. */
    driven_low = (pin_level() == 0U);

    /* Release and time the rise. A real pull-up recharges the line in
       microseconds; a line with no pull-up anywhere never comes back. */
    pin_mode(GPIO_MODE_INPUT);
    while (n < LINE_PROBE_LOOPS && pin_level() == 0U) {
        n++;
    }

    cec_bus_resume();

    if (n < LINE_PROBE_LOOPS) {
        printf("line: idle %s, recovered in %lu polls -> pull-up present\n",
               idle ? "high" : "low", (unsigned long)n);
    } else {
        printf("line: idle %s, stayed low -> no pull-up, line is floating\n",
               idle ? "high" : "low");
    }
    printf("      while driving: pin reads %s%s\n",
           driven_low ? "low" : "HIGH",
           driven_low ? "" : "  <- cannot assert the bus");
}

/* An idle CEC bus sits high and silent. Anything else is either traffic or,
   if the runs are milliseconds long and the edges come in bursts, a high
   impedance pin dithering across its threshold on ambient mains hum. */
static void diag_noise(uint32_t window_ms)
{
    uint32_t edges = 0;
    uint32_t high_us = 0;
    uint32_t max_high = 0;
    uint32_t max_low = 0;
    uint32_t start;
    uint32_t last_us;
    uint32_t first_us;
    uint32_t total_us;
    uint8_t last;

    cec_bus_suspend();
    pin_mode(GPIO_MODE_INPUT);

    last = pin_level();
    last_us = micros();
    first_us = last_us;
    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < window_ms) {
        uint8_t now = pin_level();

        if (now != last) {
            uint32_t now_us = micros();
            uint32_t run = now_us - last_us;

            if (last != 0U) {
                high_us += run;
                if (run > max_high) {
                    max_high = run;
                }
            } else if (run > max_low) {
                max_low = run;
            }
            edges++;
            last = now;
            last_us = now_us;
        }
    }

    total_us = micros() - first_us;
    if (last != 0U) {
        high_us += micros() - last_us;
    }

    cec_bus_resume();

    printf("over %lu ms: %lu edges, %lu%% high\n",
           (unsigned long)window_ms, (unsigned long)edges,
           (unsigned long)(total_us ? (high_us / (total_us / 100U)) : 0U));
    printf("longest high %lu us, longest low %lu us\n",
           (unsigned long)max_high, (unsigned long)max_low);
    printf("idle CEC bus = 0 edges, 100%% high\n");
}

static void diag_trace(uint32_t window_ms)
{
    uint32_t edges;

    printf("sampling %lu ms...\n", (unsigned long)window_ms);
    cec_bus_suspend();
    pin_mode(GPIO_MODE_INPUT);
    edges = sample_edges(window_ms);
    cec_bus_resume();
    print_edges(edges);
}

/* Transmit and watch our own frame. The GPIO input register still follows the
   pin in alternate function mode, so the peripheral keeps the pin while we
   sample it. Shows our bit timing and whether anyone asserts the ack slot. */
static void diag_txtrace(uint8_t dest, const uint8_t *payload, size_t len)
{
    if (!adapter_send_local(dest, payload, (uint8_t)len)) {
        printf("send rejected\n");
        return;
    }
    print_edges(sample_edges(300U));
}

static void diag_hold(uint32_t ms)
{
    uint32_t start;
    int low;

    printf("driving low for %lu ms, meter the breakout CEC pad now\n",
           (unsigned long)ms);

    cec_bus_suspend();
    pin_mode(GPIO_MODE_OUTPUT_OD);
    HAL_GPIO_WritePin(BOARD_CEC_PORT, BOARD_CEC_PIN, GPIO_PIN_RESET);

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < ms) {
    }
    low = (pin_level() == 0U);

    cec_bus_resume();
    printf("pin read back %s while driving\n", low ? "low" : "HIGH");
}

/* --- shell entry --- */

static void diag_usage(void)
{
    printf("usage: diag line              pull-up and drive test\n");
    printf("       diag noise             1 s activity summary\n");
    printf("       diag trace             sample the line, print edges\n");
    printf("       diag txtrace <d> [hex] send and scope our own frame\n");
    printf("       diag hold              drive low 5 s, meter the far end\n");
}

void cec_diag_command(const char *args)
{
    const char *cursor = args;

    if (shell_match_word(&cursor, "line") && *cursor == '\0') {
        diag_line();
        return;
    }

    cursor = args;
    if (shell_match_word(&cursor, "noise") && *cursor == '\0') {
        diag_noise(1000U);
        return;
    }

    cursor = args;
    if (shell_match_word(&cursor, "trace") && *cursor == '\0') {
        diag_trace(800U);
        return;
    }

    cursor = args;
    if (shell_match_word(&cursor, "hold") && *cursor == '\0') {
        diag_hold(5000U);
        return;
    }

    cursor = args;
    if (shell_match_word(&cursor, "txtrace")) {
        uint8_t payload[CEC_FRAME_MAX - 1];
        uint8_t dest;
        size_t len = 0;

        if (!shell_take_nibble(&cursor, &dest)) {
            diag_usage();
            return;
        }
        while (*cursor != '\0') {
            if (len >= sizeof(payload) ||
                !shell_take_hex_byte(&cursor, &payload[len])) {
                diag_usage();
                return;
            }
            len++;
        }
        diag_txtrace(dest, payload, len);
        return;
    }

    diag_usage();
}
