/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "lcd.h"
#include "oscilloscope.h"
#include "ui.h"
#include "fft.h"
#include "dac.h"
#include <stdio.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* USER CODE BEGIN PD */
#define DISP_REFRESH_MS   33U
#define DAC_TEST_FREQ_HZ  1000U
/* USER CODE END PD */

/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* USER CODE BEGIN PV */
static uint32_t last_disp_tick = 0;
static uint32_t last_dac_ms    = 0;
static uint32_t dac_accum_us   = 0;
static uint8_t  dac_high       = 0;
/* USER CODE END PV */

void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void IO_Init(void);
static void IO_Process(void);
static void DAC_TestSignal_Init(void);
static void DAC_TestSignal_Process(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */
  SystemClock_Config();
  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  //MX_DAC_Init();
  /* USER CODE BEGIN 2 */

LCD_Init();
IO_Init();            /* 编码器+按键+继电器 */
MX_ADC2_Init();
Osc_Init();
FFT_Init();
Osc_Start();
//DAC_TestSignal_Init();

/* 上电自动设置 */
HAL_Delay(30);
Osc_AutoSet();

UI_ClearWaveAreas();
UI_ClearStatusBar();
UI_DrawGrids();
UI_DrawStatusBar(&g_osc);

last_disp_tick = HAL_GetTick();
last_dac_ms    = HAL_GetTick();

  /* USER CODE END 2 */

  while (1)
  {
    /* ---- 编码器 + 按键处理 ---- */
    IO_Process();

    /* ---- DAC测试信号 ---- */
    DAC_TestSignal_Process();

    /* ---- 显示刷新 ---- */
    if (HAL_GetTick() - last_disp_tick >= DISP_REFRESH_MS)
    {
      last_disp_tick = HAL_GetTick();

      if (g_osc.disp_mode == DISP_WAVEFORM)
      {
        Osc_Capture();
        Osc_DoMeasurements();
        Osc_AutoTimebase();

        UI_DrawWaveform(OSC_CH0, g_osc.disp_buf[OSC_CH0],
                        OSC_PRE_TRIG, g_osc.trig_found,
                        g_osc.trig_channel == OSC_CH0);
        UI_DrawWaveform(OSC_CH1, g_osc.disp_buf[OSC_CH1],
                        0, 0,
                        g_osc.trig_channel == OSC_CH1);
      }
      else
      {
        Osc_Capture();
        Osc_DoMeasurements();
        Osc_AutoTimebase();
        g_osc.fft_sample_rate = (float)Osc_GetCurrentSampleHz();
        FFT_Process(g_osc.adc_buf, OSC_ADC_BUF_SIZE,
                    g_osc.dma_last_pos, g_osc.fft_sample_rate);
        UI_DrawFFT(&g_fft_result);
      }

      UI_DrawStatusBar(&g_osc);
    }

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    Error_Handler();
}

/* USER CODE BEGIN 4 */

/* ========== IO系统: 编码器(EXTI中断) + K1~K6 + 继电器 ========== */
/*
 * 编码器: SW2(PD2=EXTI2_A, PD3=B)调整时基, SW3(PD0=EXTI0_A, PD1=B)调整垂直幅度
 *          EXTI下降沿中断驱动, ISR在stm32f4xx_it.c中
 * 按键:   K1(PD13)=AUTO, K2(PD11)=触发通道切换, K3(PD9)=双通道同步耦合,
 *         K4(PD12)=MODE, K5(PD10)=预留, K6(PD8)=预留
 * 继电器: PE0=CH1耦合, PE11=CH2耦合 (1=DC)
 */
static uint8_t  btn_last[6] = {1,1,1,1,1,1};
static uint32_t io_tick = 0;

static void IO_Init(void)
{
  GPIO_InitTypeDef G = {0};

  /* ---- 按键 K1-K6: PD8-PD13 上拉输入 ---- */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  G.Pin  = GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10
         | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
  G.Mode = GPIO_MODE_INPUT;
  G.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOD, &G);

  /* ---- 继电器: PE0, PE11 推挽输出(默认DC=1) ---- */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  G.Pin   = GPIO_PIN_0 | GPIO_PIN_11;
  G.Mode  = GPIO_MODE_OUTPUT_PP;
  G.Pull  = GPIO_NOPULL;
  G.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &G);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0 | GPIO_PIN_11, GPIO_PIN_SET);
}

static void IO_Process(void)
{
  uint32_t now = HAL_GetTick();
  if (now - io_tick < 10) return;   /* 10ms周期 */
  io_tick = now;

  /* ---- 编码器中断触发的UI刷新 ---- */
  if (g_enc_ui_dirty)
  {
    g_enc_ui_dirty = 0;
    UI_ClearWaveAreas();
    if (g_osc.disp_mode == DISP_WAVEFORM) UI_DrawGrids();
    UI_ResetStatusBar();
  }

  /* ---- 自校正定时: 校正中→校正完成(2s) →恢复(3.5s) ---- */
  if (g_calib_state == 1 && now - g_calib_start_ms >= 2000)
  {
    g_calib_state = 2;
    UI_ResetStatusBar();
  }
  if (g_calib_state == 2 && now - g_calib_start_ms >= 3500)
  {
    g_calib_state = 0;
    UI_ResetStatusBar();
  }

  /* ====== 按键 (下降沿触发) ====== */
  uint16_t bp[6] = {GPIO_PIN_13, GPIO_PIN_11, GPIO_PIN_9,
                    GPIO_PIN_12, GPIO_PIN_10, GPIO_PIN_8};
  uint8_t bn[6];
  for (int i = 0; i < 6; i++)
  {
    bn[i] = (HAL_GPIO_ReadPin(GPIOD, bp[i]) == GPIO_PIN_SET) ? 1 : 0;
    if (bn[i] == 0 && btn_last[i] == 1)  /* 按下 */
    {
      if (i == 0)  /* K1(PD13): AUTO */
      {
        Osc_AutoSet();
        g_osc.auto_tb = 1;
        UI_ClearWaveAreas(); UI_ResetCache(); UI_DrawGrids();
      }
      else if (i == 1)  /* K2(PD11): 触发通道切换 */
      {
        g_osc.trig_channel = !g_osc.trig_channel;
        UI_ResetStatusBar();
      }
      else if (i == 2)  /* K3(PD9): 双通道同步耦合切换(以CH2为准) */
      {
        Osc_ToggleCoupling(OSC_CH1);
        HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_11);
        g_osc.coupling_dc[OSC_CH0] = g_osc.coupling_dc[OSC_CH1];
        if (g_osc.coupling_dc[OSC_CH0])
          HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
        else
          HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET);
      }
      else if (i == 3)  /* K4(PD12): MODE切换 */
      {
        Osc_SwitchDispMode();
        UI_ClearWaveAreas(); UI_ResetCache();
        if (g_osc.disp_mode == DISP_WAVEFORM) UI_DrawGrids();
      }
      else if (i == 4)  /* K5(PD10): 触发调节 / FFT通道切换 */
      {
        if (g_osc.disp_mode == DISP_FFT)
        {
            g_osc.fft_channel = !g_osc.fft_channel;
            UI_ClearWaveAreas();
            UI_ResetStatusBar();
        }
        else
        {
            g_trig_adj_mode = !g_trig_adj_mode;
            UI_ResetStatusBar();
        }
      }
      else if (i == 5)  /* K6(PD8): 自校正 */
      {
        g_calib_state    = 1;
        g_calib_start_ms = HAL_GetTick();
        UI_ResetStatusBar();
      }
    }
    btn_last[i] = bn[i];
  }
}

/* ======================== DAC 测试方波 ======================== */
static void DAC_TestSignal_Init(void)
{
  dac_high = 0;
  dac_accum_us = 0;
  HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 500);
  HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
}

static void DAC_TestSignal_Process(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t half_period_us = 500000U / DAC_TEST_FREQ_HZ;
  uint32_t elapsed_ms = now - last_dac_ms;
  if (elapsed_ms == 0) return;
  last_dac_ms = now;
  dac_accum_us += elapsed_ms * 1000U;
  while (dac_accum_us >= half_period_us)
  {
    dac_accum_us -= half_period_us;
    dac_high = !dac_high;
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R,
                     dac_high ? 3000 : 500);
  }
}

/* USER CODE END 4 */

void Error_Handler(void)
{
  __disable_irq();
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
