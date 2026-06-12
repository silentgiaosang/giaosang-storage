/**
 ******************************************************************************
 * @file    fft.c
 * @brief   FFT频谱分析 (基于CMSIS-DSP arm_rfft_fast_f32)
 * @note    流程: 加窗 → FFT → 幅度(dB) → 峰值检测
 *          需要Keil RTE添加CMSIS-DSP + 定义 ARM_MATH_CM4
 ******************************************************************************
 */
#include "fft.h"
#include "oscilloscope.h"
#include <math.h>

/* =========================== 全局实例 =========================== */
FFTResult_t g_fft_result;

/* =========================== 通道选择 & EMA平滑 =================== */
#ifdef ARM_MATH_CM4
static uint8_t  g_fft_channel = FFT_CH1;          /* 默认通道2(CH1)   */
static float    g_fft_ema_mag[FFT_OUT_BINS];       /* EMA平滑缓存      */
static uint8_t  g_fft_ema_inited = 0;
#endif

void FFT_SetChannel(uint8_t ch)
{
#ifdef ARM_MATH_CM4
    if (ch <= 1)
    {
        g_fft_channel = ch;
        g_fft_ema_inited = 0;  /* 切换通道时重置EMA */
    }
#else
    (void)ch;
#endif
}

/* =========================== CMSIS-DSP句柄 =========================== */
#ifdef ARM_MATH_CM4
static arm_rfft_fast_instance_f32 g_rfft_inst;
static uint8_t g_fft_initialized = 0;

int FFT_Init(void)
{
    arm_status status = arm_rfft_fast_init_f32(&g_rfft_inst, FFT_SIZE);
    if (status != ARM_MATH_SUCCESS)
    {
        return -1;
    }
    g_fft_initialized = 1;
    return 0;
}

/**
 * @brief 加Hanning窗
 * @param buf  输入数据(FLOAT格式, 长度=len)
 */
void FFT_ApplyWindow(float *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        float w = 0.5f * (1.0f - cosf(2.0f * 3.141592654f * i / (len - 1)));
        buf[i] *= w;
    }
}

/**
 * @brief 从ADC buffer提取FFT输入、加窗、执行FFT
 * @param adc_buf      ADC DMA原始buffer
 * @param buf_len      ADC buffer总长度
 * @param trigger_pos  触发点位置(以此为起点取FFT_SIZE个样本)
 * @param sample_rate  当前采样率(Hz)
 */
void FFT_Process(const uint16_t *adc_buf, uint32_t buf_len,
                 uint32_t trigger_pos, float sample_rate)
{
    if (!g_fft_initialized) return;

    float *fft_input = g_osc.fft_in;
    float *fft_out   = g_osc.fft_out;

    /* ---- 1. 从ADC buffer取FFT_SIZE个样本(按通道, 线性读取) ---- */
    const uint16_t (*buf)[OSC_ADC_BUF_SIZE] = (const uint16_t (*)[OSC_ADC_BUF_SIZE])adc_buf;
    uint32_t start = trigger_pos;
    for (uint32_t i = 0; i < FFT_SIZE; i++)
    {
        uint32_t idx = (start + i) % buf_len;
        uint16_t raw = buf[g_fft_channel][idx];
        fft_input[i] = (float)raw - 2048.0f;
    }

    /* ---- 2. 加窗 ---- */
    FFT_ApplyWindow(fft_input, FFT_SIZE);

    /* ---- 3. 执行RFFT ---- */
    /* arm_rfft_fast_f32: 输入是实部[0..N-1]在前半, 输出是频率域(交错复) */
    arm_rfft_fast_f32(&g_rfft_inst, fft_input, fft_out, 0);

    /* ---- 4. 计算幅度(dB) + 峰值检测 ---- */
    FFT_ComputeMagnitude(fft_out, FFT_SIZE,
                         g_fft_result.mag, sample_rate,
                         &g_fft_result.max_mag,
                         &g_fft_result.peak_bin,
                         &g_fft_result.peak_freq);
}

/**
 * @brief 计算各频点幅度(dB)并检测峰值
 * @param fft_out       arm_rfft_fast_f32输出(交错复数格式)
 * @param fft_size      FFT点数
 * @param mag[out]      幅度数组(dB), 长度=fft_size/2
 * @param sample_rate   采样率(Hz)
 * @param max_mag[out]  最大幅度(dB)
 * @param peak_bin[out] 峰值bin索引
 * @param peak_freq[out]峰值频率(Hz)
 */
void FFT_ComputeMagnitude(const float *fft_out, uint32_t fft_size,
                          float *mag, float sample_rate,
                          float *max_mag, uint16_t *peak_bin,
                          float *peak_freq)
{
    float max_val = -200.0f;
    uint16_t peak = 0;

    /* ===== CMSIS-DSP arm_rfft_fast_f32 输出格式(packed): =====
     * pOut[0] = DC real (bin 0)
     * pOut[1] = Nyquist real (bin N/2) — 不存入mag
     * pOut[2*k+0] = re[k], pOut[2*k+1] = im[k]  for k=1..N/2-1
     * mag数组存储: mag[0]=DC, mag[1..N/2-1]=bin 1..N/2-1
     */

    /* Bin 0 (DC): 只取实部 */
    float mag0 = fabsf(fft_out[0]);
    if (mag0 < 1e-9f) mag0 = 1e-9f;
    float raw0 = 20.0f * log10f(mag0 / (float)fft_size);
    if (!g_fft_ema_inited)
        mag[0] = raw0;
    else
        mag[0] = g_fft_ema_mag[0] * (1.0f - FFT_EMA_ALPHA) + raw0 * FFT_EMA_ALPHA;

    if (mag[0] > max_val) { max_val = mag[0]; peak = 0; }

    /* Bin 1..N/2-1 */
    for (uint32_t i = 1; i < fft_size / 2; i++)
    {
        float real = fft_out[2 * i];
        float imag = fft_out[2 * i + 1];
        float m = sqrtf(real * real + imag * imag);
        if (m < 1e-9f) m = 1e-9f;
        float raw = 20.0f * log10f(m / (float)fft_size);

        if (!g_fft_ema_inited)
            mag[i] = raw;
        else
            mag[i] = g_fft_ema_mag[i] * (1.0f - FFT_EMA_ALPHA) + raw * FFT_EMA_ALPHA;

        /* 峰值检测 (跳过DC bin 0, bin 1) */
        if (i >= 2 && mag[i] > max_val)
        {
            max_val = mag[i];
            peak    = (uint16_t)i;
        }
    }

    /* 保存EMA缓存 */
    for (uint32_t i = 0; i < fft_size / 2; i++)
        g_fft_ema_mag[i] = mag[i];
    g_fft_ema_inited = 1;

    *max_mag   = max_val;
    *peak_bin  = peak;
    *peak_freq = (float)peak * sample_rate / (float)fft_size;
}

/**
 * @brief 在FFT幅度谱中找到基频+3次+5次谐波
 * @param fft_res     FFT结果(含mag[]数组)
 * @param sample_rate 当前采样率(Hz)
 * @param harmonics[out] 输出的谐波峰值数组, 长度=FFT_MAX_HARMONICS
 * @note  只显示基频(f0)、3次谐波(3f0)、5次谐波(5f0)
 *        每个谐波窗口为 ±10%, 取窗口内幅度最高的局部极大值
 */
void FFT_FindHarmonics(const FFTResult_t *fft_res, float sample_rate,
                       HarmonicPeak_t *harmonics)
{
    float bin_hz = sample_rate / (float)FFT_SIZE;

    /* 初始化输出 */
    for (int i = 0; i < FFT_MAX_HARMONICS; i++)
        harmonics[i].valid = 0;

    if (sample_rate <= 0.0f) return;

    /* ---- 扫描局部极大值(跳过DC bin0 和 bin1) ---- */
    #define FFT_MAX_PEAKS 16
    float    pk_db[FFT_MAX_PEAKS];
    uint16_t pk_bin[FFT_MAX_PEAKS];
    uint8_t  pk_n = 0;

    for (uint32_t i = 3; i < FFT_OUT_BINS - 1 && pk_n < FFT_MAX_PEAKS; i++)
    {
        float db = fft_res->mag[i];
        if (db < -65.0f) continue;                     /* 低于绝对最小dB */
        if (db < fft_res->mag[i - 1]) continue;        /* 非局部极大  */
        if (db < fft_res->mag[i + 1]) continue;

        /* 按幅度降序插入 */
        int8_t pos = pk_n;
        while (pos > 0 && pk_db[pos - 1] < db) pos--;
        for (int8_t j = pk_n; j > pos; j--)
        {
            pk_db[j]  = pk_db[j - 1];
            pk_bin[j] = pk_bin[j - 1];
        }
        pk_db[pos]  = db;
        pk_bin[pos] = (uint16_t)i;
        pk_n++;
    }

    if (pk_n == 0) return;

    /* ---- 找基频: 20Hz~10kHz范围内幅度最高的局部极大值 ---- */
    float   f0     = 0.0f;
    float   f0_db  = -200.0f;
    uint8_t f0_idx = 0xFF;

    for (uint8_t i = 0; i < pk_n; i++)
    {
        float freq = (float)pk_bin[i] * bin_hz;
        if (freq >= 20.0f && freq <= 10000.0f && pk_db[i] > f0_db)
        {
            f0_db  = pk_db[i];
            f0     = freq;
            f0_idx = i;
        }
    }
    if (f0_idx == 0xFF) return;  /* 无有效基频 */

    /* ---- 填入3个谐波: 基频, 3次, 5次 ---- */
    float harm_target[3] = { f0, f0 * 3.0f, f0 * 5.0f };

    for (int h_idx = 0; h_idx < 3; h_idx++)
    {
        float target = harm_target[h_idx];
        float window_lo = target * 0.90f;
        float window_hi = target * 1.10f;

        /* 在该窗口内找幅度最高的局部极大值 */
        float   best_db  = -200.0f;
        float   best_f   = 0.0f;
        uint8_t best_idx = 0xFF;

        for (uint8_t i = 0; i < pk_n; i++)
        {
            float freq = (float)pk_bin[i] * bin_hz;
            if (freq >= window_lo && freq <= window_hi && pk_db[i] > best_db)
            {
                best_db  = pk_db[i];
                best_f   = freq;
                best_idx = i;
            }
        }

        if (best_idx != 0xFF)
        {
            harmonics[h_idx].valid = 1;
            harmonics[h_idx].db    = best_db;
            harmonics[h_idx].freq  = best_f;
            harmonics[h_idx].bin   = pk_bin[best_idx];
        }
    }

    #undef FFT_MAX_PEAKS
}

#else /* !ARM_MATH_CM4 — 无DSP时提供空实现 */

int FFT_Init(void)
{
    return -1;  /* DSP未启用 */
}

void FFT_Process(const uint16_t *adc_buf, uint32_t buf_len,
                 uint32_t trigger_pos, float sample_rate)
{
    (void)adc_buf; (void)buf_len; (void)trigger_pos; (void)sample_rate;
}

void FFT_ApplyWindow(float *buf, uint32_t len)
{
    (void)buf; (void)len;
}

void FFT_ComputeMagnitude(const float *fft_out, uint32_t fft_size,
                          float *mag, float sample_rate,
                          float *max_mag, uint16_t *peak_bin,
                          float *peak_freq)
{
    (void)fft_out; (void)fft_size; (void)mag; (void)sample_rate;
    (void)max_mag; (void)peak_bin; (void)peak_freq;
}

void FFT_FindHarmonics(const FFTResult_t *fft_res, float sample_rate,
                       HarmonicPeak_t *harmonics)
{
    (void)fft_res; (void)sample_rate; (void)harmonics;
}

#endif /* ARM_MATH_CM4 */
