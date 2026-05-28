#include "diagnostic.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

/* ── Private variables ────────────────────────────────────────── */
/* ── Private variables ────────────────────────────────────────── */
static uint8_t rx_byte;
static uint8_t rx_buf[DIAG_RX_BUF_SIZE];
static volatile uint8_t rx_index = 0;
static volatile uint8_t cmd_ready = 0;
static volatile uint8_t ignore_next_lf = 0;
/* ─────────────────────────────────────────────────────────────────
 * DIAG_Init
 * Starts UART receive interrupt for single byte reception.
 * Call once after MX_USART2_UART_Init.
 * ───────────────────────────────────────────────────────────────── */
void DIAG_Init(UART_HandleTypeDef *huart)
{
    /* Clear buffers */
    memset(rx_buf, 0, DIAG_RX_BUF_SIZE);
    rx_index = 0;
    cmd_ready = 0;

    /* Start interrupt-driven single byte reception */
    HAL_UART_Receive_IT(huart, &rx_byte, 1);

    /* Print welcome banner */
    DIAG_Print(huart, "\r\n== ECU Diagnostic Interface ==\r\n");
    DIAG_Print(huart, "Type HELP for available commands\r\n");
    DIAG_Print(huart, "> ");
}

/* ─────────────────────────────────────────────────────────────────
 * DIAG_Print
 * Transmits null-terminated string over UART.
 * Blocking with 100ms timeout — safe for diagnostic use.
 * ───────────────────────────────────────────────────────────────── */
void DIAG_Print(UART_HandleTypeDef *huart, const char *msg)
{
    HAL_UART_Transmit(huart, (uint8_t *)msg, strlen(msg), 100);
}

/* ─────────────────────────────────────────────────────────────────
 * DIAG_RxCallback
 * Called from HAL_UART_RxCpltCallback when a byte is received.
 * Assembles bytes into command buffer until newline received.
 * ───────────────────────────────────────────────────────────────── */
void DIAG_RxCallback(UART_HandleTypeDef *huart)
{
    /* Ignore LF if it immediately follows CR */
    if (ignore_next_lf && rx_byte == '\n')
    {
        ignore_next_lf = 0;
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
        return;
    }

    ignore_next_lf = 0;

    /* Echo received character */
    HAL_UART_Transmit(huart, &rx_byte, 1, 10);

    if (rx_byte == '\r' || rx_byte == '\n')
    {
        if (rx_byte == '\r')
        {
            ignore_next_lf = 1;
        }

        if (rx_index > 0)
        {
            rx_buf[rx_index] = '\0';
            cmd_ready = 1;
            rx_index = 0;
        }
    }
    else if ((rx_byte == 0x08 || rx_byte == 0x7F) && rx_index > 0)
    {
        rx_index--;
    }
    else if (!cmd_ready && rx_index < DIAG_RX_BUF_SIZE - 1)
    {
        rx_buf[rx_index++] = rx_byte;
    }

    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}

/* ─────────────────────────────────────────────────────────────────
 * DIAG_Process
 * Checks if a complete command is ready and executes it.
 * Call from main loop — not from ISR context.
 * ───────────────────────────────────────────────────────────────── */
void DIAG_Process(UART_HandleTypeDef *huart, SystemContext_t *ctx,
                  SHT31_Data_t *sht31, BMP280_Data_t *bmp280)
{
    char tx_buf[DIAG_TX_BUF_SIZE];

    if (!cmd_ready) return;   /* No complete command yet */

    cmd_ready = 0;            /* Clear flag immediately  */
    if (!cmd_ready) return;

    cmd_ready = 0;
    rx_buf[DIAG_RX_BUF_SIZE - 1] = '\0';

    snprintf(tx_buf, sizeof(tx_buf), "\r\nCMD RECEIVED: [%s]\r\n", rx_buf);
    DIAG_Print(huart, tx_buf);


    DIAG_Print(huart, "\r\n");

    /* ── STATUS command ─────────────────────────────────────────── */
    if (strcmp((char *)rx_buf, DIAG_CMD_STATUS) == 0)
    {
        snprintf(tx_buf, sizeof(tx_buf),
                 "State    : %s\r\n"
                 "Fault    : 0x%02X\r\n"
                 "Failures : %d\r\n"
                 "Tick     : %lu\r\n",
                 SM_GetStateString(ctx->state),
                 ctx->fault_code,
                 ctx->fail_count,
                 ctx->tick);
        DIAG_Print(huart, tx_buf);
    }

    /* ── SENSORS command ────────────────────────────────────────── */
    else if (strcmp((char *)rx_buf, DIAG_CMD_SENSORS) == 0)
    {
        int sht_temp_i = (int)(sht31->temperature * 100);
        int sht_hum_i  = (int)(sht31->humidity * 100);
        int bmp_temp_i = (int)(bmp280->temperature * 100);
        int bmp_pres_i = (int)(bmp280->pressure * 100);

        snprintf(tx_buf, sizeof(tx_buf),
                 "SHT31  Temp : %d.%02d C\r\n"
                 "SHT31  Hum  : %d.%02d %%\r\n"
                 "SHT31  Valid: %s\r\n"
                 "BMP280 Temp : %d.%02d C\r\n"
                 "BMP280 Press: %d.%02d hPa\r\n"
                 "BMP280 Valid: %s\r\n",
                 sht_temp_i / 100, abs(sht_temp_i % 100),
                 sht_hum_i / 100, abs(sht_hum_i % 100),
                 sht31->valid ? "YES" : "NO",
                 bmp_temp_i / 100, abs(bmp_temp_i % 100),
                 bmp_pres_i / 100, abs(bmp_pres_i % 100),
                 bmp280->valid ? "YES" : "NO");

        DIAG_Print(huart, tx_buf);
    }

    /* ── FAULT command ──────────────────────────────────────────── */
    else if (strcmp((char *)rx_buf, DIAG_CMD_FAULT) == 0)
    {
        if (ctx->fault_code == FAULT_NONE)
        {
            DIAG_Print(huart, "No active faults\r\n");
        }
        else
        {
            snprintf(tx_buf, sizeof(tx_buf), "Fault code: 0x%02X\r\n", ctx->fault_code);
            DIAG_Print(huart, tx_buf);

            /* Decode fault bits */
            if (ctx->fault_code & FAULT_SHT31_CRC)    DIAG_Print(huart, "  [SHT31 CRC failure]\r\n");
            if (ctx->fault_code & FAULT_SHT31_RANGE)  DIAG_Print(huart, "  [SHT31 out of range]\r\n");
            if (ctx->fault_code & FAULT_BMP280_SPI)   DIAG_Print(huart, "  [BMP280 SPI failure]\r\n");
            if (ctx->fault_code & FAULT_BMP280_RANGE) DIAG_Print(huart, "  [BMP280 out of range]\r\n");
            if (ctx->fault_code & FAULT_CROSSVAL)     DIAG_Print(huart, "  [Temp cross-validation fail]\r\n");
            if (ctx->fault_code & FAULT_TEMP_HIGH)    DIAG_Print(huart, "  [Temperature too high]\r\n");
            if (ctx->fault_code & FAULT_HUM_HIGH)     DIAG_Print(huart, "  [Humidity too high]\r\n");
            if (ctx->fault_code & FAULT_PRESS_LOW)    DIAG_Print(huart, "  [Pressure too low]\r\n");
        }
    }

    /* ── RESET command ──────────────────────────────────────────── */
    else if (strcmp((char *)rx_buf, DIAG_CMD_RESET) == 0)
    {
        DIAG_Print(huart, "Resetting system...\r\n");
        HAL_Delay(100);
        NVIC_SystemReset();
    }

    /* ── HELP command ───────────────────────────────────────────── */
    else if (strcmp((char *)rx_buf, DIAG_CMD_HELP) == 0)
    {
        DIAG_Print(huart, "Available commands:\r\n");
        DIAG_Print(huart, "  STATUS  — system state, fault code, tick\r\n");
        DIAG_Print(huart, "  SENSORS — live sensor readings\r\n");
        DIAG_Print(huart, "  FAULT   — active fault details\r\n");
        DIAG_Print(huart, "  RESET   — software system reset\r\n");
        DIAG_Print(huart, "  HELP    — this message\r\n");
    }

    /* ── Unknown command ────────────────────────────────────────── */
    else if (strlen((char *)rx_buf) > 0)
    {
        snprintf(tx_buf, sizeof(tx_buf), "Unknown command: %s\r\n", rx_buf);
        DIAG_Print(huart, tx_buf);
    }

    DIAG_Print(huart, "> ");   /* Print prompt for next command */
}
