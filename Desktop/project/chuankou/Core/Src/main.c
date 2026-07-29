/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : TJC Waveform - 参考 tjc_screen.c 的 addt 模式
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <math.h>
#include <string.h>
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define GRAPH_W       600
#define GRAPH_H       460
#define WAVE_POINTS   GRAPH_W
#ifndef M_PI
#define M_PI          3.14159265358979323846f
#endif
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static uint16_t wave_buf[WAVE_POINTS];   /* 正弦波, 值域 0 ~ GRAPH_H */
static char tjc_buf[256];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void tjc_send(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart6, (uint8_t *)data, len, 100);
}

static const uint8_t TJC_END[3] = {0xFF, 0xFF, 0xFF};
static void tjc_end(void) { tjc_send(TJC_END, 3); }

static void tjc_cle(uint8_t ch)
{
    int len = snprintf(tjc_buf, sizeof(tjc_buf), "cle s0,%d", ch);
    tjc_send((uint8_t *)tjc_buf, len);
    tjc_end();
}

static void tjc_add_val(uint16_t val)
{
    int len = snprintf(tjc_buf, sizeof(tjc_buf), "%d", val);
    tjc_send((uint8_t *)tjc_buf, len);
    tjc_end();
}

/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART6_UART_Init();

  /* USER CODE BEGIN 2 */
  HAL_Delay(1500);  /* 等待屏幕就绪 */

  static const char *boot = "Start\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t *)boot, strlen(boot), 100);

  /* 生成一周期正弦波, 值映射到 0 ~ GRAPH_H */
  for (uint16_t i = 0; i < WAVE_POINTS; i++)
  {
      float phase = 2.0f * M_PI * i / WAVE_POINTS;
      float val   = (GRAPH_H / 2.0f) + (GRAPH_H / 2.0f - 1) * sinf(phase);
      if (val < 0.0f)       val = 0.0f;
      if (val > GRAPH_H)    val = (float)GRAPH_H;
      wave_buf[i] = (uint16_t)(val + 0.5f);
  }
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */

    /* 1. 清除曲线 */
    tjc_cle(0);
    HAL_Delay(20);

    /* 2. addt 前缀: 告知屏幕将发送 WAVE_POINTS 个点 */
    int len = snprintf(tjc_buf, sizeof(tjc_buf), "addt s0,0,%d", WAVE_POINTS);
    tjc_send((uint8_t *)tjc_buf, len);
    tjc_end();
    HAL_Delay(10);

    /* 3. 逐点发送 ASCII 值 + FF FF FF */
    for (uint16_t i = 0; i < WAVE_POINTS; i++)
    {
        /* TJC 曲线 Y轴: 0=顶部, GRAPH_H=底部, 翻转使其底部=0 */
        uint16_t y = GRAPH_H - wave_buf[i];
        if (y > GRAPH_H) y = GRAPH_H;

        tjc_add_val(y);
        HAL_Delay(1);
    }

    /* 帧结束调试: USART1 输出 */
    static const char *done = ".\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)done, strlen(done), 100);

    HAL_Delay(500);  /* 帧间延时 */
    /* USER CODE END 3 */
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
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
  while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif /* USE_FULL_ASSERT */
