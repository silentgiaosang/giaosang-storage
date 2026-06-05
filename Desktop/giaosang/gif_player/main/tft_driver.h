#ifndef TFT_DRIVER_H
#define TFT_DRIVER_H
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define PIN_MOSI    GPIO_NUM_11
#define PIN_SCLK    GPIO_NUM_12
#define PIN_CS      GPIO_NUM_10
#define PIN_DC      GPIO_NUM_4
#define PIN_RST     GPIO_NUM_5
#define PIN_BL      GPIO_NUM_6
#define TFT_WIDTH   128
#define TFT_HEIGHT  160
#define SPI_CLOCK_HZ (18 * 1000 * 1000)

esp_err_t tft_init(void);
void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
esp_err_t tft_send_frame(const uint16_t *frame);
void tft_set_backlight(uint8_t brightness);
void tft_fill_screen(uint16_t color);
#endif
