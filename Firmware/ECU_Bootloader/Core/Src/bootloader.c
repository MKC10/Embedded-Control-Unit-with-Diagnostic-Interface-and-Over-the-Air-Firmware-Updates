#include "bootloader.h"
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define APP_START_ADDR 0x08010000U

extern UART_HandleTypeDef huart2;

void BL_JumpToApp(void)
{
    uint32_t app_sp    = *(volatile uint32_t*)APP_START_ADDR;
    uint32_t app_reset = *(volatile uint32_t*)(APP_START_ADDR + 4U);

    char msg[128];

    snprintf(msg, sizeof(msg),
             "APP SP: 0x%08lX\r\nAPP RESET: 0x%08lX\r\n",
             app_sp,
             app_reset);

    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    if ((app_sp < 0x20000000U) || (app_sp > 0x20020000U))
    {
        HAL_UART_Transmit(&huart2,
                          (uint8_t*)"INVALID APP SP\r\n",
                          16,
                          HAL_MAX_DELAY);
        return;
    }

    uint32_t app_reset_clean = app_reset & 0xFFFFFFFEU;

    if ((app_reset_clean < 0x08010000U) || (app_reset_clean > 0x0807FFFFU))
    {
        HAL_UART_Transmit(&huart2,
                          (uint8_t*)"INVALID APP RESET\r\n",
                          19,
                          HAL_MAX_DELAY);
        return;
    }

    HAL_UART_Transmit(&huart2,
                      (uint8_t*)"JUMPING TO APPLICATION\r\n",
                      24,
                      HAL_MAX_DELAY);

    HAL_Delay(100);

    __disable_irq();

    HAL_UART_DeInit(&huart2);
    HAL_RCC_DeInit();
    HAL_DeInit();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    SCB->VTOR = APP_START_ADDR;
    __DSB();
    __ISB();

    __set_MSP(app_sp);

    void (*app_entry)(void) = (void (*)(void))app_reset;
    app_entry();
}
