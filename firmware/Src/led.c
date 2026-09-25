#include "led.h"
#include "board.h"

#include <string.h>

#define BLINK_INTERVAL_MS 500U

/* One table, one place: enum, name and lookup all read from it. */
static const struct {
    led_mode_t mode;
    const char *name;
} mode_names[] = {
    { LED_OFF,   "off" },
    { LED_ON,    "on" },
    { LED_BLINK, "blink" },
};

#define MODE_COUNT (sizeof(mode_names) / sizeof(mode_names[0]))

static led_mode_t mode = LED_BLINK;
static uint32_t next_toggle;

static void led_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, state);
}

void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    BOARD_LED_CLK_ENABLE();

    gpio.Pin = BOARD_LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LED_PORT, &gpio);

    next_toggle = HAL_GetTick();
}

void led_set_mode(led_mode_t new_mode)
{
    mode = new_mode;

    switch (mode) {
    case LED_ON:
        led_write(GPIO_PIN_SET);
        break;
    case LED_OFF:
        led_write(GPIO_PIN_RESET);
        break;
    case LED_BLINK:
        /* restart the phase: a stale deadline would flicker until it caught up */
        next_toggle = HAL_GetTick();
        break;
    }
}

led_mode_t led_get_mode(void)
{
    return mode;
}

const char *led_mode_name(led_mode_t query)
{
    for (size_t i = 0; i < MODE_COUNT; i++) {
        if (mode_names[i].mode == query) {
            return mode_names[i].name;
        }
    }
    return "?";
}

int led_mode_from_name(const char *name, led_mode_t *out)
{
    for (size_t i = 0; i < MODE_COUNT; i++) {
        if (strcmp(name, mode_names[i].name) == 0) {
            *out = mode_names[i].mode;
            return 1;
        }
    }
    return 0;
}

void led_task(void)
{
    if (mode != LED_BLINK) {
        return;
    }
    if ((HAL_GetTick() - next_toggle) < BLINK_INTERVAL_MS) {
        return;
    }
    next_toggle += BLINK_INTERVAL_MS;
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
}
