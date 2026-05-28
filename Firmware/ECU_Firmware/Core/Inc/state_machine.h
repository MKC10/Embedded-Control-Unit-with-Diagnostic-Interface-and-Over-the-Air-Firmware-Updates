#ifndef INC_STATE_MACHINE_H_
#define INC_STATE_MACHINE_H_

#include "stm32f4xx_hal.h"
#include "sht31.h"
#include "bmp280.h"
#include <stdint.h>

/* ── Thresholds ───────────────────────────────────────────────── */
#define TEMP_WARN_HIGH      35.0f    /* Celsius — warning threshold  */
#define TEMP_FAULT_HIGH     40.0f    /* Celsius — fault threshold    */
#define HUM_WARN_HIGH       70.0f    /* % RH — warning threshold     */
#define HUM_FAULT_HIGH      85.0f    /* % RH — fault threshold       */
#define PRESS_WARN_LOW      950.0f   /* hPa — low pressure warning   */
#define PRESS_FAULT_LOW     900.0f   /* hPa — low pressure fault     */

/* Cross-validation threshold                                      */
/* SHT31 and BMP280 both measure temp — should agree within 5C    */
#define TEMP_CROSSVAL_DELTA 5.0f

/* Consecutive failure count before state transition              */
#define FAIL_COUNT_WARN     1        /* 1 failure  → WARNING        */
#define FAIL_COUNT_FAULT    3        /* 3 failures → FAULT          */
#define REINIT_MAX_ATTEMPTS 3        /* Max re-init tries in RECOVERY */

/* ── System states ────────────────────────────────────────────── */
typedef enum {
    STATE_NORMAL   = 0,
    STATE_WARNING  = 1,
    STATE_FAULT    = 2,
    STATE_RECOVERY = 3
} SystemState_t;

/* ── Fault codes ──────────────────────────────────────────────── */
typedef enum {
    FAULT_NONE         = 0x00,
    FAULT_SHT31_CRC    = 0x01,   /* SHT31 CRC failure              */
    FAULT_SHT31_RANGE  = 0x02,   /* SHT31 value out of range       */
    FAULT_BMP280_SPI   = 0x04,   /* BMP280 SPI failure             */
    FAULT_BMP280_RANGE = 0x08,   /* BMP280 value out of range      */
    FAULT_CROSSVAL     = 0x10,   /* Temperature cross-val mismatch */
    FAULT_TEMP_HIGH    = 0x20,   /* Temperature exceeded limit     */
    FAULT_HUM_HIGH     = 0x40,   /* Humidity exceeded limit        */
    FAULT_PRESS_LOW    = 0x80    /* Pressure below limit           */
} FaultCode_t;

/* ── System context ───────────────────────────────────────────── */
typedef struct {
    SystemState_t state;          /* Current system state           */
    FaultCode_t   fault_code;     /* Active fault code              */
    uint8_t       fail_count;     /* Consecutive failure counter    */
    uint8_t       reinit_attempts;/* Recovery re-init attempt count */
    uint32_t      tick;           /* Cycle counter                  */
} SystemContext_t;

/* ── Public API ───────────────────────────────────────────────── */
void SM_Init(SystemContext_t *ctx);
void SM_Update(SystemContext_t *ctx, SHT31_Data_t *sht31, BMP280_Data_t *bmp280,
               I2C_HandleTypeDef *hi2c, SPI_HandleTypeDef *hspi);
const char* SM_GetStateString(SystemState_t state);

#endif /* INC_STATE_MACHINE_H_ */
