/*
 * fw_flash.c  -- ECU_BOOTLOADER project
 */

#include "fw_flash_boot.h"
#include <string.h>

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
        uint32_t word  = 0xFFFFFFFFU;
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
