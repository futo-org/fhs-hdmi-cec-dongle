#ifndef LED_H
#define LED_H

typedef enum {
    LED_OFF,
    LED_ON,
    LED_BLINK
} led_mode_t;

void led_init(void);
void led_set_mode(led_mode_t mode);
led_mode_t led_get_mode(void);
const char *led_mode_name(led_mode_t mode);

/* Name -> mode. 0 if unknown. */
int led_mode_from_name(const char *name, led_mode_t *out);

/* Drives the blink. Call from main loop. */
void led_task(void);

#endif
