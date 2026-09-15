/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Nucleo-F072RB: LED on PA5 + command shell on the ST-LINK VCP.
 ******************************************************************************
 */

#include "main.h"
#include "uart.h"

#include <stdio.h>
#include <string.h>

#define LED_PORT GPIOA
#define LED_PIN  GPIO_PIN_5

#define BLINK_MS 500U

typedef enum { LED_OFF, LED_ON, LED_BLINK } led_mode_t;

static led_mode_t led_mode = LED_BLINK;

static void SystemClock_Config(void);
static void LED_Init(void);
static void handle_command(char *cmd);
static void prompt(void);

void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    uint32_t next_blink;

    HAL_Init();
    SystemClock_Config();
    LED_Init();
    uart_init();

    printf("\n\nfhs-hdmi-cec on NUCLEO-F072RB, SYSCLK %lu Hz\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("type 'help' for commands\n");
    prompt();

    next_blink = HAL_GetTick();

    for (;;) {
        char *cmd = uart_take_line();

        if (cmd != NULL) {
            handle_command(cmd);
            prompt();
        }

        if (led_mode == LED_BLINK && (HAL_GetTick() - next_blink) >= BLINK_MS) {
            next_blink += BLINK_MS;
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        }
    }
}

static void prompt(void)
{
    printf("> ");
}

static void handle_command(char *cmd)
{
    if (cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        printf("help          this text\n");
        printf("led on|off|blink\n");
        printf("status        clock, uptime, led\n");
        printf("reset         software reset\n");
    } else if (strcmp(cmd, "led on") == 0) {
        led_mode = LED_ON;
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    } else if (strcmp(cmd, "led off") == 0) {
        led_mode = LED_OFF;
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    } else if (strcmp(cmd, "led blink") == 0) {
        led_mode = LED_BLINK;
    } else if (strcmp(cmd, "status") == 0) {
        printf("sysclk  %lu Hz\n", (unsigned long)HAL_RCC_GetSysClockFreq());
        printf("pclk1   %lu Hz\n", (unsigned long)HAL_RCC_GetPCLK1Freq());
        printf("uptime  %lu ms\n", (unsigned long)HAL_GetTick());
        printf("led     %s\n", led_mode == LED_ON ? "on" :
                               led_mode == LED_OFF ? "off" : "blink");
    } else if (strcmp(cmd, "reset") == 0) {
        printf("resetting\n");
        HAL_Delay(10);
        NVIC_SystemReset();
    } else {
        printf("unknown: '%s' (try 'help')\n", cmd);
    }
}

/* F072 has HSI48, so RCC_PLLSOURCE_HSI means HSI/PREDIV, not HSI/2.
   8 MHz / 2 * 12 = 48 MHz, the part max. Flash needs 1 wait state above 24 MHz. */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLMUL = RCC_PLL_MUL12;
    osc.PLL.PREDIV = RCC_PREDIV_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) {
        Error_Handler();
    }
}

static void LED_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;);
}
