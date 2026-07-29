#ifndef __MEASURE_H__
#define __MEASURE_H__

#include "ad9220.h"

/* --- Peak result --- */
typedef struct {
    uint8_t count;          // 1~3
    float   freq_hz[3];     // 频率 Hz，升序
    float   vpp_mv[3];      // 幅值 mV
} PeakResult_t;

void Measure_Init(void);
void Measure_Process(void);
void Measure_Trigger(void);          /* 按钮触发立即测量 */

extern AD9220_Result g_result;
extern PeakResult_t  g_peaks;

#endif
