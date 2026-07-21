/*
 * fw_metadata_boot.c  -- ECU_BOOTLOADER project
 *
 * FIX: this file was missing entirely -- the previous version
 * uploaded under this filename actually contained the HEADER's
 * content (declarations only, no function bodies), so FW_Meta_Read()
 * and FW_Meta_Write() were declared but never implemented anywhere,
 * causing undefined-reference link errors for both functions.
 *
 * Always populates the caller's *meta struct on every path (blank-
 * flash self-provision AND normal valid-magic reload) -- the fix
 * for the original bug where a normal boot left the caller's struct
 * as uninitialized stack garbage.
 */

#include "fw_metadata_boot.h"
#include "fw_flash_boot.h"
#include "fw_crc.h"
#include <string.h>

static fw_meta_t g_meta;

void FW_Meta_Read(fw_meta_t *meta)
{
    const fw_meta_t *flash = (const fw_meta_t *)META_ADDR;

    if (flash->magic != FW_META_MAGIC)
    {
        memset(&g_meta, 0, sizeof(fw_meta_t));

        g_meta.magic        = FW_META_MAGIC;
        g_meta.bankA_size   = BANK_A_SIZE;
        g_meta.bankA_crc32  = FW_CRC32_Flash(BANK_A_ADDR, BANK_A_SIZE);
        g_meta.bankB_status = BANK_EMPTY;
        g_meta.bankB_size   = 0;
        g_meta.bankB_crc32  = 0;

        FW_Meta_Write(&g_meta);
    }
    else
    {
        memcpy(&g_meta, flash, sizeof(fw_meta_t));

        uint32_t calc_crc = FW_CRC32((uint8_t*)&g_meta, sizeof(fw_meta_t) - 4);

        if (g_meta.self_crc != calc_crc)
        {
            g_meta.bankB_status = BANK_FAILED;
        }
    }

    memcpy(meta, &g_meta, sizeof(fw_meta_t));
}

void FW_Meta_Write(fw_meta_t *meta)
{
    meta->self_crc = FW_CRC32((uint8_t*)meta, sizeof(fw_meta_t) - 4);

    FW_Flash_EraseRegion(META_ADDR, 16U * 1024U);
    FW_Flash_Write(META_ADDR, (uint8_t*)meta, sizeof(fw_meta_t));
}
