/**
 * TJC8048X270 串口屏驱动
 * 周期信号测量分析装置 (2026电赛G题)
 */

#ifndef __TJC_SCREEN_H
#define __TJC_SCREEN_H

#include "main.h"
#include "app_interface.h"
#include <stdint.h>

/* ---- 屏幕尺寸 ---- */
#define TJC_SCREEN_W  800
#define TJC_SCREEN_H  480

/* ---- s0 曲线控件位置 ---- */
#define GRAPH_X  10
#define GRAPH_Y  8
#define GRAPH_W  600
#define GRAPH_H  460

/* ---- HMI 控件名 ---- */
#define HMI_CURVE    "s0.id"
#define HMI_STATUS   "t_status"
#define HMI_VPP      "t_vpp"
#define HMI_VRMS     "t_vrms"
#define HMI_F1       "t_f1"
#define HMI_U1       "t_u1"
#define HMI_F2       "t_f2"
#define HMI_U2       "t_u2"
#define HMI_F3       "t_f3"
#define HMI_U3       "t_u3"
#define HMI_BTN_WAVE "b_wave"
#define HMI_BTN_SPEC "b_spec"
#define HMI_BTN_CYC1 "b_cyc1"
#define HMI_BTN_CYC3 "b_cyc3"
#define HMI_BTN_START "b_start"

#define PAGE_MAIN    0

/* ---- API ---- */
void TJC_Init(UART_HandleTypeDef *huart);
void TJC_PageMain(void);
void TJC_SetStatus(const char *fmt, ...);
void TJC_UpdateParams(const MeasureResult_t *r);
void TJC_ClearGraph(void);
void TJC_DrawWaveform(const uint16_t *data, uint16_t len);
void TJC_DrawSpectrum(const float *freqs, const uint16_t *amps, uint8_t count);
void TJC_BtnSetActive(const char *name, uint8_t active);
void TJC_SendCmd(const char *fmt, ...);
void TJC_SendBytes(const uint8_t *data, uint16_t len);

/* ---- MCU调 ---- */
void TJC_RxByteCallback(uint8_t byte);
void TJC_HandleTouch(uint8_t page, uint8_t ctrl_id, uint8_t value);
void App_Init(UART_HandleTypeDef *huart);
void App_Loop(void);

/* Busy flag: set during waveform send, checked in RX callback to prevent re-entry */
extern volatile uint8_t tjc_busy;

#endif
