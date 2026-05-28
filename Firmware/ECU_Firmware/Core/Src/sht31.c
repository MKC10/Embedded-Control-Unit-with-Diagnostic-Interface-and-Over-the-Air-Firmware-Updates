#include "sht31.h"
#include "i2c.h"

/* ── Private function prototypes ──────────────────────────────── */
static uint8_t SHT31_CRC8(const uint8_t *data, uint16_t len);

/* ─────────────────────────────────────────────────────────────────
 * SHT31_Init
 * Sends a soft reset to the sensor and verifies I2C communication.
 * Call once after HAL_Init and MX_I2C1_Init.
 * ───────────────────────────────────────────────────────────────── */
SHT31_Status_t SHT31_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t cmd[2];

    /* Soft reset command — 0x30A2 split into two bytes MSB first */
    cmd[0] = (SHT31_CMD_SOFT_RST >> 8) & 0xFF;
    cmd[1] =  SHT31_CMD_SOFT_RST       & 0xFF;

    /* Transmit reset command — timeout 100ms */
    if (HAL_I2C_Master_Transmit(hi2c, SHT31_ADDR, cmd, 2, 100) != HAL_OK)
    {
        return SHT31_ERR_I2C;   /* Device not responding at address */
    }

    HAL_Delay(10);  /* Wait for sensor to complete reset — datasheet 1ms min */

    return SHT31_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * SHT31_Read
 * Triggers a measurement, waits for conversion, reads 6 bytes,
 * validates CRC, converts raw counts to engineering units.
 * ───────────────────────────────────────────────────────────────── */
SHT31_Status_t SHT31_Read(I2C_HandleTypeDef *hi2c, SHT31_Data_t *data)
{
    uint8_t cmd[2];
    uint8_t raw[SHT31_RAW_BYTES];
    uint16_t raw_temp;
    uint16_t raw_hum;

    /* ── Step 1: Send measurement trigger command ───────────────── */
    cmd[0] = (SHT31_CMD_MEAS_H >> 8) & 0xFF;   /* 0x2C */
    cmd[1] =  SHT31_CMD_MEAS_H       & 0xFF;   /* 0x06 */

    if (HAL_I2C_Master_Transmit(hi2c, SHT31_ADDR, cmd, 2, 100) != HAL_OK)
    {
        data->valid = false;
        return SHT31_ERR_I2C;
    }

    /* ── Step 2: Wait for sensor to complete measurement ────────── */
    /* High repeatability mode takes up to 15ms — we wait 20ms      */
    HAL_Delay(SHT31_MEAS_DELAY_MS);

    /* ── Step 3: Read 6 bytes from sensor ──────────────────────── */
    /* Byte layout: [T_MSB][T_LSB][T_CRC][RH_MSB][RH_LSB][RH_CRC]  */
    if (HAL_I2C_Master_Receive(hi2c, SHT31_ADDR, raw, SHT31_RAW_BYTES, 100) != HAL_OK)
    {
        data->valid = false;
        return SHT31_ERR_I2C;
    }

    /* ── Step 4: Validate CRC for temperature bytes ─────────────── */
    if (SHT31_CRC8(&raw[0], 2) != raw[2])
    {
        data->valid = false;
        return SHT31_ERR_CRC;   /* Temperature data corrupted */
    }

    /* ── Step 5: Validate CRC for humidity bytes ────────────────── */
    if (SHT31_CRC8(&raw[3], 2) != raw[5])
    {
        data->valid = false;
        return SHT31_ERR_CRC;   /* Humidity data corrupted */
    }

    /* ── Step 6: Reconstruct 16-bit raw values ──────────────────── */
    raw_temp = ((uint16_t)raw[0] << 8) | raw[1];
    raw_hum  = ((uint16_t)raw[3] << 8) | raw[4];

    /* ── Step 7: Convert to engineering units ───────────────────── */
    /* Formulas from SHT31 datasheet Section 4.13                    */
    /* Temperature: T = -45 + 175 * (raw / 65535)                   */
    /* Humidity:   RH = 100 * (raw / 65535)                         */
    data->temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    data->humidity    = 100.0f * ((float)raw_hum  / 65535.0f);

    /* ── Step 8: Range validation ───────────────────────────────── */
    /* SHT31 operating range: -40 to 125C, 0 to 100% RH             */
    if (data->temperature < -40.0f || data->temperature > 125.0f ||
        data->humidity    <   0.0f || data->humidity    > 100.0f)
    {
        data->valid = false;
        return SHT31_ERR_RANGE;
    }

    /* All checks passed */
    data->valid = true;
    return SHT31_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * SHT31_CRC8  (private)
 * CRC-8 checksum per Sensirion application note.
 * Polynomial: 0x31 (x^8 + x^5 + x^4 + 1)
 * Initialization: 0xFF
 * ───────────────────────────────────────────────────────────────── */
static uint8_t SHT31_CRC8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0xFF;   /* Initialize to 0xFF per Sensirion spec  */
    uint8_t i;

    while (len--)
    {
        crc ^= *data++;   /* XOR byte into CRC */

        for (i = 0; i < 8; i++)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;   /* Apply polynomial */
            else
                crc <<= 1;
        }
    }
    return crc;
}
