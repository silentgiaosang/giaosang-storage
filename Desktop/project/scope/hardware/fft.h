/**
 ******************************************************************************
 * @file    fft.h
 * @brief   FFT频谱分析模块 (基于CMSIS-DSP arm_rfft_fast_f32)
 * @note    需要Keil RTE中添加CMSIS-DSP, 并在编译选项中定义 ARM_MATH_CM4
 ******************************************************************************
 */
#ifndef __FFT_H__
#define __FFT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ---- 基于CMSIS-DSP ---- */
#ifdef ARM_MATH_CM4
#include "arm_math.h"
#endif

#define FFT_SIZE            512U              /* FFT点数(必须是2的幂)    */
#define FFT_OUT_BINS        (FFT_SIZE / 2)    /* 输出频点数              */
#define FFT_EMA_ALPHA       0.25f             /* 幅度EMA平滑系数(0-1)   */
#define FFT_CH0             0                 /* 通道0 (ADC偶数索引)     */
#define FFT_CH1             1                 /* 通道1 (ADC奇数索引)     */

/* =========================== FFT结果结构体 =========================== */
typedef struct {
    float   mag[FFT_OUT_BINS];   /* 幅度值(dB)                         */
    float   max_mag;             /* 最大幅度(dB)                        */
    uint16_t peak_bin;           /* 峰值所在bin                         */
    float   peak_freq;           /* 峰值频率(Hz)                        */
} FFTResult_t;

/* =========================== 全局实例 =========================== */
extern FFTResult_t g_fft_result;

/* =========================== API 声明 =========================== */
int  FFT_Init(void);                                  /* 初始化FFT(分配twiddle表) */
void FFT_SetChannel(uint8_t ch);                      /* 选择FFT数据通道(CH0/CH1) */
void FFT_Process(const uint16_t *adc_buf, uint32_t buf_len,
                 uint32_t trigger_pos, float sample_rate); /* 执行FFT处理          */
void FFT_ApplyWindow(float *buf, uint32_t len);       /* 加Hanning窗              */
void FFT_ComputeMagnitude(const float *fft_out, uint32_t fft_size,
                          float *mag, float sample_rate,
                          float *max_mag, uint16_t *peak_bin, float *peak_freq);
                                                      /* 计算dB幅度+峰值检测      */

#ifdef __cplusplus
}
#endif

#endif /* __FFT_H__ */
