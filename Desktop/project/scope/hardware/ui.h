/**
 ******************************************************************************
 * @file    ui.h
 * @brief   双通道示波器UI渲染 (240x320 ST7789V LCD)
 *          CH0: Y=0..134 (黄色波形), CH1: Y=135..269 (绿色波形)
 *          状态栏: Y=270..319
 ******************************************************************************
 */
#ifndef __OSC_UI_H__
#define __OSC_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "oscilloscope.h"
#include "fft.h"
#include <stdint.h>

/* =========================== 布局常量 =========================== */
#define UI_CH_HEIGHT        135     /* 每通道波形区高度(pixels)   */
#define UI_CH0_TOP          0       /* CH0顶部Y                   */
#define UI_CH0_BOTTOM       134     /* CH0底部Y                   */
#define UI_CH1_TOP          135     /* CH1顶部Y                   */
#define UI_CH1_BOTTOM       269     /* CH1底部Y                   */
#define UI_STATUSBAR_TOP    270     /* 状态栏顶部Y                */
#define UI_STATUSBAR_BOTTOM 319     /* 状态栏底部Y                */
#define UI_GRID_DIVS        10      /* X方向格数                  */
#define UI_GRID_DIV_Y       5       /* Y方向格数(每27px一格)      */
#define UI_GRID_COLOR       0x4208  /* 网格颜色(暗灰)             */
#define UI_WAVE_COLOR_CH0   0xFFE0  /* CH0波形颜色(黄)           */
#define UI_WAVE_COLOR_CH1   0x07E0  /* CH1波形颜色(绿)           */
#define UI_TRIG_COLOR       0xF800  /* 触发标记颜色(红)           */
#define UI_BG_COLOR         0x0000  /* 背景(黑)                   */
#define UI_TEXT_COLOR       0xFFFF  /* 文字(白)                   */
#define UI_TEXT_CH0_COLOR   0xFFE0  /* CH0文字(黄)               */
#define UI_TEXT_CH1_COLOR   0x07E0  /* CH1文字(绿)               */
#define UI_FFT_COLOR        0x07E0  /* FFT频谱颜色(绿)            */
#define UI_FFT_PEAK_COLOR   0xF81F  /* FFT峰值颜色(品红)          */

/* =========================== API 声明 =========================== */
void UI_DrawGrids(void);
void UI_DrawWaveform(uint8_t ch, const uint16_t *disp_buf, uint32_t trig_pos,
                     uint8_t trig_found, uint8_t use_trigger);
void UI_DrawFFT(const FFTResult_t *fft_res);
void UI_DrawStatusBar(const Oscilloscope_t *osc);
void UI_ResetCache(void);
void UI_ResetStatusBar(void);
void UI_ClearWaveAreas(void);
void UI_ClearStatusBar(void);

#ifdef __cplusplus
}
#endif

#endif /* __OSC_UI_H__ */
