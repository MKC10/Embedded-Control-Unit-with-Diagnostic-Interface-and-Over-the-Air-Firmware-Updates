#include "bmp280.h"
#include "spi.h"

/* ── Private variables ────────────────────────────────────────── */
static BMP280_Calib_t calib;   /* Trim coefficients loaded at init  */

/* ── Private function prototypes ──────────────────────────────── */
static BMP280_Status_t BMP280_ReadReg(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t *data, uint16_t len);
static BMP280_Status_t BMP280_WriteReg(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t value);
static void            BMP280_ReadCalib(SPI_HandleTypeDef *hspi);
static float           BMP280_CompensateTemp(int32_t adc_T, int32_t *t_fine);
static float           BMP280_CompensatePress(int32_t adc_P, int32_t t_fine);

/* ─────────────────────────────────────────────────────────────────
 * BMP280_ReadReg  (private)
 * Reads len bytes from register reg over SPI.
 * SPI read: set bit7 of register address, CS low, tx addr, rx data, CS high.
 * ───────────────────────────────────────────────────────────────── */
static BMP280_Status_t BMP280_ReadReg(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t *data, uint16_t len)
{
    uint8_t addr = reg | 0x80;   /* Set bit7 high = read operation   */

    BMP280_CS_LOW();

    /* Transmit register address */
    if (HAL_SPI_Transmit(hspi, &addr, 1, 100) != HAL_OK)
    {
        BMP280_CS_HIGH();
        return BMP280_ERR_SPI;
    }

    /* Receive data bytes */
    if (HAL_SPI_Receive(hspi, data, len, 100) != HAL_OK)
    {
        BMP280_CS_HIGH();
        return BMP280_ERR_SPI;
    }

    BMP280_CS_HIGH();
    return BMP280_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * BMP280_WriteReg  (private)
 * Writes single byte value to register reg over SPI.
 * SPI write: clear bit7 of register address.
 * ───────────────────────────────────────────────────────────────── */
static BMP280_Status_t BMP280_WriteReg(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t value)
{
    uint8_t buf[2];
    buf[0] = reg & 0x7F;   /* Clear bit7 = write operation          */
    buf[1] = value;

    BMP280_CS_LOW();

    if (HAL_SPI_Transmit(hspi, buf, 2, 100) != HAL_OK)
    {
        BMP280_CS_HIGH();
        return BMP280_ERR_SPI;
    }

    BMP280_CS_HIGH();
    return BMP280_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * BMP280_ReadCalib  (private)
 * Reads 24 bytes of factory trim data from 0x88-0x9F.
 * These coefficients are unique per chip and must be read once
 * at startup — used in compensation formulas for every reading.
 * ───────────────────────────────────────────────────────────────── */
static void BMP280_ReadCalib(SPI_HandleTypeDef *hspi)
{
    uint8_t buf[24];

    BMP280_ReadReg(hspi, BMP280_REG_CALIB_00, buf, 24);

    /* Reconstruct signed/unsigned 16-bit trim values from byte pairs */
    calib.dig_T1 = (uint16_t)(buf[1]  << 8) | buf[0];
    calib.dig_T2 = (int16_t) (buf[3]  << 8) | buf[2];
    calib.dig_T3 = (int16_t) (buf[5]  << 8) | buf[4];
    calib.dig_P1 = (uint16_t)(buf[7]  << 8) | buf[6];
    calib.dig_P2 = (int16_t) (buf[9]  << 8) | buf[8];
    calib.dig_P3 = (int16_t) (buf[11] << 8) | buf[10];
    calib.dig_P4 = (int16_t) (buf[13] << 8) | buf[12];
    calib.dig_P5 = (int16_t) (buf[15] << 8) | buf[14];
    calib.dig_P6 = (int16_t) (buf[17] << 8) | buf[16];
    calib.dig_P7 = (int16_t) (buf[19] << 8) | buf[18];
    calib.dig_P8 = (int16_t) (buf[21] << 8) | buf[20];
    calib.dig_P9 = (int16_t) (buf[23] << 8) | buf[22];
}

/* ─────────────────────────────────────────────────────────────────
 * BMP280_CompensateTemp  (private)
 * Converts raw 20-bit ADC temperature to Celsius.
 * Formula directly from BMP280 datasheet Section 4.2.3.
 * Also computes t_fine — shared intermediate used by pressure.
 * ───────────────────────────────────────────────────────────────── */
static float BMP280_CompensateTemp(int32_t adc_T, int32_t *t_fine)
{
    int32_t var1, var2;

    var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) *
             ((int32_t)calib.dig_T2)) >> 11;

    var2 = (((((adc_T >> 4) - ((int32_t)calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calib.dig_T1))) >> 12) *
             ((int32_t)calib.dig_T3)) >> 14;

    *t_fine = var1 + var2;

    return (float)((*t_fine * 5 + 128) >> 8) / 100.0f;
}

/* ─────────────────────────────────────────────────────────────────
 * BMP280_CompensatePress  (private)
 * Converts raw 20-bit ADC pressure to hPa.
 * Formula directly from BMP280 datasheet Section 4.2.3.
 * Requires t_fine from temperature compensation first.
 * ───────────────────────────────────────────────────────────────── */
static float BMP280_CompensatePress(int32_t adc_P, int32_t t_fine)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) +
           ((var1 * (int64_t)calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) *
            ((int64_t)calib.dig_P1) >> 33;

    if (var1 == 0) return 0.0f;   /* Avoid division by zero          */

    p    = 1048576 - adc_P;
    p    = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.dig_P8) * p) >> 19;
    p    = ((p + var1 + var2) >> 8) + (((int64_t)calib.dig_P7) << 4);

    return (float)p / 25600.0f;   /* Convert to hPa                  */
}

/* ─────────────────────────────────────────────────────────────────
 * BMP280_Init
 * Verifies chip ID, soft resets, reads calibration, configures
 * oversampling and IIR filter, starts normal mode measurement.
 * ───────────────────────────────────────────────────────────────── */
BMP280_Status_t BMP280_Init(SPI_HandleTypeDef *hspi)
{
    uint8_t chip_id = 0;

    /* ── Step 1: Verify chip ID ─────────────────────────────────── */
    BMP280_ReadReg(hspi, BMP280_REG_ID, &chip_id, 1);
    if (chip_id != BMP280_CHIP_ID)
    {
        return BMP280_ERR_ID;   /* Wrong device or wiring issue      */
    }

    /* ── Step 2: Soft reset ─────────────────────────────────────── */
    BMP280_WriteReg(hspi, BMP280_REG_RESET, BMP280_SOFT_RESET);
    HAL_Delay(10);             /* Wait for reset to complete         */

    /* ── Step 3: Read factory calibration trim coefficients ─────── */
    BMP280_ReadCalib(hspi);

    /* ── Step 4: Configure IIR filter and standby time ──────────── */
    BMP280_WriteReg(hspi, BMP280_REG_CONFIG, BMP280_CONFIG_VAL);

    /* ── Step 5: Set oversampling and start normal mode ─────────── */
    /* This must be last — writing mode bits starts measurement      */
    BMP280_WriteReg(hspi, BMP280_REG_CTRL_MEAS, BMP280_CTRL_MEAS_VAL);

    HAL_Delay(10);

    return BMP280_OK;
}

/* ─────────────────────────────────────────────────────────────────
 * BMP280_Read
 * Burst reads 6 raw ADC bytes from 0xF7-0xFC, reconstructs
 * 20-bit pressure and temperature, applies compensation formulas.
 * ───────────────────────────────────────────────────────────────── */
BMP280_Status_t BMP280_Read(SPI_HandleTypeDef *hspi, BMP280_Data_t *data)
{
    uint8_t  raw[6];
    int32_t  adc_P, adc_T;
    int32_t  t_fine;

    /* ── Step 1: Burst read 6 bytes starting at 0xF7 ───────────── */
    /* Layout: [P_MSB][P_LSB][P_XLSB][T_MSB][T_LSB][T_XLSB]        */
    if (BMP280_ReadReg(hspi, BMP280_REG_PRESS_MSB, raw, 6) != BMP280_OK)
    {
        data->valid = false;
        return BMP280_ERR_SPI;
    }

    /* ── Step 2: Reconstruct 20-bit raw ADC values ──────────────── */
    /* Bits 19:12 in MSB, 11:4 in LSB, 3:0 in XLSB bits 7:4        */
    adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

    /* ── Step 3: Apply compensation formulas ───────────────────── */
    /* Temperature must be computed first — produces t_fine for pressure */
    data->temperature = BMP280_CompensateTemp(adc_T, &t_fine);
    data->pressure    = BMP280_CompensatePress(adc_P, t_fine);

    /* ── Step 4: Range validation ───────────────────────────────── */
    /* BMP280 operating range: -40 to 85C, 300 to 1100 hPa          */
    if (data->temperature < -40.0f || data->temperature > 85.0f  ||
        data->pressure    < 300.0f || data->pressure    > 1100.0f)
    {
        data->valid = false;
        return BMP280_ERR_RANGE;
    }

    data->valid = true;
    return BMP280_OK;
}
