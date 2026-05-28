#ifndef INC_DIAGNOSTIC_H_
#define INC_DIAGNOSTIC_H_

#include "stm32f4xx_hal.h"
#include "state_machine.h"
#include "sht31.h"
#include "bmp280.h"
#include <stdint.h>

/* ── UART buffer sizes ────────────────────────────────────────── */
#define DIAG_TX_BUF_SIZE    128
#define DIAG_RX_BUF_SIZE    32

/* ── Command definitions ──────────────────────────────────────── */
#define DIAG_CMD_STATUS     "STATUS"    /* Print full system status  */
#define DIAG_CMD_SENSORS    "SENSORS"   /* Print raw sensor values   */
#define DIAG_CMD_FAULT      "FAULT"     /* Print active fault code   */
#define DIAG_CMD_RESET      "RESET"     /* Trigger system reset      */
#define DIAG_CMD_HELP       "HELP"      /* Print available commands  */

/* ── Public API ───────────────────────────────────────────────── */
void DIAG_Init(UART_HandleTypeDef *huart);
void DIAG_Process(UART_HandleTypeDef *huart, SystemContext_t *ctx,
                  SHT31_Data_t *sht31, BMP280_Data_t *bmp280);
void DIAG_Print(UART_HandleTypeDef *huart, const char *msg);
void DIAG_RxCallback(UART_HandleTypeDef *huart);

#endif /* INC_DIAGNOSTIC_H_ */
