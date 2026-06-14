/**
 ******************************************************************************
 * @file    oscilloscope.c
 * @brief   双通道示波器核心引擎
 *          - TIM2精确时基控制
 *          - DMA循环buffer管理(独立双通道CH0/CH1)
 *          - 软件触发检测(上升/下降沿, 可选CH0/CH1触发源)
 *          - 双通道波形提取 & 自动测量 & 波形有无判断
 ******************************************************************************
 */
#include "oscilloscope.h"
#include "adc.h"    /* hadc1 */
#include "dma.h"
#include "tim.h"    /* htim2 (CubeMX) */

/* ADC/DMA handles (adc.c内部定义) */
extern DMA_HandleTypeDef hdma_adc1;
extern ADC_HandleTypeDef hadc2;
extern DMA_HandleTypeDef hdma_adc2;

/* =========================== 全局实例 =========================== */
Oscilloscope_t g_osc;
volatile uint8_t g_enc_ui_dirty = 0;   /* 编码器ISR触发UI刷新 */
volatile uint8_t g_trig_adj_mode = 0;  /* 1=触发调节模式, SW2调电平 */
volatile uint8_t  g_calib_state = 0;    /* 0=空闲 1=校正中 2=校正完成 */
volatile uint32_t g_calib_start_ms = 0;

/* =========================== 时基参数表 =========================== */
/* TIM2时钟 = APB1_Timer = 84MHz (APB1=42MHz, Timer x2)              */
/* f_TIM2 = 84MHz / (PSC+1) / (ARR+1)                                */
/* 独立双ADC: 每TRGO→单通道1次转换=15 ADC周期                        */
/* ADC_CLK=21MHz, max_TRGO = 21M/15 = 1.4MHz (单ADC极限)             */
const TimebaseEntry_t g_tb_table[TB_NUM] = {
    /* TB_5US:   50us满屏,  1.4MHz*50us=70样本  需插值    */
    { 0,     59, 1400000, 1,  70, "5us/div"  },
    /* TB_10US:  100us满屏, 1.4MHz*100us=140样本 需插值   */
    { 0,     59, 1400000, 1, 140, "10us/div" },
    /* TB_20US:  200us满屏, 1.4MHz*200us=280样本 需抽取   */
    { 0,     59, 1400000, 1, 280, "20us/div" },
    /* TB_50US:  500us满屏, 480kHz*500us=240样本 直出     */
    { 0,    174,  480000, 1, 240, "50us/div" },
    /* TB_100US: 1ms满屏,   240kHz*1ms=240样本 直出       */
    { 0,    349,  240000, 1, 240, "100us/div"},
    /* TB_200US: 2ms满屏,   120kHz*2ms=240样本 直出       */
    { 0,    699,  120000, 1, 240, "200us/div"},
    /* TB_500US: 5ms满屏,   48kHz*5ms=240样本 直出        */
    { 0,   1749,   48000, 1, 240, "500us/div"},
    /* TB_1MS:   10ms满屏,  24kHz*10ms=240样本 直出       */
    { 0,   3499,   24000, 1, 240, "1ms/div" },
    /* TB_2MS:   20ms满屏,  12kHz*20ms=240样本 直出       */
    { 0,   6999,   12000, 1, 240, "2ms/div" },
    /* TB_5MS:   50ms满屏,  4.8kHz*50ms=240样本 直出      */
    { 0,  17499,    4800, 1, 240, "5ms/div" },
    /* TB_10MS:  100ms满屏, 2.4kHz*100ms=240样本 直出     */
    { 0,  34999,    2400, 1, 240, "10ms/div"},
    /* TB_20MS:  200ms满屏, 1.2kHz*200ms=240样本 直出     */
    { 3,  17499,    1200, 1, 240, "20ms/div"},
    /* TB_200MS: 2s满屏,   120Hz*2s=240样本 直出          */
    { 17499,    39,     120, 1, 240, "200ms/div"},
};

/* =========================== V/div参数表 =========================== */
/* adc_span: 满屏(10div)对应的ADC跨度, 用于Y轴缩放                    */
const VScaleEntry_t g_vscale_table[VSCALE_NUM] = {
    {  124, "10mV"  },     /* 满屏=0.1V (10div×10mV)                  */
    { 1241, "100mV" },     /* 满屏=1.0V (10div×100mV)                */
    {12409, "1V"    },     /* 满屏=10V  (软件放大, ADC 3.3V→3.3格)   */
};

uint16_t Osc_GetAdcSpan(void)
{
    return g_vscale_table[g_osc.vdiv].adc_span;
}

void Osc_SetVScale(OscVScale_t vs)
{
    if (vs >= VSCALE_NUM) return;
    g_osc.vdiv = vs;
}

void Osc_ToggleCoupling(uint8_t ch)
{
    if (ch < 2) g_osc.coupling_dc[ch] = !g_osc.coupling_dc[ch];
}

/* =========================== TIM2时基更新 =========================== */
static void Osc_TIM2_UpdateTimebase(uint16_t psc, uint32_t arr)
{
    HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_PRESCALER(&htim2, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    TIM2->EGR = TIM_EGR_UG;
    HAL_TIM_Base_Start(&htim2);
}

/* =========================== ADC + DMA 控制 =========================== */
static void Osc_ADC_DMA_Start(void)
{
    if (HAL_ADC_Start_DMA(&hadc1,
                          (uint32_t *)g_osc.adc_buf[0],
                          OSC_ADC_BUF_SIZE) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_ADC_Start_DMA(&hadc2,
                          (uint32_t *)g_osc.adc_buf[1],
                          OSC_ADC_BUF_SIZE) != HAL_OK)
    {
        HAL_ADC_Stop_DMA(&hadc1);
        Error_Handler();
    }
    g_osc.running = 1;
}

static void Osc_ADC_DMA_Stop(void)
{
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_ADC_Stop_DMA(&hadc2);
    g_osc.running = 0;
}

/* =========================== DMA 当前位置获取 =========================== */
static inline uint32_t Osc_DMA_GetWritePos(void)
{
    uint32_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_adc1);
    if (ndtr == 0 || ndtr > OSC_ADC_BUF_SIZE) ndtr = OSC_ADC_BUF_SIZE;
    return (OSC_ADC_BUF_SIZE - ndtr) % OSC_ADC_BUF_SIZE;
}

/* =========================== 触发检测 =========================== */
/**
 * @brief 在选定触发通道的DMA缓冲区中扫描触发事件
 * @param start_pos  扫描起点(DMA buffer偏移, 0..2047)
 * @param len        扫描长度
 * @return 触发点偏移, 0xFFFFFFFF=未找到
 * @note   独立双ADC, 按g_osc.trig_channel选择触发源
 */
static uint32_t Osc_ScanTrigger(uint32_t start_pos, uint32_t len)
{
    if (len < 2) return 0xFFFFFFFF;  /* 至少2个样本 */

    uint16_t *buf = g_osc.adc_buf[g_osc.trig_channel];

    uint16_t prev = buf[start_pos % OSC_ADC_BUF_SIZE];
    uint32_t p = (start_pos + 1) % OSC_ADC_BUF_SIZE;

    for (uint32_t i = 1; i < len; i++)
    {
        uint16_t curr = buf[p];

        if (g_osc.trig_edge == EDGE_RISING)
        {
            if (prev < g_osc.trig_level && curr >= g_osc.trig_level)
                return p;
        }
        else
        {
            if (prev > g_osc.trig_level && curr <= g_osc.trig_level)
                return p;
        }
        prev = curr;
        p = (p + 1) % OSC_ADC_BUF_SIZE;
    }
    return 0xFFFFFFFF;
}


/* =========================== 波形提取(双通道) =========================== */
/**
 * @brief 从独立双通道DMA buffer提取显示波形(时间对齐)
 * @note  用 sps(满屏样本数)做插值/抽取, 保证240像素=10div屏幕时间
 */
void Osc_Capture(void)
{
    uint32_t write_pos = Osc_DMA_GetWritePos();
    uint16_t sps = g_tb_table[g_osc.timebase].sps;
    float    step = (float)sps / (float)OSC_DISP_WIDTH;

    /* ---- 扫描新数据区寻找触发 ---- */
    uint32_t new_len;
    if (write_pos >= g_osc.dma_last_pos)
        new_len = write_pos - g_osc.dma_last_pos;
    else
        new_len = OSC_ADC_BUF_SIZE - g_osc.dma_last_pos + write_pos;

    if (new_len > OSC_ADC_BUF_SIZE / 2) new_len = OSC_ADC_BUF_SIZE / 2;

    uint32_t trig_found_pos = Osc_ScanTrigger(g_osc.dma_last_pos, new_len);
    g_osc.dma_last_pos = write_pos;

    /* 选择起始位置(ADC样本空间) */
    uint32_t start_adc;
    if (trig_found_pos != 0xFFFFFFFF)
    {
        g_osc.trig_found = 1;
        g_osc.trig_pos   = trig_found_pos;
        uint32_t pre_samp = (uint32_t)((float)OSC_PRE_TRIG * step + 0.5f);
        start_adc = (trig_found_pos + OSC_ADC_BUF_SIZE - pre_samp) % OSC_ADC_BUF_SIZE;
    }
    else if (g_osc.trig_mode == TRIG_AUTO)
    {
        g_osc.trig_timeout++;
        if (g_osc.trig_timeout > 50)
        {
            g_osc.trig_found  = 0;
            g_osc.trig_timeout = 0;
            /* 取最新满屏sps个样本, 映射到240像素 */
            start_adc = (write_pos + OSC_ADC_BUF_SIZE - sps) % OSC_ADC_BUF_SIZE;
        }
        else return;
    }
    else return;

    /* ---- 提取双通道: 插值/抽取到240像素 ---- */
    uint16_t *buf0 = g_osc.adc_buf[OSC_CH0];
    uint16_t *buf1 = g_osc.adc_buf[OSC_CH1];

    if (sps == OSC_DISP_WIDTH)
    {
        /* 直出: 240样本→240像素, 1:1映射 */
        for (uint32_t i = 0; i < OSC_DISP_WIDTH; i++)
        {
            uint32_t idx = (start_adc + i) % OSC_ADC_BUF_SIZE;
            g_osc.disp_buf[OSC_CH0][i] = buf0[idx];
            g_osc.disp_buf[OSC_CH1][i] = buf1[idx];
        }
    }
    else if (step >= 1.0f)
    {
        /* 抽取: >240样本→240像素, 取最近样本 */
        for (uint32_t i = 0; i < OSC_DISP_WIDTH; i++)
        {
            uint32_t adc_off = (uint32_t)((float)i * step + 0.5f);
            uint32_t idx = (start_adc + adc_off) % OSC_ADC_BUF_SIZE;
            g_osc.disp_buf[OSC_CH0][i] = buf0[idx];
            g_osc.disp_buf[OSC_CH1][i] = buf1[idx];
        }
    }
    else
    {
        /* 插值: <240样本→240像素, 线性插值 */
        float adc_pos = 0.0f;
        for (uint32_t i = 0; i < OSC_DISP_WIDTH; i++)
        {
            uint32_t base = (uint32_t)adc_pos;
            float    frac = adc_pos - (float)base;
            uint32_t idx0 = (start_adc + base)     % OSC_ADC_BUF_SIZE;
            uint32_t idx1 = (start_adc + base + 1) % OSC_ADC_BUF_SIZE;

            g_osc.disp_buf[OSC_CH0][i] = (uint16_t)(
                (float)buf0[idx0] * (1.0f - frac) + (float)buf0[idx1] * frac);
            g_osc.disp_buf[OSC_CH1][i] = (uint16_t)(
                (float)buf1[idx0] * (1.0f - frac) + (float)buf1[idx1] * frac);

            adc_pos += step;
        }
    }

    if (g_osc.trig_mode == TRIG_SINGLE && g_osc.trig_found)
    {
        g_osc.trig_armed = 1;
        Osc_ADC_DMA_Stop();
    }
}
/* =========================== 自动测量(双通道) =========================== */
void Osc_DoMeasurements(void)
{
    /* ---- 逐通道幅度测量 ---- */
    for (uint8_t ch = 0; ch < 2; ch++)
    {
        OscMeasure_t *m = &g_osc.measure[ch];
        uint16_t vmin = 4095, vmax = 0;
        uint32_t sum = 0;

        for (uint32_t i = 0; i < OSC_DISP_WIDTH; i++)
        {
            uint16_t v = g_osc.disp_buf[ch][i];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
            sum += v;
        }

        m->vmin = vmin * 3.3f / 4096.0f;
        m->vmax = vmax * 3.3f / 4096.0f;
        m->vpp  = m->vmax - m->vmin;
        m->vavg = (sum / OSC_DISP_WIDTH) * 3.3f / 4096.0f;

        /* 波形有无判断: Vpp > adc_span的10% (约0.6div) */
        uint16_t threshold = g_vscale_table[g_osc.vdiv].adc_span / 10;
        g_osc.wave_present[ch] = ((vmax - vmin) > threshold) ? 1 : 0;
    }

    /* ---- CH0频率/周期/占空比 (基于adc_buf[0], 线性扫描) ---- */
    {
        OscMeasure_t *m = &g_osc.measure[OSC_CH0];
        uint16_t vmin_adc = (uint16_t)(m->vmin * 4096.0f / 3.3f);
        uint16_t vmax_adc = (uint16_t)(m->vmax * 4096.0f / 3.3f);
        uint16_t mid_level = (vmin_adc + vmax_adc) / 2;

        uint32_t scan_len = 2048;
        uint32_t trig_pos = g_osc.trig_found ? g_osc.trig_pos
                           : Osc_DMA_GetWritePos();
        uint32_t cross_count = 0;
        uint32_t first_cross = 0, last_cross = 0;
        uint8_t  first_found = 0;

        uint16_t *buf = g_osc.adc_buf[OSC_CH0];
        for (uint32_t i = 1; i < scan_len; i++)
        {
            uint32_t idx_prev = (trig_pos + i - 1) % OSC_ADC_BUF_SIZE;
            uint32_t idx_curr = (trig_pos + i)     % OSC_ADC_BUF_SIZE;

            if (buf[idx_prev] < mid_level && buf[idx_curr] >= mid_level)
            {
                if (!first_found)
                {
                    first_cross = i;
                    first_found = 1;
                }
                else
                {
                    last_cross = i;
                    cross_count++;
                }
            }
        }

        if (cross_count >= 2 && first_found)
        {
            float sample_hz_actual = (float)Osc_GetCurrentSampleHz();
            float sample_interval  = 1.0f / sample_hz_actual;
            float period_samples   = (float)(last_cross - first_cross)
                                     / (float)cross_count;
            float raw_period = period_samples * sample_interval;
            float raw_freq   = 1.0f / raw_period;

            if (!m->ema_inited)
            {
                m->ema_freq   = raw_freq;
                m->ema_inited = 1;
            }
            else
            {
                m->ema_freq = m->ema_freq * 0.85f + raw_freq * 0.15f;
            }
            m->freq   = m->ema_freq;
            m->period = (m->ema_freq > 0.0f) ? (1.0f / m->ema_freq) : 0.0f;
            m->lost_frames = 0;

            /* 占空比 */
            uint32_t high_count = 0;
            for (uint32_t i = 0; i < scan_len; i++)
            {
                uint32_t idx = (trig_pos + i) % OSC_ADC_BUF_SIZE;
                if (buf[idx] > mid_level) high_count++;
            }
            m->duty = (float)high_count / (float)scan_len * 100.0f;
        }
        else
        {
            m->lost_frames++;
            if (m->lost_frames > 10)
            {
                m->ema_inited = 0;
                m->ema_freq   = 0.0f;
            }
            m->freq   = 0.0f;
            m->period = 0.0f;
            m->duty   = 0.0f;
        }
    }

    /* ---- CH1 频率测量(基于adc_buf[1], 独立线性扫描) ---- */
    {
        OscMeasure_t *m = &g_osc.measure[OSC_CH1];
        if (!g_osc.wave_present[OSC_CH1])
        {
            m->freq = 0.0f; m->period = 0.0f; m->duty = 0.0f;
            m->ema_inited = 0; m->ema_freq = 0.0f; m->lost_frames = 0;
        }
        else
        {
            uint16_t vmin_adc = (uint16_t)(m->vmin * 4096.0f / 3.3f);
            uint16_t vmax_adc = (uint16_t)(m->vmax * 4096.0f / 3.3f);
            uint16_t mid_level = (vmin_adc + vmax_adc) / 2;

            uint32_t scan_len = 2048;
            uint32_t trig_pos = Osc_DMA_GetWritePos();
            uint32_t cross_count = 0;
            uint32_t first_cross = 0, last_cross = 0;
            uint8_t  first_found = 0;

            uint16_t *buf = g_osc.adc_buf[OSC_CH1];
            for (uint32_t i = 1; i < scan_len; i++)
            {
                uint32_t idx_prev = (trig_pos + i - 1) % OSC_ADC_BUF_SIZE;
                uint32_t idx_curr = (trig_pos + i)     % OSC_ADC_BUF_SIZE;

                if (buf[idx_prev] < mid_level && buf[idx_curr] >= mid_level)
                {
                    if (!first_found) { first_cross = i; first_found = 1; }
                    else { last_cross = i; cross_count++; }
                }
            }

            if (cross_count >= 2 && first_found)
            {
                float sample_hz_actual = (float)Osc_GetCurrentSampleHz();
                float period_samples   = (float)(last_cross - first_cross)
                                         / (float)cross_count;
                float raw_period = period_samples / sample_hz_actual;
                float raw_freq   = 1.0f / raw_period;

                if (!m->ema_inited) { m->ema_freq = raw_freq; m->ema_inited = 1; }
                else { m->ema_freq = m->ema_freq * 0.85f + raw_freq * 0.15f; }
                m->freq = m->ema_freq;
                m->period = (m->ema_freq > 0.0f) ? (1.0f / m->ema_freq) : 0.0f;
                m->lost_frames = 0;

                uint32_t high_count = 0;
                for (uint32_t i = 0; i < scan_len; i++)
                {
                    uint32_t idx = (trig_pos + i) % OSC_ADC_BUF_SIZE;
                    if (buf[idx] > mid_level) high_count++;
                }
                m->duty = (float)high_count / (float)scan_len * 100.0f;
            }
            else
            {
                m->lost_frames++;
                if (m->lost_frames > 10) { m->ema_inited = 0; m->ema_freq = 0.0f; }
                m->freq = 0.0f; m->period = 0.0f; m->duty = 0.0f;
            }
        }
    }
}

/* =========================== 公开API实现 =========================== */
void Osc_Init(void)
{
    g_osc.timebase    = TB_100US;
    g_osc.trig_mode    = TRIG_AUTO;
    g_osc.trig_edge    = EDGE_RISING;
    g_osc.trig_level   = 2048;
    g_osc.trig_channel = OSC_CH1;       /* 默认CH1触发 */
    g_osc.disp_mode    = DISP_WAVEFORM;
    g_osc.dma_last_pos = 0;
    g_osc.trig_found   = 0;
    g_osc.trig_timeout = 0;
    g_osc.trig_armed   = 0;
    g_osc.tb_dirty     = 0;
    g_osc.running     = 0;
    g_osc.fft_sample_rate = 0.0f;
    g_osc.wave_type       = WAVE_UNKNOWN;
    g_osc.fft_channel     = 0;  /* FFT默认CH0 */
    g_osc.auto_tb         = 1;
    g_osc.last_manual_tb  = TB_100US;
    g_osc.vdiv            = VSCALE_1V;    /* 默认1V/div */
    g_osc.coupling_dc[0]  = 1;            /* CH0默认DC耦合 */
    g_osc.coupling_dc[1]  = 1;            /* CH1默认DC耦合 */
    g_osc.custom_psc      = 0;
    g_osc.custom_arr      = 349;
    g_osc.custom_sample_hz= 240000;

    /* 波形有无: 默认无 */
    g_osc.wave_present[OSC_CH0] = 0;
    g_osc.wave_present[OSC_CH1] = 0;

    /* 清空缓冲区 */
    for (uint32_t i = 0; i < OSC_ADC_BUF_SIZE; i++)
    {
        g_osc.adc_buf[OSC_CH0][i] = 0;
        g_osc.adc_buf[OSC_CH1][i] = 0;
    }
    for (uint32_t i = 0; i < OSC_DISP_WIDTH; i++)
    {
        g_osc.disp_buf[OSC_CH0][i] = 0;
        g_osc.disp_buf[OSC_CH1][i] = 0;
    }

    /* 初始化测量EMA状态 */
    for (uint8_t ch = 0; ch < 2; ch++)
    {
        g_osc.measure[ch].ema_inited  = 0;
        g_osc.measure[ch].ema_freq    = 0.0f;
        g_osc.measure[ch].lost_frames = 0;
        g_osc.measure[ch].freq        = 0.0f;
        g_osc.measure[ch].period      = 0.0f;
        g_osc.measure[ch].duty        = 0.0f;
    }

    /* TIM2时基启动 */
    const TimebaseEntry_t *tb = &g_tb_table[g_osc.timebase];
    __HAL_TIM_SET_PRESCALER(&htim2, tb->psc);
    __HAL_TIM_SET_AUTORELOAD(&htim2, tb->arr);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    TIM2->EGR = TIM_EGR_UG;
    HAL_TIM_Base_Start(&htim2);
}

void Osc_Start(void)
{
    g_osc.trig_armed = 0;
    g_osc.dma_last_pos = 0;
    Osc_ADC_DMA_Start();
}

void Osc_Stop(void)
{
    Osc_ADC_DMA_Stop();
}

void Osc_SetTimebase(OscTimebase_t tb)
{
    if (tb >= TB_NUM) return;
    g_osc.timebase = tb;
    g_osc.tb_dirty = 1;
    g_osc.auto_tb = 0;
    g_osc.last_manual_tb = tb;

    const TimebaseEntry_t *entry = &g_tb_table[tb];
    Osc_TIM2_UpdateTimebase(entry->psc, entry->arr);

    g_osc.custom_psc       = entry->psc;
    g_osc.custom_arr       = entry->arr;
    g_osc.custom_sample_hz = entry->sample_hz;
    g_osc.tb_dirty = 0;
}

void Osc_SetTrigMode(OscTrigMode_t mode)
{
    g_osc.trig_mode   = mode;
    g_osc.trig_timeout = 0;
    if (mode == TRIG_SINGLE)
    {
        g_osc.trig_armed = 0;
        if (!g_osc.running) Osc_Start();
    }
}

void Osc_SetTrigEdge(OscTrigEdge_t edge)
{
    g_osc.trig_edge = edge;
}

void Osc_SetTrigLevel(uint16_t level)
{
    if (level > 4095) level = 4095;
    g_osc.trig_level = level;
}

void Osc_SwitchDispMode(void)
{
    g_osc.disp_mode = (g_osc.disp_mode == DISP_WAVEFORM)
                      ? DISP_FFT : DISP_WAVEFORM;
}

uint32_t Osc_GetCurrentSampleHz(void)
{
    return g_osc.custom_sample_hz;
}

/* =========================== 自动时基(4周期显示, CH0驱动) ================ */
void Osc_AutoTimebase(void)
{
    if (!g_osc.auto_tb) return;
    if (g_osc.measure[OSC_CH0].freq <= 0.0f) return;

    float target_hz = g_osc.measure[OSC_CH0].freq * (float)OSC_DISP_WIDTH / 4.0f;

    /* 钳位到表内最大/最小采样率 */
    float max_hz = (float)g_tb_table[TB_5US].sample_hz;
    float min_hz = (float)g_tb_table[TB_200MS].sample_hz;
    if (target_hz > max_hz) target_hz = max_hz;
    if (target_hz < min_hz)  target_hz = min_hz;

    /* 10% 滞回: 避免频繁跳档 */
    float current_hz = (float)g_osc.custom_sample_hz;
    float ratio = (current_hz > target_hz) ? (current_hz / target_hz) : (target_hz / current_hz);
    if (ratio < 1.10f) return;

    /* 选最接近目标采样率的表内档位 */
    OscTimebase_t best = TB_100US;
    float best_diff = 1e12f;
    for (int i = 0; i < TB_NUM; i++)
    {
        float hz = (float)g_tb_table[i].sample_hz;
        float diff = (hz > target_hz) ? (hz - target_hz) : (target_hz - hz);
        if (diff < best_diff) { best_diff = diff; best = (OscTimebase_t)i; }
    }

    /* 未变化则跳过 */
    if (best == g_osc.timebase) return;

    Osc_SetTimebase(best);
    g_osc.auto_tb = 1;   /* Osc_SetTimebase会清零, 恢复AUTO标志 */
}

/* =========================== 编码器时基细调(单步走档) =============== */
void Osc_TimebaseFineTune(int8_t dir)
{
    g_osc.auto_tb = 0;   /* 手动模式 */

    if (dir > 0 && g_osc.timebase < TB_200MS)
        Osc_SetTimebase((OscTimebase_t)(g_osc.timebase + 1));
    else if (dir < 0 && g_osc.timebase > TB_5US)
        Osc_SetTimebase((OscTimebase_t)(g_osc.timebase - 1));
}

/* =========================== AUTO一键自动设置 =========================== */
void Osc_AutoSet(void)
{
    uint32_t write_pos = Osc_DMA_GetWritePos();
    uint32_t scan_len = 2048;

    uint32_t start_pos;
    if (write_pos >= scan_len)
        start_pos = write_pos - scan_len;
    else
        start_pos = OSC_ADC_BUF_SIZE - (scan_len - write_pos);

    /* CH0幅度分析(线性扫描adc_buf[0]) */
    uint16_t vmin = 4095, vmax = 0;
    uint16_t *buf = g_osc.adc_buf[OSC_CH0];
    for (uint32_t i = 0; i < scan_len; i++)
    {
        uint32_t idx = (start_pos + i) % OSC_ADC_BUF_SIZE;
        uint16_t v = buf[idx];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }

    uint16_t mid = (vmin + vmax) / 2;
    Osc_SetTrigLevel(mid);

    if ((vmax - vmin) < 250)
    {
        Osc_SetTrigMode(TRIG_AUTO);
        Osc_SetTrigEdge(EDGE_RISING);
        if (g_osc.disp_mode != DISP_WAVEFORM)
            Osc_SwitchDispMode();
        return;
    }

    /* 频率估计(上升沿过零, CH0线性扫描) */
    uint32_t cross_count = 0;
    uint32_t first_cross = 0, last_cross = 0;
    uint8_t  cross_found = 0;

    for (uint32_t i = 1; i < scan_len; i++)
    {
        uint32_t idx_prev = (start_pos + i - 1) % OSC_ADC_BUF_SIZE;
        uint32_t idx_curr = (start_pos + i)     % OSC_ADC_BUF_SIZE;
        uint16_t prev = buf[idx_prev];
        uint16_t curr = buf[idx_curr];

        if (prev < mid && curr >= mid)
        {
            if (!cross_found) { first_cross = i; cross_found = 1; }
            else { last_cross = i; cross_count++; }
        }
    }

    if (cross_count >= 2 && cross_found)
    {
        float cur_sample_rate    = (float)Osc_GetCurrentSampleHz();
        float avg_period_samples = (float)(last_cross - first_cross)
                                   / (float)cross_count;
        float signal_period = avg_period_samples / cur_sample_rate;
        float ideal_screen_s = signal_period * 4.0f;  /* 满屏4周期 */
        float ideal_tb_s     = ideal_screen_s / 10.0f;

        const float tb_vals[TB_NUM] = {
            5e-6f, 10e-6f, 20e-6f, 50e-6f,
            100e-6f, 200e-6f, 500e-6f,
            1e-3f, 2e-3f, 5e-3f, 10e-3f, 20e-3f, 200e-3f
        };

        OscTimebase_t best_tb = TB_100US;
        float min_diff = 1e6f;
        for (int i = 0; i < TB_NUM; i++)
        {
            float diff = (ideal_tb_s > tb_vals[i])
                         ? (ideal_tb_s - tb_vals[i])
                         : (tb_vals[i] - ideal_tb_s);
            if (diff < min_diff) { min_diff = diff; best_tb = (OscTimebase_t)i; }
        }
        Osc_SetTimebase(best_tb);
    }

    Osc_SetTrigMode(TRIG_AUTO);
    Osc_SetTrigEdge(EDGE_RISING);
    g_osc.auto_tb = 1;

    if (g_osc.disp_mode != DISP_WAVEFORM)
        Osc_SwitchDispMode();
}
