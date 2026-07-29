#ifndef __MEASURE_H__
#define __MEASURE_H__

#include "ad9220.h"
#include "app_interface.h"

/* --- Wave type --- */
typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_HARMONIC,
} WaveType_t;

/* --- Peak result --- */
typedef struct {
    uint8_t count;          // 1~3
    float   freq_hz[3];     // Hz, ascending
    float   vpp_mv[3];      // mV
} PeakResult_t;

/* --- Public API --- */
void Measure_Init(void);
void Measure_Process(void);                     /* call from main loop; auto-timed */

uint8_t Measure_DataReady(void);                /* returns 1 when new data available, auto-clears */
void Measure_GetLatest(MeasureResult_t *result, PeakResult_t *peaks, WaveType_t *type);

/* Internal globals exposed for debug print only */
extern AD9220_Result g_result;
extern PeakResult_t  g_peaks;

#endif
