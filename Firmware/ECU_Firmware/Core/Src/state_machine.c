#include "state_machine.h"
#include <math.h>

/* ── Private function prototypes ──────────────────────────────── */
static FaultCode_t SM_CheckSensors(SHT31_Data_t *sht31, BMP280_Data_t *bmp280,
                                    SHT31_Status_t s_status, BMP280_Status_t b_status);
static void SM_EnterFault(SystemContext_t *ctx, FaultCode_t fault);
static void SM_EnterWarning(SystemContext_t *ctx, FaultCode_t fault);
static void SM_EnterNormal(SystemContext_t *ctx);
static void SM_DoRecovery(SystemContext_t *ctx, I2C_HandleTypeDef *hi2c,
                           SPI_HandleTypeDef *hspi);

/* ─────────────────────────────────────────────────────────────────
 * SM_Init
 * Initializes system context to clean NORMAL state.
 * Call once after all peripherals and sensors are initialized.
 * ───────────────────────────────────────────────────────────────── */
void SM_Init(SystemContext_t *ctx)
{
    ctx->state           = STATE_NORMAL;
    ctx->fault_code      = FAULT_NONE;
    ctx->fail_count      = 0;
    ctx->reinit_attempts = 0;
    ctx->tick            = 0;
}

/* ─────────────────────────────────────────────────────────────────
 * SM_Update
 * Main state machine tick — call every 100ms from TIM2 ISR context
 * or from main loop. Reads sensor data, evaluates transitions,
 * executes state actions.
 * ───────────────────────────────────────────────────────────────── */
void SM_Update(SystemContext_t *ctx, SHT31_Data_t *sht31, BMP280_Data_t *bmp280,
               I2C_HandleTypeDef *hi2c, SPI_HandleTypeDef *hspi)
{
    SHT31_Status_t  s_status;
    BMP280_Status_t b_status;
    FaultCode_t     detected_fault;

    ctx->tick++;

    switch (ctx->state)
    {
        /* ── NORMAL ─────────────────────────────────────────────── */
        case STATE_NORMAL:

            /* Read both sensors */
            s_status = SHT31_Read(hi2c, sht31);
            b_status = BMP280_Read(hspi, bmp280);

            /* Check for any faults */
            detected_fault = SM_CheckSensors(sht31, bmp280, s_status, b_status);

            if (detected_fault != FAULT_NONE)
            {
                ctx->fail_count++;

                if (ctx->fail_count >= FAIL_COUNT_FAULT)
                {
                    /* Three consecutive failures — go to FAULT */
                    SM_EnterFault(ctx, detected_fault);
                }
                else if (ctx->fail_count >= FAIL_COUNT_WARN)
                {
                    /* First failure — go to WARNING */
                    SM_EnterWarning(ctx, detected_fault);
                }
            }
            else
            {
                /* Clean read — reset failure counter */
                ctx->fail_count = 0;
                ctx->fault_code = FAULT_NONE;
            }
            break;

        /* ── WARNING ────────────────────────────────────────────── */
        case STATE_WARNING:

            s_status = SHT31_Read(hi2c, sht31);
            b_status = BMP280_Read(hspi, bmp280);

            detected_fault = SM_CheckSensors(sht31, bmp280, s_status, b_status);

            if (detected_fault != FAULT_NONE)
            {
                ctx->fail_count++;

                if (ctx->fail_count >= FAIL_COUNT_FAULT)
                {
                    /* Failures accumulating — escalate to FAULT */
                    SM_EnterFault(ctx, detected_fault);
                }
            }
            else
            {
                /* Readings clean again — return to NORMAL */
                SM_EnterNormal(ctx);
            }
            break;

        /* ── FAULT ──────────────────────────────────────────────── */
        case STATE_FAULT:

            /* Stop all actuation in FAULT — safe state             */
            /* Outputs driven LOW by GPIO actuation layer           */

            /* Attempt recovery after every 10 ticks (1 second)    */
            if (ctx->tick % 10 == 0)
            {
                ctx->state           = STATE_RECOVERY;
                ctx->reinit_attempts = 0;
            }
            break;

        /* ── RECOVERY ───────────────────────────────────────────── */
        case STATE_RECOVERY:

            SM_DoRecovery(ctx, hi2c, hspi);
            break;

        default:
            /* Unknown state — safe fallback to FAULT */
            SM_EnterFault(ctx, FAULT_NONE);
            break;
    }
}

/* ─────────────────────────────────────────────────────────────────
 * SM_CheckSensors  (private)
 * Evaluates sensor read results and data values.
 * Returns FAULT_NONE if everything is clean.
 * Returns first detected fault code if any check fails.
 * ───────────────────────────────────────────────────────────────── */
static FaultCode_t SM_CheckSensors(SHT31_Data_t *sht31, BMP280_Data_t *bmp280,
                                    SHT31_Status_t s_status, BMP280_Status_t b_status)
{
    /* ── Check SHT31 read status ────────────────────────────────── */
    if (s_status == SHT31_ERR_CRC)   return FAULT_SHT31_CRC;
    if (s_status == SHT31_ERR_RANGE) return FAULT_SHT31_RANGE;
    if (s_status != SHT31_OK)        return FAULT_SHT31_CRC;

    /* ── Check BMP280 read status ───────────────────────────────── */
    if (b_status == BMP280_ERR_SPI)   return FAULT_BMP280_SPI;
    if (b_status == BMP280_ERR_RANGE) return FAULT_BMP280_RANGE;
    if (b_status != BMP280_OK)        return FAULT_BMP280_SPI;

    /* ── Cross-validate temperature readings ────────────────────── */
    /* Both sensors measure temperature — they should agree within 5C */
    if (fabsf(sht31->temperature - bmp280->temperature) > TEMP_CROSSVAL_DELTA)
    {
        return FAULT_CROSSVAL;
    }

    /* ── Check threshold warnings/faults ───────────────────────── */
    if (sht31->temperature >= TEMP_FAULT_HIGH) return FAULT_TEMP_HIGH;
    if (sht31->humidity    >= HUM_FAULT_HIGH)  return FAULT_HUM_HIGH;
    if (bmp280->pressure   <= PRESS_FAULT_LOW) return FAULT_PRESS_LOW;

    return FAULT_NONE;
}

/* ─────────────────────────────────────────────────────────────────
 * SM_DoRecovery  (private)
 * Attempts to re-initialize failed sensors.
 * Returns to NORMAL on success, back to FAULT on max attempts.
 * ───────────────────────────────────────────────────────────────── */
static void SM_DoRecovery(SystemContext_t *ctx, I2C_HandleTypeDef *hi2c,
                           SPI_HandleTypeDef *hspi)
{
    SHT31_Status_t  s_status;
    BMP280_Status_t b_status;

    ctx->reinit_attempts++;

    /* Attempt re-initialization of both sensors */
    s_status = SHT31_Init(hi2c);
    b_status = BMP280_Init(hspi);

    if (s_status == SHT31_OK && b_status == BMP280_OK)
    {
        /* Both sensors recovered — return to NORMAL */
        SM_EnterNormal(ctx);
    }
    else if (ctx->reinit_attempts >= REINIT_MAX_ATTEMPTS)
    {
        /* Max attempts reached — back to FAULT permanently */
        SM_EnterFault(ctx, ctx->fault_code);
    }
    /* else — keep trying next tick */
}

/* ─────────────────────────────────────────────────────────────────
 * SM_EnterFault  (private)
 * Transitions system to FAULT state.
 * Resets fail counter, stores fault code.
 * ───────────────────────────────────────────────────────────────── */
static void SM_EnterFault(SystemContext_t *ctx, FaultCode_t fault)
{
    ctx->state      = STATE_FAULT;
    ctx->fault_code = fault;
    ctx->fail_count = 0;
}

/* ─────────────────────────────────────────────────────────────────
 * SM_EnterWarning  (private)
 * Transitions system to WARNING state.
 * ───────────────────────────────────────────────────────────────── */
static void SM_EnterWarning(SystemContext_t *ctx, FaultCode_t fault)
{
    ctx->state      = STATE_WARNING;
    ctx->fault_code = fault;
}

/* ─────────────────────────────────────────────────────────────────
 * SM_EnterNormal  (private)
 * Transitions system to NORMAL state.
 * Clears all fault tracking.
 * ───────────────────────────────────────────────────────────────── */
static void SM_EnterNormal(SystemContext_t *ctx)
{
    ctx->state           = STATE_NORMAL;
    ctx->fault_code      = FAULT_NONE;
    ctx->fail_count      = 0;
    ctx->reinit_attempts = 0;
}

/* ─────────────────────────────────────────────────────────────────
 * SM_GetStateString
 * Returns human readable state name for UART diagnostic output.
 * ───────────────────────────────────────────────────────────────── */
const char* SM_GetStateString(SystemState_t state)
{
    switch (state)
    {
        case STATE_NORMAL:   return "NORMAL";
        case STATE_WARNING:  return "WARNING";
        case STATE_FAULT:    return "FAULT";
        case STATE_RECOVERY: return "RECOVERY";
        default:             return "UNKNOWN";
    }
}
