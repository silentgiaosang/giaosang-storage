#ifndef __APP_INTERFACE_H
#define __APP_INTERFACE_H

#include <stdint.h>

/* ---- 模式 ---- */
typedef enum {
    MODE_WAVEFORM = 0,
    MODE_SPECTRUM = 1
} DispMode_t;

typedef enum {
    CYC_1 = 0,
    CYC_3 = 1
} Cycle_t;

/* ---- 填 ---- */
typedef struct {
    float vpp_mv;
    float vrms_mv;
    float f_base_hz;
    uint8_t harmonic_count;     // 1~3
    float freq_hz[3];           // 升序, [0]=基频
    float amp_mv[3];
} MeasureResult_t;

/* ---- 调 ---- */
uint8_t App_MeasureRequested(void);     // 用户按按钮 → 返回1
DispMode_t App_GetMode(void);           // 当前显示模式
Cycle_t App_GetCycle(void);             // 1T or 3T

void App_SubmitResult(MeasureResult_t *result,
                      const float *wave_data, uint16_t wave_len,
                      uint32_t sample_rate,
                      const float *fft_mag, uint16_t fft_len);

void App_ShowError(const char *msg);

#endif
