#include "firmware.h"
#include "sht31.h"
#include "bmp280.h"
#include "state_machine.h"
#include "diagnostic.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include <stdint.h>

#define SENSOR_TEST_MODE  1

SHT31_Data_t     sht31_data;
SHT31_Status_t   sht31_status;
BMP280_Data_t    bmp280_data;
BMP280_Status_t  bmp280_status;
SystemContext_t  sys_ctx;

volatile uint8_t timer_tick = 0;

void Firmware_Init(void)
{
    HAL_TIM_Base_Start_IT(&htim2);

    HAL_UART_Transmit(&huart2,
                      (uint8_t*)"FIRMWARE INIT\r\n",
                      15,
                      HAL_MAX_DELAY);

#if SENSOR_TEST_MODE

    SM_Init(&sys_ctx);
    DIAG_Init(&huart2);

#else

    sht31_status = SHT31_Init(&hi2c1);
    if (sht31_status != SHT31_OK)
    {
        Error_Handler();
    }

    bmp280_status = BMP280_Init(&hspi1);
    if (bmp280_status != BMP280_OK)
    {
        Error_Handler();
    }

    SM_Init(&sys_ctx);
    DIAG_Init(&huart2);

#endif
}

void Firmware_Run(void)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t*)"FIRMWARE RUNNING\r\n",
                      18,
                      HAL_MAX_DELAY);

    while (1)
    {
        if (timer_tick)
        {
            timer_tick = 0;

            SM_Update(&sys_ctx,
                      &sht31_data,
                      &bmp280_data,
                      &hi2c1,
                      &hspi1);
        }

        DIAG_Process(&huart2,
                     &sys_ctx,
                     &sht31_data,
                     &bmp280_data);
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        timer_tick = 1;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        DIAG_RxCallback(huart);
    }
}
