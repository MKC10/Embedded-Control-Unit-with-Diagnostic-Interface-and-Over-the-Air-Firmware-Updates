#include "fw_metadata_boot.h"
#include "boot_decision.h"
#include "bootloader.h"
#include "fw_crc.h"
#include "uart_fw_receiver.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define OTA_BUTTON_PORT   GPIOC
#define OTA_BUTTON_PIN    GPIO_PIN_13

/* FIX: exactly 2 real trial boots before rollback -- previously the
 * PENDING->TRIAL promotion boot was uncounted, giving 3 total
 * unconfirmed jumps before failing instead of 2. Now PENDING and
 * TRIAL are handled in one merged block that increments the counter
 * on EVERY jump attempt, including the first. */
#ifndef MAX_TRIAL_ATTEMPTS
#define MAX_TRIAL_ATTEMPTS 2
#endif

extern UART_HandleTypeDef huart2;

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

static void Boot_CheckEntryButton(void)
{
    if (HAL_GPIO_ReadPin(OTA_BUTTON_PORT, OTA_BUTTON_PIN) == GPIO_PIN_RESET)
    {
        g_update_request = 1;
    }
}

void Boot_Decision_Run(void)
{
    fw_meta_t meta;

    Boot_CheckEntryButton();

    FW_Meta_Read(&meta);

    dbg_print("Checking metadata for pending update...\r\n");

    dbg_printf("META: magic=0x%08lX bankB_status=0x%02X bankB_size=%lu bankB_crc32=0x%08lX trial_count=%u\r\n",
               (unsigned long)meta.magic,
               (unsigned)meta.bankB_status,
               (unsigned long)meta.bankB_size,
               (unsigned long)meta.bankB_crc32,
               (unsigned)meta.bankB_trial_count);

    /* ---------------- UART UPDATE MODE ---------------- */
    if (g_update_request)
    {
        g_update_request = 0;
        UART_FW_Receiver_Run();
        return;
    }

    /* ---------------- BANK B: PENDING or TRIAL -- merged,
     * exactly MAX_TRIAL_ATTEMPTS counted jumps before rollback.
     * A fresh PENDING image resets the counter to 0 first, then
     * both paths fall through into the SAME increment-and-check
     * logic below, so the very first jump attempt is counted too. */
    if (meta.bankB_status == BANK_PENDING)
    {
        dbg_print("BANK B: status=PENDING -> starting trial sequence\r\n");
        meta.bankB_status = BANK_TRIAL;
        meta.bankB_trial_count = 0;
        /* falls through to the TRIAL block below -- no jump yet */
    }

    if (meta.bankB_status == BANK_TRIAL)
    {
        meta.bankB_trial_count++;

        dbg_printf("BANK B: TRIAL attempt %u of %u\r\n",
                   (unsigned)meta.bankB_trial_count, (unsigned)MAX_TRIAL_ATTEMPTS);

        if (meta.bankB_trial_count > MAX_TRIAL_ATTEMPTS)
        {
            dbg_print("BANK B: never confirmed within allowed attempts -> FAILED, rolling back to Bank A\r\n");
            meta.bankB_status = BANK_FAILED;
            FW_Meta_Write(&meta);
            goto BOOT_A;
        }

        if (FW_CRC32_Flash(BANK_B_ADDR, meta.bankB_size) == meta.bankB_crc32)
        {
            dbg_print("BANK B: CRC OK -> jumping to Bank B (unconfirmed)\r\n");
            FW_Meta_Write(&meta);
            BL_JumpToApp(BANK_B_ADDR, BANK_B_SIZE);
            while (1);
        }
        else
        {
            dbg_print("BANK B: CRC MISMATCH -> FAILED, rolling back to Bank A\r\n");
            meta.bankB_status = BANK_FAILED;
            FW_Meta_Write(&meta);
            goto BOOT_A;
        }
    }

    /* ---------------- BANK B: CONFIRMED -- final, permanent ---------------- */
    if (meta.bankB_status == BANK_CONFIRMED)
    {
        dbg_print("BANK B: status=CONFIRMED (final) -> validating CRC...\r\n");

        if (FW_CRC32_Flash(BANK_B_ADDR, meta.bankB_size) == meta.bankB_crc32)
        {
            dbg_print("BANK B: CRC OK -> jumping to Bank B\r\n");
            BL_JumpToApp(BANK_B_ADDR, BANK_B_SIZE);
            while (1);
        }
        else
        {
            dbg_print("BANK B: CONFIRMED image now fails CRC (corruption?) -> FAILED\r\n");
            meta.bankB_status = BANK_FAILED;
            FW_Meta_Write(&meta);
        }
    }

BOOT_A:

    dbg_print("No valid update -- running Bank A.\r\n");

    dbg_printf("BOOT_A: status was 0x%02X -> validating Bank A\r\n",
               (unsigned)meta.bankB_status);

    if (FW_CRC32_Flash(BANK_A_ADDR, meta.bankA_size) == meta.bankA_crc32)
    {
        dbg_print("BOOT_A: CRC OK -> jumping to Bank A\r\n");
        BL_JumpToApp(BANK_A_ADDR, BANK_A_SIZE);
        while (1);
    }
    else
    {
        dbg_print("BOOT_A: CRC FAIL -> entering recovery mode\r\n");
    }

    /* ---------------- RECOVERY MODE ---------------- */
    UART_FW_Receiver_Run();

    while (1);
}
