/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "i2c.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "oled.h"
#include "stdlib.h"
#include "string.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DINO_X 16           // 恐龙X坐标
#define DINO_Y 48           // 恐龙地面Y坐标
#define DINO_WIDTH 16       // 恐龙宽度
#define DINO_HEIGHT 16      // 恐龙高度
#define GROUND_Y 56         // 地面Y坐标
#define MAX_JUMP_HEIGHT 24  // 最大跳跃高度
#define CACTUS_Y 48         // 仙人掌Y坐标



#define START_PORT        GPIOC
#define JUMP_PORT        GPIOD
#define START_PIN   GPIO_PIN_7    // 开始/重启键
#define JUMP_PIN    GPIO_PIN_0    // 跳跃键

#define KEY_NONE        0
#define KEY_JUMP        1
#define KEY_START       2


#define KEY_DEBOUNCE_DELAY  10        // 消抖延时(ms)
#define KEY_RELEASE_DELAY   200       // 按键释放延时(ms)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void DrawDigit(uint8_t x, uint8_t y, uint8_t digit);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int get_key_val(void)
{
  // 检测开始键(PC7)
  if(HAL_GPIO_ReadPin(START_PORT, START_PIN) == GPIO_PIN_RESET)
  {
    HAL_Delay(KEY_DEBOUNCE_DELAY); // 消抖
    if(HAL_GPIO_ReadPin(START_PORT, START_PIN) == GPIO_PIN_RESET)
    {
      // 等待按键释放
      while(HAL_GPIO_ReadPin(START_PORT, START_PIN) == GPIO_PIN_RESET);
      HAL_Delay(KEY_DEBOUNCE_DELAY); // 释放后消抖
      return KEY_START;
    }
  }
  
  // 检测跳跃键(PD0)
  if(HAL_GPIO_ReadPin(JUMP_PORT, JUMP_PIN) == GPIO_PIN_RESET)
  {
    HAL_Delay(KEY_DEBOUNCE_DELAY); // 消抖
    if(HAL_GPIO_ReadPin(JUMP_PORT, JUMP_PIN) == GPIO_PIN_RESET)
    {
      return KEY_JUMP;
    }
  }
  
  return KEY_NONE;
}

int get_key_val_noblock(void)
{
  static uint32_t last_key_time = 0;
  uint32_t current_time = HAL_GetTick();
  
  // 防止按键过快响应
  if(current_time - last_key_time < 50)
    return KEY_NONE;
  
  // 检测开始键
  if(HAL_GPIO_ReadPin(START_PORT, START_PIN) == GPIO_PIN_RESET)
  {
    last_key_time = current_time;
    return KEY_START;
  }
  
  // 检测跳跃键
  if(HAL_GPIO_ReadPin(JUMP_PORT, JUMP_PIN) == GPIO_PIN_RESET)
  {
    last_key_time = current_time;
    return KEY_JUMP;
  }
  
  return KEY_NONE;
}

void DrawScore(unsigned int score, unsigned int high_score)
{
  char buf[10];
  
  // 显示 HI 标记
  OLED_PrintASCIIString(70, 2, "HI", &afont8x6, OLED_COLOR_NORMAL);
  
  // 显示最高分
  sprintf(buf, "%05u", high_score);
  OLED_PrintASCIIString(85, 2, buf, &afont8x6, OLED_COLOR_NORMAL);
  
  // 显示当前分数
  sprintf(buf, "%05u", score);
  OLED_PrintASCIIString(25, 2, buf, &afont8x6, OLED_COLOR_NORMAL);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
unsigned char key_num = KEY_NONE;
  unsigned char cactus_type = 0;
  unsigned char cactus_width = 8;
  unsigned int score = 0;           // 添加分数变量
  unsigned int high_score = 0;      // 添加最高分变量
  int height = 0;
  int jump_speed = 0;
  int cactus_x = 128;
  unsigned char game_speed = 30;
  unsigned int frame_count = 0;
  char game_over = 0;
  int cloud_x = 100;
  uint8_t ground_offset = 0;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
 HAL_Delay(20);
  OLED_Init();
  
  // 显示开始界面
  OLED_NewFrame();
  
  // 使用打印函数显示标题
  OLED_DrawRectangle(10, 10, 108, 30, OLED_COLOR_NORMAL);
  OLED_PrintASCIIString(25, 20, "DINO GAME", &afont8x6, OLED_COLOR_NORMAL);
  
  // 绘制简单恐龙
  OLED_DrawFilledRectangle(54, 45, 10, 10, OLED_COLOR_NORMAL);
  OLED_DrawFilledRectangle(52, 55, 4, 6, OLED_COLOR_NORMAL);
  OLED_DrawFilledRectangle(60, 55, 4, 6, OLED_COLOR_NORMAL);
  OLED_SetPixel(59, 48, OLED_COLOR_REVERSED);
  
  OLED_ShowFrame();
  
  // 等待开始
  while(get_key_val() != KEY_START)
  {
    HAL_Delay(10);
  }
  HAL_Delay(KEY_RELEASE_DELAY);
  
  // 初始化随机数
  srand(HAL_GetTick());
  cactus_type = rand() % 3;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
		// ==================== 游戏结束处理 ====================
     // ==================== 游戏结束处理 ====================
    if (game_over == 1)
    {
      // 立即更新最高分（在重置任何变量之前）
      if(score > high_score)
      {
        high_score = score;
      }
      
      OLED_NewFrame();
      
      // 绘制边框
      OLED_DrawRectangle(15, 12, 98, 45, OLED_COLOR_NORMAL);
      OLED_DrawRectangle(16, 13, 96, 43, OLED_COLOR_NORMAL);
      
      // 使用打印函数显示 "GAME OVER"
      OLED_PrintASCIIString(28, 18, "GAME OVER", &afont8x6, OLED_COLOR_NORMAL);
      
      // 显示最终分数
      char score_buf[20];
      sprintf(score_buf, "Score: %u", score);
      OLED_PrintASCIIString(28, 30, score_buf, &afont8x6, OLED_COLOR_NORMAL);
      
      // 显示最高分
      sprintf(score_buf, "Best:  %u", high_score);
      OLED_PrintASCIIString(28, 40, score_buf, &afont8x6, OLED_COLOR_NORMAL);
      
      // 显示重启提示
      OLED_PrintASCIIString(20, 52, "Press START", &afont8x6, OLED_COLOR_NORMAL);
      
      OLED_ShowFrame();
      
      key_num = get_key_val();
      if (key_num == KEY_START)
      {
        // 重置游戏变量（最高分已保存，不重置）
        score = 0;              
        game_over = 0;
        height = 0;
        jump_speed = 0;
        cactus_x = 128;
        cactus_type = rand() % 3;
        game_speed = 30;
        frame_count = 0;
        cloud_x = 100;
        ground_offset = 0;
        
        HAL_Delay(KEY_RELEASE_DELAY);
      }
      continue;
    }
    
    // ==================== 开始绘制新帧 ====================
    OLED_NewFrame();
    
    frame_count++;
    
    // 每10帧增加1分
    if(frame_count % 10 == 0)
    {
      score++;
    }
    
    // 获取按键
    if (height == 0) 
      key_num = get_key_val_noblock();
    
    // ==================== 绘制分数 ====================
    DrawScore(score, high_score);
    
    // ==================== 绘制地面 ====================
    for(uint8_t i = 0; i < 128; i += 6)
    {
      uint8_t pos = (i + ground_offset) % 128;
      OLED_DrawLine(pos, GROUND_Y, pos + 3, GROUND_Y, OLED_COLOR_NORMAL);
    }
    ground_offset = (ground_offset + 2) % 6;
    
    // ==================== 绘制云朵 ====================
    cloud_x--;
    if(cloud_x < -15)
      cloud_x = 128;
    
    if(cloud_x >= 0 && cloud_x < 128)
    {
      OLED_DrawCircle(cloud_x+2, 10, 3, OLED_COLOR_NORMAL);
      OLED_DrawCircle(cloud_x+6, 9, 3, OLED_COLOR_NORMAL);
      OLED_DrawCircle(cloud_x+10, 10, 3, OLED_COLOR_NORMAL);
      OLED_DrawFilledRectangle(cloud_x+2, 10, 8, 2, OLED_COLOR_NORMAL);
    }
    
    // ==================== 恐龙跳跃逻辑 ====================
    if (key_num == KEY_JUMP && height == 0)
    {
      jump_speed = -6;
    }
    
    if (height != 0 || jump_speed != 0)
    {
      height += jump_speed;
      jump_speed += 1;
      
      if (height >= 0)
      {
        height = 0;
        jump_speed = 0;
      }
      else if (height < -MAX_JUMP_HEIGHT)
      {
        height = -MAX_JUMP_HEIGHT;
        jump_speed = 0;
      }
    }
    
    // ==================== 绘制恐龙 ====================
    int dino_draw_y = DINO_Y + height;  // 改为 int 类型
    // 头部
    OLED_DrawFilledRectangle(DINO_X+6, dino_draw_y, 6, 5, OLED_COLOR_NORMAL);
    OLED_SetPixel(DINO_X+9, dino_draw_y+2, OLED_COLOR_REVERSED);
    OLED_DrawLine(DINO_X+11, dino_draw_y+3, DINO_X+12, dino_draw_y+3, OLED_COLOR_REVERSED);
    // 身体
    OLED_DrawFilledRectangle(DINO_X+2, dino_draw_y+5, 10, 6, OLED_COLOR_NORMAL);
    // 尾巴
    OLED_DrawLine(DINO_X, dino_draw_y+7, DINO_X+2, dino_draw_y+7, OLED_COLOR_NORMAL);
    // 左腿
    OLED_DrawFilledRectangle(DINO_X+4, dino_draw_y+11, 2, 5, OLED_COLOR_NORMAL);
    // 右腿
    OLED_DrawFilledRectangle(DINO_X+8, dino_draw_y+11, 2, 5, OLED_COLOR_NORMAL);
    
    // ==================== 仙人掌移动 ====================
    cactus_x -= 3;
    
    // 确定仙人掌宽度
    if(cactus_type == 0) 
      cactus_width = 8;
    else if(cactus_type == 1) 
      cactus_width = 16;
    else 
      cactus_width = 24;
    
    // 重新生成仙人掌（在绘制之前检查）
    if (cactus_x + (int)cactus_width < 0)
    {
      cactus_x = 128 + (rand() % 20 + 10);  // 128到158之间
      cactus_type = rand() % 3;
      
      // 重新计算宽度
      if(cactus_type == 0) 
        cactus_width = 8;
      else if(cactus_type == 1) 
        cactus_width = 16;
      else 
        cactus_width = 24;
    }
    
    // 绘制仙人掌（只在可见范围内绘制）
    if(cactus_x >= -30 && cactus_x < 128)
    {
      switch(cactus_type)
      {
        case 0: // 小仙人掌
          if(cactus_x + 3 >= 0 && cactus_x + 3 < 128)
            OLED_DrawFilledRectangle(cactus_x+3, CACTUS_Y+2, 2, 6, OLED_COLOR_NORMAL);
          if(cactus_x + 1 >= 0 && cactus_x + 1 < 128)
            OLED_DrawFilledRectangle(cactus_x+1, CACTUS_Y+4, 2, 3, OLED_COLOR_NORMAL);
          if(cactus_x + 5 >= 0 && cactus_x + 5 < 128)
            OLED_DrawFilledRectangle(cactus_x+5, CACTUS_Y+4, 2, 3, OLED_COLOR_NORMAL);
          break;
          
        case 1: // 中仙人掌
          if(cactus_x + 7 >= 0 && cactus_x + 7 < 128)
            OLED_DrawFilledRectangle(cactus_x+7, CACTUS_Y, 2, 8, OLED_COLOR_NORMAL);
          if(cactus_x + 3 >= 0 && cactus_x + 3 < 128)
            OLED_DrawFilledRectangle(cactus_x+3, CACTUS_Y+2, 3, 5, OLED_COLOR_NORMAL);
          if(cactus_x + 10 >= 0 && cactus_x + 10 < 128)
            OLED_DrawFilledRectangle(cactus_x+10, CACTUS_Y+3, 3, 4, OLED_COLOR_NORMAL);
          break;
          
        case 2: // 大仙人掌
          if(cactus_x + 11 >= 0 && cactus_x + 11 < 128)
            OLED_DrawFilledRectangle(cactus_x+11, CACTUS_Y-2, 2, 10, OLED_COLOR_NORMAL);
          if(cactus_x + 7 >= 0 && cactus_x + 7 < 128)
            OLED_DrawFilledRectangle(cactus_x+7, CACTUS_Y+1, 3, 6, OLED_COLOR_NORMAL);
          if(cactus_x + 14 >= 0 && cactus_x + 14 < 128)
            OLED_DrawFilledRectangle(cactus_x+14, CACTUS_Y+2, 3, 5, OLED_COLOR_NORMAL);
          if(cactus_x + 3 >= 0 && cactus_x + 3 < 128)
            OLED_DrawFilledRectangle(cactus_x+3, CACTUS_Y+3, 3, 4, OLED_COLOR_NORMAL);
          if(cactus_x + 18 >= 0 && cactus_x + 18 < 128)
            OLED_DrawFilledRectangle(cactus_x+18, CACTUS_Y+4, 3, 3, OLED_COLOR_NORMAL);
          break;
      }
    }
    
    // ==================== 碰撞检测 ====================
    if (height > -14)
    {
      // 更精确的碰撞检测
      if ((cactus_x < DINO_X + DINO_WIDTH - 4) && 
          (cactus_x + (int)cactus_width > DINO_X + 4))
      {
        game_over = 1;
      }
    }
    
    // ==================== 显示这一帧 ====================
    OLED_ShowFrame();
    
    // ==================== 游戏速度控制 ====================
     game_speed = score / 50;  // 根据分数调整速度
    if (game_speed > 22) 
      game_speed = 22;
    HAL_Delay(30 - game_speed);
    
    key_num = KEY_NONE;
  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
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

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
