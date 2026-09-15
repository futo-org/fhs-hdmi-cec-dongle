/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Blink the Nucleo-F072RB user LED (LD2, PA5) at 1 Hz.
 ******************************************************************************
 */

#include "stm32f0xx_hal.h"

#define LED_PORT GPIOA
#define LED_PIN  GPIO_PIN_5

static void SystemClock_Config(void);
static void LED_Init(void);
void Error_Handler(void);

void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    LED_Init();

    for (;;) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(100);
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
