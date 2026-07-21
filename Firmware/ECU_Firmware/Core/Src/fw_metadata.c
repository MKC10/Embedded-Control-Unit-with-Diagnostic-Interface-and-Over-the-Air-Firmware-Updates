#include "fw_metadata.h"
#include "fw_crc.h"
#include <string.h>

/* --------------------------------------------------------------------
 * FW_Flash_EraseRegion / FW_Flash_Write merged directly into this file.
 *
 * fw_flash.c compiled cleanly every single time (confirmed across
 * multiple full clean rebuilds, including a from-scratch Bank_B
 * folder regeneration) but its symbols never made it into the final
 * link for this project -- a CubeIDE project-configuration quirk
 * (almost certainly a per-file build-exclusion setting hidden in
 * .cproject) that survived every standard fix: Clean, deleting the
 * Bank_B build folder entirely, and an Index rebuild.
 *
 * Merging the functions directly into fw_metadata.c sidesteps the
 * issue entirely -- fw_metadata.o is proven to link correctly (no
 * other symbol from it has ever failed to resolve), so anything
 * living in this object file is guaranteed to actually make it into
 * the final binary.
 * -------------------------------------------------------------------- */

typedef struct {
    uint32_t sector;
    uint32_t addr;
    uint32_t size;
} flash_region_t;

static const flash_region_t flash_sectors[8] = {
    { FLASH_SECTOR_0, 0x08000000U,  16U * 1024U },
    { FLASH_SECTOR_1, 0x08004000U,  16U * 1024U },
    { FLASH_SECTOR_2, 0x08008000U,  16U * 1024U },
    { FLASH_SECTOR_3, 0x0800C000U,  16U * 1024U },
    { FLASH_SECTOR_4, 0x08010000U,  64U * 1024U },
    { FLASH_SECTOR_5, 0x08020000U, 128U * 1024U },
    { FLASH_SECTOR_6, 0x08040000U, 128U * 1024U },
    { FLASH_SECTOR_7, 0x08060000U, 128U * 1024U },
};

HAL_StatusTypeDef FW_Flash_EraseRegion(uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;
    HAL_StatusTypeDef status = HAL_OK;

    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < 8U; i++)
    {
        uint32_t sec_start = flash_sectors[i].addr;
        uint32_t sec_end   = sec_start + flash_sectors[i].size;

        if ((sec_start < end) && (sec_end > addr))
        {
            FLASH_EraseInitTypeDef erase = {0};
            uint32_t sector_error = 0;

            erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
            erase.Sector       = flash_sectors[i].sector;
            erase.NbSectors    = 1U;
            erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

            if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
            {
                status = HAL_ERROR;
                break;
            }
        }
    }

    HAL_FLASH_Lock();
    return status;
}

HAL_StatusTypeDef FW_Flash_Write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t offset = 0;

    HAL_FLASH_Unlock();

    while (offset < len)
    {
        uint32_t word = 0xFFFFFFFFU;

        uint32_t chunk = (len - offset >= 4U) ? 4U : (len - offset);

        memcpy(&word, &data[offset], chunk);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + offset, word) != HAL_OK)
        {
            status = HAL_ERROR;
            break;
        }

        if (*(uint32_t *)(addr + offset) != word)
        {
            status = HAL_ERROR;
            break;
        }

        offset += 4U;
    }

    HAL_FLASH_Lock();
    return status;
}

/* --------------------------------------------------------------------
 * Original fw_metadata.c content below, unchanged.
 * -------------------------------------------------------------------- */

static uint32_t Meta_SelfCRC(const fw_meta_t *meta)
{
    return FW_CRC32((const uint8_t *)meta, sizeof(fw_meta_t) - sizeof(uint32_t));
}

void FW_Meta_Read(fw_meta_t *meta)
{
    memcpy(meta, (const void *)META_ADDR, sizeof(fw_meta_t));

    if ((meta->magic != FW_META_MAGIC) || (meta->self_crc != Meta_SelfCRC(meta)))
    {
        memset(meta, 0, sizeof(fw_meta_t));

        meta->magic        = FW_META_MAGIC;
        meta->bankA_size   = BANK_A_SIZE;
        meta->bankA_crc32  = FW_CRC32_Flash(BANK_A_ADDR, BANK_A_SIZE);
        meta->bankB_status = BANK_EMPTY;

        FW_Meta_Write(meta);
    }
}

void FW_Meta_Write(fw_meta_t *meta)
{
    meta->self_crc = Meta_SelfCRC(meta);

    FW_Flash_EraseRegion(META_ADDR, sizeof(fw_meta_t));
    FW_Flash_Write(META_ADDR, (const uint8_t *)meta, sizeof(fw_meta_t));
}
