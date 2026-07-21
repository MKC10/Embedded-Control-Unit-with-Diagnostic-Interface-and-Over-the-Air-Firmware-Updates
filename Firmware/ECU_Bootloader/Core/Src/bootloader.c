#include "bootloader.h"
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

void BL_JumpToApp(uint32_t base_addr, uint32_t bank_size)
{
    uint32_t app_sp    = *(volatile uint32_t*)base_addr;
    uint32_t app_reset = *(volatile uint32_t*)(base_addr + 4U);

#ifdef DEBUG
    char msg[128];

    snprintf(msg, sizeof(msg),
             "APP SP: 0x%08lX\r\nAPP RESET: 0x%08lX\r\n",
             app_sp,
             app_reset);

    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    HAL_UART_Transmit(&huart2,
                      (uint8_t*)"JUMPING TO APPLICATION\r\n",
                      24,
                      HAL_MAX_DELAY);

    HAL_Delay(10);
#endif

    /* ---------------- VALIDATE STACK POINTER ---------------- */
    if ((app_sp < 0x20000000U) || (app_sp > 0x20020000U))
    {
#ifdef DEBUG
        HAL_UART_Transmit(&huart2,
                          (uint8_t*)"INVALID APP SP\r\n",
                          17,
                          HAL_MAX_DELAY);
#endif
        return;
    }

    /* ---------------- VALIDATE RESET VECTOR ---------------- */
    uint32_t app_reset_clean = app_reset & 0xFFFFFFFEU;
    uint32_t bank_end = base_addr + bank_size;

    if ((app_reset_clean < base_addr) || (app_reset_clean >= bank_end))
    {
#ifdef DEBUG
        HAL_UART_Transmit(&huart2,
                          (uint8_t*)"INVALID APP RESET\r\n",
                          19,
                          HAL_MAX_DELAY);
#endif
        return;
    }

    /* ---------------- STOP SYSTEM INTERRUPTS ---------------- */
    __disable_irq();

    /* ---------------- CLEAR NVIC ---------------- */
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* ---------------- STOP SYSTICK ---------------- */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* ---------------- DEINIT PERIPHERALS ---------------- */
    HAL_UART_DeInit(&huart2);
    HAL_DeInit();

    /* DO NOT RESET CLOCK TREE (no HAL_RCC_DeInit) */

    /* ---------------- SET VECTOR TABLE ---------------- */
    SCB->VTOR = base_addr;

    __DSB();
    __ISB();

    /* ---------------- SET STACK POINTER (ALIGNED) ---------------- */
    app_sp &= 0xFFFFFFF8U;
    __set_MSP(app_sp);

    /* ---------------- SIMPLE CHECKPOINT ----------------
     * One raw LED pulse, right before the jump instruction itself
     * executes. Raw register access, zero HAL/peripheral dependency
     * -- confirms the bootloader genuinely reaches this exact line,
     * the last instant before control transfers away permanently. */
    {
        volatile uint32_t *rcc_ahb1enr = (volatile uint32_t*)0x40023830UL;
        volatile uint32_t *gpioa_moder = (volatile uint32_t*)0x40020000UL;
        volatile uint32_t *gpioa_bsrr  = (volatile uint32_t*)0x40020018UL;
        *rcc_ahb1enr |= (1UL << 0);
        *gpioa_moder &= ~(3UL << (5*2));
        *gpioa_moder |=  (1UL << (5*2));
        *gpioa_bsrr = (1UL << 5);
        for (volatile int d = 0; d < 500000; d++) { }
        *gpioa_bsrr = (1UL << (5 + 16));
    }

    /* ---------------- JUMP TO APPLICATION ---------------- */
    /* FIX: app_reset_clean (bit 0 stripped) was being used for the
     * actual function call below -- but Cortex-M REQUIRES bit 0 set
     * (the Thumb bit) on any callable function pointer; that bit is
     * how the CPU knows to branch in Thumb mode, which is the only
     * mode Cortex-M supports at all. Calling through a pointer with
     * that bit cleared causes an IMMEDIATE fault on the branch
     * itself, landing silently in Default_Handler (the app's own
     * vector table, since VTOR was already switched) -- before a
     * single instruction of main() ever runs. app_reset_clean was
     * only ever meant for the range-validation check above; the
     * actual call must use the original app_reset value instead. */
    void (*app_entry)(void);
    app_entry = (void (*)(void))app_reset;

    app_entry();

    /* ---------------- SAFETY HANG (MUST NEVER RETURN) ---------------- */
    while (1);
}
