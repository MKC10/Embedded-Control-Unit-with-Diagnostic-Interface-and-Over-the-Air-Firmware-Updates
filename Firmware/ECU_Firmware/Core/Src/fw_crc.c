#include "fw_crc.h"

static uint32_t crc_table[256];
static uint8_t  crc_table_ready = 0;

static void Crc_TableInit(void)
{
    for (uint32_t i = 0; i < 256U; i++)
    {
        uint32_t c = i;

        for (uint32_t k = 0; k < 8U; k++)
        {
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }

        crc_table[i] = c;
    }

    crc_table_ready = 1U;
}

uint32_t FW_CRC32(const uint8_t *data, uint32_t len)
{
    if (!crc_table_ready)
    {
        Crc_TableInit();
    }

    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0; i < len; i++)
    {
        crc = crc_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFU;
}

uint32_t FW_CRC32_Flash(uint32_t addr, uint32_t len)
{
    return FW_CRC32((const uint8_t *)addr, len);
}
