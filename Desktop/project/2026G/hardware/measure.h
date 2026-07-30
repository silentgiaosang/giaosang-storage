#ifndef __MEASURE_H__
#define __MEASURE_H__

#include "ad9220.h"

/* --- Wave type --- */
typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_MULTITONE,
} WaveType_t;

/* --- Peak result --- */
typedef struct {
    uint8_t count;          // 1~3
    float   freq_hz[3];     // Hz, ascending
    float   vpp_mv[3];      // mV
} PeakResult_t;

/* --- Public API --- */
void Measure_Init(void);
void Measure_Process(void);
void Measure_Trigger(void);

/* Internal globals exposed for debug */
extern AD9220_Result g_result;
extern PeakResult_t  g_peaks;

#endif
