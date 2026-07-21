/* fw_update_confirm.c
 *
 * APPLICATION-SIDE code (NOT bootloader). Link this into the Bank B
 * application build, together with fw_metadata.c, fw_flash.c and
 * fw_crc.c (the app needs write access to the same metadata sector
 * the bootloader reads).
 *
 * Purpose: close the trial/confirm loop. The bootloader promotes a
 * PENDING image to TRIAL and jumps into it. If the app runs correctly,
 * it must prove that by calling FW_Update_ConfirmBoot() once it has
 * reached a known-good state -- peripherals up, main loop about to
 * start. That flips TRIAL -> CONFIRMED, and all future boots take the
 * fast CONFIRMED path with no trial counting.
 *
 * If the app never calls this (crash, hang caught by IWDG, or broken
 * logic), the bootloader keeps seeing TRIAL on each reset, exhausts
 * MAX_TRIAL_ATTEMPTS, marks the bank FAILED, and falls back to Bank A.
 * That is the rollback guarantee: CRC proves the image is intact,
 * confirm proves it actually RUNS.
 *
 * Deliberately does NOT touch any trial-count field -- the bootloader
 * and firmware modules' fw_metadata.h headers name that reserved byte
 * differently (bankB_trial_count vs. part of reserved[3]), and once
 * status is CONFIRMED the bootloader never reads that byte again
 * anyway. Leaving it untouched keeps this file compatible with either
 * header without depending on a specific field name existing.
 */

#include <fw_metadata_boot.h>
#include "fw_update_confirm.h"

fw_confirm_result_t FW_Update_ConfirmBoot(void)
{
    fw_meta_t meta;

    FW_Meta_Read(&meta);

    if (meta.bankB_status == BANK_CONFIRMED)
    {
        return FW_CONFIRM_ALREADY;
    }

    if (meta.bankB_status != BANK_TRIAL)
    {
        return FW_CONFIRM_NOT_TRIAL;
    }

    meta.bankB_status = BANK_CONFIRMED;

    FW_Meta_Write(&meta);

    return FW_CONFIRM_OK;
}
