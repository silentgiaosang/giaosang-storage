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

#endif
