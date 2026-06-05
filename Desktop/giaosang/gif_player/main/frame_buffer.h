#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H
#include <stdint.h>
#include <stdbool.h>
#include "tft_driver.h"

#define FB_PIXELS (TFT_WIDTH * TFT_HEIGHT)
#define FB_BYTES  (FB_PIXELS * 2)

typedef enum { FB_OK=0, FB_ERR_NO_PSRAM, FB_ERR_ALLOC } fb_result_t;

fb_result_t fb_init(void);
uint16_t   *fb_get_back(void);
uint16_t   *fb_get_front(void);
void        fb_swap(void);
uint16_t   *fb_get_prev(void);
void        fb_deinit(void);
bool        fb_has_psram(void);
#endif
