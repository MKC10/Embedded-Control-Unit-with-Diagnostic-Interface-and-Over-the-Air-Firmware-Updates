#ifndef INC_BMP280_H_
#define INC_BMP280_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ── CS pin control ───────────────────────────────────────────── */
#define BMP280_CS_LOW()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define BMP280_CS_HIGH()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)

/* ── Register addresses ───────────────────────────────────────── */
#define BMP280_REG_ID         0xD0    /* Chip ID — should read 0x60        */
#define BMP280_REG_RESET      0xE0    /* Write 0xB6 to soft reset          */
#define BMP280_REG_STATUS     0xF3    /* Measuring/im_update status        */
#define BMP280_REG_CTRL_MEAS  0xF4    /* osrs_t, osrs_p, mode              */
#define BMP280_REG_CONFIG     0xF5    /* t_sb, filter, spi3w_en            */
#define BMP280_REG_PRESS_MSB  0xF7    /* Pressure MSB — burst read start   */
#define BMP280_REG_CALIB_00   0x88    /* Factory trim start — 24 bytes     */

/* ── Configuration values ─────────────────────────────────────── */
#define BMP280_CHIP_ID        0x60    /* Expected chip ID                  */
#define BMP280_SOFT_RESET     0xB6    /* Reset command                     */

/* ── ctrl_meas register ───────────────────────────────────────── */
/* osrs_t = 001 (x1 oversampling temp)                             */
/* osrs_p = 011 (x4 oversampling pressure)                         */
/* mode   = 11  (normal mode — continuous measurement)             */
#define BMP280_CTRL_MEAS_VAL  0x4F

/* ── config register ──────────────────────────────────────────── */
/* t_sb   = 000 (0.5ms standby)                                    */
/* filter = 100 (IIR filter coefficient 16)                        */
/* spi3w  = 0   (4-wire SPI)                                       */
#define BMP280_CONFIG_VAL     0x10

/* ── Trim coefficient struct ──────────────────────────────────── */
/* Factory calibration data read from 0x88-0x9F on startup        */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} BMP280_Calib_t;

/* ── Result struct ────────────────────────────────────────────── */
typedef struct {
    float temperature;   /* Degrees Celsius                        */
    float pressure;      /* Hectopascals (hPa)                     */
    bool  valid;         /* true = chip ID matched, data in range  */
} BMP280_Data_t;

/* ── Status codes ─────────────────────────────────────────────── */
typedef enum {
    BMP280_OK           = 0,
    BMP280_ERR_SPI      = 1,   /* HAL SPI transaction failed        */
    BMP280_ERR_ID       = 2,   /* Chip ID mismatch                  */
    BMP280_ERR_RANGE    = 3    /* Value out of valid range          */
} BMP280_Status_t;

/* ── Public API ───────────────────────────────────────────────── */
BMP280_Status_t BMP280_Init(SPI_HandleTypeDef *hspi);
BMP280_Status_t BMP280_Read(SPI_HandleTypeDef *hspi, BMP280_Data_t *data);

#endif /* INC_BMP280_H_ */
