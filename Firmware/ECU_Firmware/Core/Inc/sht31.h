#ifndef INC_SHT31_H_
#define INC_SHT31_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Device address ───────────────────────────────────────────── */
#define SHT31_ADDR          (0x44 << 1)   /* ADDR pin LOW  → 0x44, shifted for HAL */

/* ── Measurement commands ─────────────────────────────────────── */
#define SHT31_CMD_MEAS_H    0x2C06        /* High repeatability, clock stretching  */
#define SHT31_CMD_SOFT_RST  0x30A2        /* Soft reset                            */

/* ── Timing ───────────────────────────────────────────────────── */
#define SHT31_MEAS_DELAY_MS 20            /* Wait after trigger — datasheet 15ms + margin */

/* ── Raw data buffer size ─────────────────────────────────────── */
#define SHT31_RAW_BYTES     6             /* T_MSB T_LSB T_CRC RH_MSB RH_LSB RH_CRC */

/* ── Result struct ────────────────────────────────────────────── */
typedef struct {
    float temperature;   /* Degrees Celsius */
    float humidity;      /* Relative humidity % */
    bool  valid;         /* true = CRC passed and values in range */
} SHT31_Data_t;

/* ── Status codes ─────────────────────────────────────────────── */
typedef enum {
    SHT31_OK        = 0,
    SHT31_ERR_I2C   = 1,   /* HAL I2C transaction failed    */
    SHT31_ERR_CRC   = 2,   /* CRC mismatch on received data */
    SHT31_ERR_RANGE = 3    /* Value out of valid range      */
} SHT31_Status_t;

/* ── Public API ───────────────────────────────────────────────── */
SHT31_Status_t SHT31_Init(I2C_HandleTypeDef *hi2c);
SHT31_Status_t SHT31_Read(I2C_HandleTypeDef *hi2c, SHT31_Data_t *data);

#endif /* INC_SHT31_H_ */
