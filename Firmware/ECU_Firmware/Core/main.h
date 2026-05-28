#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* Exported functions prototypes */
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* Onboard Nucleo LED */
#define LD2_Pin GPIO_PIN_5
#define LD2_GPIO_Port GPIOA

/* BMP280 SPI chip select */
#define BMP280_CS_Pin GPIO_PIN_4
#define BMP280_CS_GPIO_Port GPIOA

/* Optional aliases if your code uses BMP280_CS instead of BMP280_CS_Pin */
#define BMP280_CS_GPIO_Port GPIOA
#define BMP280_CS_Pin GPIO_PIN_4

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
