/**
  ******************************************************************************
  * @file    lcd.h
  * @brief   ST7789V TFT LCD 驱动 (跨平台封装)
  * @note    换用其他MCU只需修改下方 [用户配置区]
  *          分辨率: 240x320, 接口: 4线SPI Mode0
  ******************************************************************************
  */
#ifndef __LCD_H__
#define __LCD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*                        [ 用户配置区 ]                               */
/*         移植到其他MCU只需修改此区域, .c文件无需改动                  */
/* ================================================================== */

/* ----- 1. MCU HAL 头文件 ----- */
#include "stm32f4xx_hal.h"

/* ----- 2. SPI 句柄 ---- */
extern SPI_HandleTypeDef hspi1;  /* CubeMX 在 spi.h 中生成, 此处声明 */
#define LCD_SPI         hspi1

/* ----- 3. 控制引脚 ----- */
#define LCD_CS_PORT     GPIOA
#define LCD_CS_PIN      GPIO_PIN_3
#define LCD_DC_PORT     GPIOA
#define LCD_DC_PIN      GPIO_PIN_4
#define LCD_RST_PORT    GPIOA
#define LCD_RST_PIN     GPIO_PIN_6

/* ----- 4. 屏幕物理分辨率 ----- */
#define LCD_WIDTH       240
#define LCD_HEIGHT      320

/* ================================================================== */
/*                       [ 以下无需修改 ]                              */
/* ================================================================== */

/* -------------------- 引脚控制宏 -------------------- */
#define LCD_CS_LOW()    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)
#define LCD_CS_HIGH()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#define LCD_DC_LOW()    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET)
#define LCD_DC_HIGH()   HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET)
#define LCD_RST_LOW()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
#define LCD_RST_HIGH()  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)

/* -------------------- 颜色定义 (RGB565) -------------------- */
#define WHITE         0xFFFF
#define BLACK         0x0000
#define BLUE          0x001F
#define BRED          0xF81F
#define GRED          0xFFE0
#define GBLUE         0x07FF
#define RED           0xF800
#define MAGENTA       0xF81F
#define GREEN         0x07E0
#define CYAN          0x7FFF
#define YELLOW        0xFFE0
#define BROWN         0xBC40
#define BRRED         0xFC07
#define GRAY          0x8430
#define DARKBLUE      0x01CF
#define LIGHTBLUE     0x7D7C
#define GRAYBLUE      0x5458
#define LIGHTGREEN    0x8410
#define LGRAY         0xC618
#define LGRAYBLUE     0xA651
#define LBBLUE        0x2B12

/* -------------------- 颜色合成宏 -------------------- */
#define LCD_RGB565(r, g, b)  ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/* -------------------- 方向定义 -------------------- */
#define LCD_DIR_HORIZONTAL  0
#define LCD_DIR_VERTICAL    1

/* ======================== API 声明 ======================== */

void LCD_Init(void);
void LCD_SetDirection(uint8_t dir);
void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_Clear(uint16_t color);
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_DrawRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color);
void LCD_FillCircle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color);
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color);
void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);
void LCD_ShowNum(uint16_t x, uint16_t y, int32_t num, uint8_t len, uint16_t color, uint16_t bg_color);
void LCD_Test(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H__ */
