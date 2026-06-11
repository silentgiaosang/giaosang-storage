/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oscilloscope.h"
#include "ui.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_adc2;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA2 stream0 global interrupt.
  */
void DMA2_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream0_IRQn 0 */

  /* USER CODE END DMA2_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA2_Stream0_IRQn 1 */

  /* USER CODE END DMA2_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream2 global interrupt.
  */
void DMA2_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream2_IRQn 0 */

  /* USER CODE END DMA2_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc2);
  /* USER CODE BEGIN DMA2_Stream2_IRQn 1 */

  /* USER CODE END DMA2_Stream2_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* ---- 编码器 EXTI 中断: A相下降沿, B相读方向 ---- */

/**
 * @brief EXTI0: SW3 垂直幅度编码器 (PD0=A↓, PD1=B)
 */
void EXTI0_IRQHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) != RESET)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);

    /* 消抖: 读两次B, 不一致则丢弃 */
    uint8_t b1 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1) == GPIO_PIN_SET) ? 1 : 0;
    for (volatile uint32_t d = 0; d < 500; d++) { __NOP(); }
    uint8_t b2 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1) == GPIO_PIN_SET) ? 1 : 0;
    if (b1 != b2) return;
    uint8_t b = b1;
    if (b)
    {
      /* CCW: V/div 减小(放大波形) */
      if (g_osc.vdiv < VSCALE_500MV)
      {
        Osc_SetVScale((OscVScale_t)(g_osc.vdiv + 1));
        g_enc_ui_dirty = 1;
      }
    }
    else
    {
      /* CW: V/div 增大(缩小波形) */
      if (g_osc.vdiv > VSCALE_2V)
      {
        Osc_SetVScale((OscVScale_t)(g_osc.vdiv - 1));
        g_enc_ui_dirty = 1;
      }
    }
  }
}

/**
 * @brief EXTI2: SW2 时基编码器 (PD2=A↓, PD3=B)
 */
void EXTI2_IRQHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_2) != RESET)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_2);

    /* 消抖: 读两次B, 不一致则丢弃 */
    uint8_t b1 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_3) == GPIO_PIN_SET) ? 1 : 0;
    for (volatile uint32_t d = 0; d < 500; d++) { __NOP(); }
    uint8_t b2 = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_3) == GPIO_PIN_SET) ? 1 : 0;
    if (b1 != b2) return;
    uint8_t b = b1;
    if (b)
    {
      /* CCW: 时基增大(展宽波形) */
      if (g_osc.timebase < TB_20MS)
      {
        g_osc.auto_tb = 0;
        Osc_SetTimebase((OscTimebase_t)(g_osc.timebase + 1));
        g_enc_ui_dirty = 1;
      }
    }
    else
    {
      /* CW: 时基减小(压缩波形) */
      if (g_osc.timebase > TB_5US)
      {
        g_osc.auto_tb = 0;
        Osc_SetTimebase((OscTimebase_t)(g_osc.timebase - 1));
        g_enc_ui_dirty = 1;
      }
    }
  }
}

/* USER CODE END 1 */
