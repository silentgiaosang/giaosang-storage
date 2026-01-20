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
#include "oled.h"
#include "menu.h"
#include "stdio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define KEY_UP_PIN      GPIO_PIN_0
#define KEY_DOWN_PIN    GPIO_PIN_1
#define KEY_OK_PIN      GPIO_PIN_2
#define KEY_BACK_PIN    GPIO_PIN_3
#define KEY_PORT        GPIOC


/* ========================== 全局变量（菜单数值示例） ========================== */
static int32_t g_brightness = 50;   // 屏幕亮度（0-100）
static int32_t g_volume = 30;       // 音量（0-100）
static int32_t g_hour = 12;         // 小时（0-23）
static int32_t g_minute = 0;        // 分钟（0-59）


/**
 * @brief  功能1：显示信息
 */
void Func_ShowInfo(void)
{
    OLED_NewFrame();
    OLED_PrintASCIIString(0, 16, "Info Display", &afont8x6, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 26, "Version: 1.0", &afont8x6, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 36, "MCU: STM32F407", &afont8x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
    HAL_Delay(2000);
}

void Func_SystemReset(void)
{
    OLED_NewFrame();
    OLED_PrintASCIIString(0, 26, "Resetting...", &afont8x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
    HAL_Delay(1000);
    NVIC_SystemReset();
}

void Func_SaveSettings(void)
{
    OLED_NewFrame();
    OLED_PrintASCIIString(0, 26, "Settings Saved!", &afont8x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
    HAL_Delay(1000);
}
/* ========================== 菜单结构配置 ========================== */

// 三级菜单：系统设置 -> 时间设置 -> 时间子选项
MenuItem_t menu_time_items[] = 
{
    {"Hour",   MENU_TYPE_VALUE, NULL, NULL, 0, &g_hour,   0, 23, 1},
    {"Minute", MENU_TYPE_VALUE, NULL, NULL, 0, &g_minute, 0, 59, 1},
    {NULL, 0, NULL, NULL, 0, NULL, 0, 0, 0} // 结束标记
};

// 二级菜单：系统设置 -> 设置子选项
MenuItem_t menu_settings_items[] = 
{
    {"Brightness", MENU_TYPE_VALUE,   NULL, NULL, 0, &g_brightness, 0, 100, 5},
    {"Volume",     MENU_TYPE_VALUE,   NULL, NULL, 0, &g_volume,     0, 100, 10},
    {"Time Set",   MENU_TYPE_SUBMENU, NULL, menu_time_items, 2, NULL, 0, 0, 0},
    {"Save",       MENU_TYPE_FUNCTION, Func_SaveSettings, NULL, 0, NULL, 0, 0, 0},
    {NULL, 0, NULL, NULL, 0, NULL, 0, 0, 0}
};

// 一级菜单：根菜单
MenuItem_t menu_root_items[] = 
{
    {"System Set",  MENU_TYPE_SUBMENU,  NULL, menu_settings_items, 4, NULL, 0, 0, 0},
    {"Show Info",   MENU_TYPE_FUNCTION, Func_ShowInfo, NULL, 0, NULL, 0, 0, 0},
    {"System Reset", MENU_TYPE_FUNCTION, Func_SystemReset, NULL, 0, NULL, 0, 0, 0},
    {NULL, 0, NULL, NULL, 0, NULL, 0, 0, 0}
};


 uint8_t Key_Scan(void)  
{
    static uint8_t key_state = 0;
    
    if(key_state == 0)
    {
        if(HAL_GPIO_ReadPin(KEY_PORT, KEY_UP_PIN) == GPIO_PIN_RESET)
        {
            key_state = 1;
            HAL_Delay(20);
            return 1;
        }
        else if(HAL_GPIO_ReadPin(KEY_PORT, KEY_DOWN_PIN) == GPIO_PIN_RESET)
        {
            key_state = 1;
            HAL_Delay(20);
            return 2;
        }
        else if(HAL_GPIO_ReadPin(KEY_PORT, KEY_OK_PIN) == GPIO_PIN_RESET)
        {
            key_state = 1;
            HAL_Delay(20);
            return 3;
        }
        else if(HAL_GPIO_ReadPin(KEY_PORT, KEY_BACK_PIN) == GPIO_PIN_RESET)
        {
            key_state = 1;
            HAL_Delay(20);
            return 4;
        }
    }
    else
    {
        if(HAL_GPIO_ReadPin(KEY_PORT, KEY_UP_PIN) == GPIO_PIN_SET &&
           HAL_GPIO_ReadPin(KEY_PORT, KEY_DOWN_PIN) == GPIO_PIN_SET &&
           HAL_GPIO_ReadPin(KEY_PORT, KEY_OK_PIN) == GPIO_PIN_SET &&
           HAL_GPIO_ReadPin(KEY_PORT, KEY_BACK_PIN) == GPIO_PIN_SET)
        {
            key_state = 0;
        }
    }
    
    return 0;
}
/* USER CODE END PTD */

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
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

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
		/* OLED初始化（波特律动驱动无需传参） */
    OLED_Init();
    
    /* 菜单系统初始化 */
    Menu_Init(menu_root_items, 3);
    
    /* 显示欢迎界面 */
    OLED_NewFrame();
    OLED_PrintASCIIString(20, 16, "STM32 OLED", &afont8x6, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(10, 26, "Menu System", &afont8x6, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(15, 36, "Loading...", &afont8x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
    HAL_Delay(2000);
    
    /* 菜单系统初始化 */
    Menu_Init(menu_root_items, 3); // 根菜单有3个选项
    
    /* 显示欢迎界面 */
    
    HAL_Delay(2000);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
		 // 按键扫描
        uint8_t key = Key_Scan();
        
        // 菜单按键处理
        if(key != 0)
        {
            Menu_KeyHandler(key);
        }
        
        // 菜单显示刷新
        Menu_Display();
        
        HAL_Delay(50); // 降低刷新频率，减少闪烁
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
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
