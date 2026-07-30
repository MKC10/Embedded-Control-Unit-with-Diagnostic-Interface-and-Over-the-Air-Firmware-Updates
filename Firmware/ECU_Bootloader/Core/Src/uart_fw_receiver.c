#include "fw_metadata_boot.h"
#include "uart_fw_receiver.h"
#include "fw_flash_boot.h"
#include "fw_crc.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define MAX_PAYLOAD_SIZE 256U

#define OTA_ACK 0x06U
#define OTA_NAK 0x15U

#define FIELD_RX_TIMEOUT_MS 60000U

extern UART_HandleTypeDef huart1;   // OTA protocol -> ESP32
extern UART_HandleTypeDef huart2;   // debug text -> PuTTY

/* ---------------- GLOBAL STATE ---------------- */
volatile uint8_t g_update_request = 0;

static uint8_t  rx_byte;
static uint8_t  session_active = 0;

static uint32_t write_addr = BANK_B_ADDR;
static uint32_t running_crc = 0xFFFFFFFF;

static uint32_t total_size = 0;

/* ---------------- BUFFER ---------------- */
static uint8_t payload[MAX_PAYLOAD_SIZE];

/* Debug prints go to huart2 (PuTTY), NOT the protocol UART */
static void dbg_print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

static void dbg_printf(const char *fmt, ...)
{
    char buf[96];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0)
    {
        HAL_UART_Transmit(&huart2, (uint8_t*)buf, (uint16_t)len, HAL_MAX_DELAY);
    }
}

static void Uart_ClearErrors(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
    __HAL_UART_CLEAR_PEFLAG(&huart1);
    __HAL_UART_FLUSH_DRREGISTER(&huart1);
}

void UART_FW_Receiver_Init(void)
{
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (rx_byte == 0xAA)
        {
            g_update_request = 1;
        }

        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}

static void Ota_SendAck(void)
{
    uint8_t b = OTA_ACK;
    HAL_UART_Transmit(&huart1, &b, 1, 100);
}

static void Ota_SendNak(void)
{
    uint8_t b = OTA_NAK;
    HAL_UART_Transmit(&huart1, &b, 1, 100);
}

void UART_FW_Receiver_Run(void)
{
    {
        const char *banner = "OTA RX READY\r\n";
        HAL_UART_Transmit(&huart2, (uint8_t*)banner, strlen(banner), HAL_MAX_DELAY);
        // NOTE: banner now prints on huart2 (PuTTY-visible), not huart1
        // (the OTA protocol line to the ESP32 doesn't need a text banner)
    }

    HAL_UART_AbortReceive_IT(&huart1);
    Uart_ClearErrors();

    uint8_t cmd;
    uint16_t len;

    session_active = 1;

    while (session_active)
    {
        Uart_ClearErrors();

        if (HAL_UART_Receive(&huart1, &cmd, 1, HAL_MAX_DELAY) != HAL_OK)
        {
            continue;
        }

        if (cmd == 0x01)
        {
            uint8_t start_hdr[6];

            if (HAL_UART_Receive(&huart1, start_hdr, 6, FIELD_RX_TIMEOUT_MS) != HAL_OK)
            {
                Ota_SendNak();
                Uart_ClearErrors();
                continue;
            }

            memcpy(&total_size, &start_hdr[0], 4);

            if (total_size > BANK_B_SIZE)
            {
                Ota_SendNak();
                session_active = 0;
                break;
            }

            write_addr = BANK_B_ADDR;
            running_crc = 0xFFFFFFFF;

            FW_Flash_EraseRegion(BANK_B_ADDR, BANK_B_SIZE);

            Uart_ClearErrors();
            Ota_SendAck();

            dbg_printf("OTA: START received, size=%lu\r\n", (unsigned long)total_size);
        }

        else if (cmd == 0x02)
        {
            if (HAL_UART_Receive(&huart1, (uint8_t*)&len, 2, FIELD_RX_TIMEOUT_MS) != HAL_OK)
            {
                Ota_SendNak();
                Uart_ClearErrors();
                continue;
            }

            if (len == 0 || len > MAX_PAYLOAD_SIZE)
            {
                Ota_SendNak();
                session_active = 0;
                break;
            }

            if (HAL_UART_Receive(&huart1, payload, len, FIELD_RX_TIMEOUT_MS) != HAL_OK)
            {
                Ota_SendNak();
                Uart_ClearErrors();
                continue;
            }

            running_crc = FW_CRC32(payload, len);

            if (FW_Flash_Write(write_addr, payload, len) != HAL_OK)
            {
                Ota_SendNak();
                continue;
            }

            write_addr += len;

            Ota_SendAck();
        }

        else if (cmd == 0x03)
        {
            uint32_t expected_crc;

            if (HAL_UART_Receive(&huart1, (uint8_t*)&expected_crc, 4, FIELD_RX_TIMEOUT_MS) != HAL_OK)
            {
                Ota_SendNak();
                continue;
            }

            uint32_t final_crc = FW_CRC32_Flash(BANK_B_ADDR, total_size);

            if (final_crc == expected_crc)
            {
                fw_meta_t meta;
                FW_Meta_Read(&meta);

                meta.bankB_size   = total_size;
                meta.bankB_crc32  = expected_crc;
                meta.bankB_status = BANK_PENDING;
                meta.bankB_version = meta.bankB_version + 1;

                FW_Meta_Write(&meta);

                dbg_printf("FIRMWARE STORED IN BANK B: size=%lu crc32=0x%08lX status=PENDING (0x%02X)\r\n",
                           (unsigned long)meta.bankB_size,
                           (unsigned long)meta.bankB_crc32,
                           (unsigned)meta.bankB_status);

                Ota_SendAck();
            }
            else
            {
                fw_meta_t meta;
                FW_Meta_Read(&meta);

                meta.bankB_status = BANK_FAILED;
                FW_Meta_Write(&meta);

                dbg_printf("END: CRC MISMATCH (final=0x%08lX expected=0x%08lX) -> BANK B MARKED FAILED\r\n",
                           (unsigned long)final_crc, (unsigned long)expected_crc);

                Ota_SendNak();
            }
        }

        else if (cmd == 0x04)
        {
            fw_meta_t meta;
            FW_Meta_Read(&meta);

            HAL_UART_Transmit(&huart1, (uint8_t*)&meta, sizeof(meta), HAL_MAX_DELAY);
        }
    }
}
