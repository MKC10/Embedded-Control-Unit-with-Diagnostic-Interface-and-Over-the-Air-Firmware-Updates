/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "firmware.h"
#include <stdint.h>

#ifdef BUILD_BANK_B
#define APP_BASE_ADDR 0x08040000U
#else
#define APP_BASE_ADDR 0x08010000U
#endif

void SystemClock_Config(void);

/* SIMPLE CHECKPOINT -- one triple-blink, the first thing that runs.
 * Raw register access, no HAL/clock dependency. Confirms the app's
 * compiled code actually started executing after the bootloader's
 * jump (or a direct flash+run). */
static void dbg_pulse_app_started(void)
{
    volatile uint32_t *rcc_ahb1enr = (volatile uint32_t*)0x40023830UL;
    volatile uint32_t *gpioa_moder = (volatile uint32_t*)0x40020000UL;
    volatile uint32_t *gpioa_bsrr  = (volatile uint32_t*)0x40020018UL;

    *rcc_ahb1enr |= (1UL << 0);
    *gpioa_moder &= ~(3UL << (5*2));
    *gpioa_moder |=  (1UL << (5*2));

    for (int i = 0; i < 3; i++)
    {
        *gpioa_bsrr = (1UL << 5);
        for (volatile int d = 0; d < 300000; d++) { }
        *gpioa_bsrr = (1UL << (5 + 16));
        for (volatile int d = 0; d < 300000; d++) { }
    }
}

int main(void)
{
  /* UNAMBIGUOUS CHECKPOINT -- solid ON, never touched again after
   * this point. No counting, no timing to misjudge: either the LED
   * is lit after a reset, or it is completely dark. This replaces
   * the triple-blink, which can visually blur into looking like a
   * single flicker and gave an ambiguous result on the last test. */
  {
      volatile uint32_t *rcc_ahb1enr = (volatile uint32_t*)0x40023830UL;
      volatile uint32_t *gpioa_moder = (volatile uint32_t*)0x40020000UL;
      volatile uint32_t *gpioa_bsrr  = (volatile uint32_t*)0x40020018UL;
      *rcc_ahb1enr |= (1UL << 0);
      *gpioa_moder &= ~(3UL << (5*2));
      *gpioa_moder |=  (1UL << (5*2));
      *gpioa_bsrr = (1UL << 5);   /* ON -- and left on, nothing else
                                     in this function touches it */
  }

  /* FPU enable -- must happen before any floating-point instruction.
   * Default_Handler/HardFault_Handler is a silent infinite loop, so
   * an FPU fault before this line shows as total silence with no
   * clue at all -- this is the same fault class found and fixed in
   * the bare-metal test binaries earlier this week. */
  SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));
  __DSB();
  __ISB();

  SCB->VTOR = APP_BASE_ADDR;
  __DSB();
  __ISB();

  HAL_Init();

  /* SystemClock_Config() internally handles being entered with the
   * PLL already active (bootloader entry) as well as a clean reset
   * (direct flash+run) -- see the fix inside that function. Works
   * correctly either way, no special handling needed here. */
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();

  HAL_UART_Transmit(&huart2,
                    (uint8_t*)"\r\nAPP STARTED [BUILD: CLOCKFIX-V2]\r\n",
                    36,
                    HAL_MAX_DELAY);

  __enable_irq();

  Firmware_Init();
  Firmware_Run();

  while (1)
  {
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* --------------------------------------------------------------
   * FIX: this function must work correctly regardless of entry
   * state. The bootloader deliberately does NOT reset the clock
   * tree before jumping (kept intentionally, to preserve stable
   * UART timing across the handoff), so when entered via the
   * bootloader's jump, SYSCLK is ALREADY running from a PLL lock
   * (the bootloader's own 180MHz configuration) -- whereas a direct
   * CubeIDE flash+run starts from a true reset, SYSCLK on HSI, no
   * PLL active yet.
   *
   * HAL_RCC_OscConfig() below refuses to modify PLL settings while
   * the PLL is the CURRENTLY ACTIVE SYSCLK source -- it silently
   * returns HAL_ERROR, landing in Error_Handler() (permanent silent
   * loop). This defensive switch neutralizes that: if SYSCLK is
   * currently PLL-sourced, switch to HSI and disable the PLL FIRST,
   * so it's guaranteed free to reconfigure below, regardless of
   * which path led into main(). If SYSCLK is already HSI (a true
   * reset), this is a no-op almost immediately (the SWS check
   * passes trivially).
   * -------------------------------------------------------------- */
  if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL)
  {
      volatile uint32_t timeout;

      RCC->CR |= RCC_CR_HSION;
      timeout = 1000000;
      while (!(RCC->CR & RCC_CR_HSIRDY) && --timeout) { }

      RCC->CFGR &= ~RCC_CFGR_SW;                     /* select HSI as SYSCLK */
      timeout = 1000000;
      while (((RCC->CFGR & RCC_CFGR_SWS) != 0) && --timeout) { }

      RCC->CR &= ~RCC_CR_PLLON;                      /* now safe to disable PLL */
      timeout = 1000000;
      while ((RCC->CR & RCC_CR_PLLRDY) && --timeout) { }
  }

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK  |
                                RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1  |
                                RCC_CLOCKTYPE_PCLK2;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();

  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
