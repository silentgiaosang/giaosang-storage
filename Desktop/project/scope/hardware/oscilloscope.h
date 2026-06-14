/**
 ******************************************************************************
 * @file    oscilloscope.h
 * @brief   双通道数字示波器 - 核心数据结构与API
 * @note    STM32F407VET6, ADC1(PC4)+ADC2(PB1)独立双ADC, TIM2 TRGO同步触发,
 *          DMA2_Stream0/2循环, 240x320 ST7789V LCD
 ******************************************************************************
 */
#ifndef __OSCILLOSCOPE_H__
#define __OSCILLOSCOPE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include "fft.h"

/* =========================== 缓冲区定义 =========================== */
#define OSC_ADC_BUF_SIZE         2048U    /* 每通道DMA循环缓冲区 */
#define OSC_DISP_WIDTH           240U     /* 屏幕波形区宽度(像素)       */
#define OSC_PRE_TRIG             60U      /* 预触发点数(屏幕左起1/4)    */
#define OSC_CH0                  0        /* 通道0索引                   */
#define OSC_CH1                  1        /* 通道1索引                   */

/* =========================== 时基枚举 (12档) ================ */
typedef enum {
    TB_5US = 0,    /*   50us满屏, ADC极限1.4Msps (3-cycle)            */
    TB_10US,       /*  100us满屏, ADC极限1.4Msps                      */
    TB_20US,       /*  200us满屏, 641kHz                              */
    TB_50US,       /*  500us满屏, 256kHz                              */
    TB_100US,      /*    1ms满屏, 128kHz                              */
    TB_200US,      /*    2ms满屏, 64kHz                               */
    TB_500US,      /*    5ms满屏, 25.6kHz                             */
    TB_1MS,        /*   10ms满屏, 12.8kHz                             */
    TB_2MS,        /*   20ms满屏, 6.4kHz                              */
    TB_5MS,        /*   50ms满屏, 2.56kHz                             */
    TB_10MS,       /*  100ms满屏, 1.28kHz                             */
    TB_20MS,       /*  200ms满屏, 640Hz                               */
    TB_200MS,      /*    2s满屏, 120Hz                                */
    TB_NUM
} OscTimebase_t;

/* =========================== 触发模式 =========================== */
typedef enum {
    TRIG_AUTO = 0,   /* 自动: 无触发时自由运行                         */
    TRIG_NORMAL,     /* 正常: 等待触发事件                              */
    TRIG_SINGLE      /* 单次: 捕获一次后停止                            */
} OscTrigMode_t;

/* =========================== 触发边沿 =========================== */
typedef enum {
    EDGE_RISING = 0,
    EDGE_FALLING
} OscTrigEdge_t;

/* =========================== 显示模式 =========================== */
typedef enum {
    DISP_WAVEFORM = 0,  /* 波形视图                                    */
    DISP_FFT            /* FFT频谱视图                                  */
} OscDispMode_t;

/* =========================== 垂直灵敏度 =========================== */
typedef enum {
    VSCALE_10MV = 0,    /* 10mV/div                                      */
    VSCALE_100MV,       /* 100mV/div                                     */
    VSCALE_1V,          /* 1V/div                                        */
    VSCALE_NUM
} OscVScale_t;

/* =========================== 时基参数表条目 =========================== */
typedef struct {
    uint16_t psc;        /* TIM2 PSC                                    */
    uint32_t arr;        /* TIM2 ARR                                    */
    uint32_t sample_hz;  /* 实际采样率(Hz)                              */
    uint8_t  decimation; /* >1时需要软件抽取(仅快速档)                  */
    uint16_t sps;        /* 满屏(=10div)ADC样本数                       */
    char     label[12];  /* 显示标签: \"5us\" \"10us\" ...             */
} TimebaseEntry_t;

/* =========================== V/div表条目 =========================== */
typedef struct {
    uint16_t adc_span;   /* 满屏ADC跨度(0=自动=4095)                    */
    char     label[8];   /* 显示标签: \"5V\" \"1V\" ...                */
} VScaleEntry_t;

/* =========================== 通道测量结果(每通道独立) ================== */
typedef struct {
    float vpp;           /* 峰峰值(V)                                   */
    float vavg;          /* 平均值(V)                                   */
    float vmin;          /* 最小值(V)                                   */
    float vmax;          /* 最大值(V)                                   */
    float freq;          /* 频率(Hz), 0=无法测量                        */
    float period;        /* 周期(s)                                     */
    float duty;          /* 占空比(%)                                   */
    float ema_freq;      /* EMA平滑频率(内部用)                        */
    uint8_t ema_inited;  /* EMA是否已初始化                            */
    uint32_t lost_frames;/* 信号丢失连续帧计数                          */
} OscMeasure_t;

/* =========================== 示波器总控结构体 =========================== */
typedef struct {
    /* ---- 硬件缓冲区 ---- */
    uint16_t adc_buf[2][OSC_ADC_BUF_SIZE];       /* CH0/CH1独立DMA缓冲      */
    uint16_t disp_buf[2][OSC_DISP_WIDTH];     /* CH0/CH1显示缓存       */

    /* ---- 波形有无标志 ---- */
    uint8_t  wave_present[2];                 /* 1=检测到波形(CH0/CH1) */

    /* ---- DMA位置跟踪 ---- */
    uint32_t dma_last_pos;                    /* 上次读DMA位置          */

    /* ---- 触发状态 ---- */
    OscTrigMode_t trig_mode;                  /* 触发模式              */
    OscTrigEdge_t trig_edge;                  /* 触发边沿              */
    uint16_t      trig_level;                 /* 触发电平(ADC count)   */
    uint8_t       trig_channel;               /* 触发源(0=CH0, 1=CH1)  */
    uint8_t       trig_found;                 /* 1=本次找到触发点      */
    uint32_t      trig_pos;                   /* 触发点在DMA buffer偏移*/
    uint32_t      trig_timeout;               /* Auto模式超时计数器    */
    uint8_t       trig_armed;                 /* 单次模式已触发标志    */

    /* ---- 时基 ---- */
    OscTimebase_t timebase;                   /* 当前时基档位          */
    uint8_t       tb_dirty;                   /* 1=需要更新TIM2        */
    uint8_t       auto_tb;                    /* 1=自动时基(3周期显示) */
    OscTimebase_t last_manual_tb;             /* 最后手动选择的档位    */
    uint32_t      custom_arr;                 /* 自动模式ARR值         */
    uint16_t      custom_psc;                 /* 自动模式PSC值         */
    uint32_t      custom_sample_hz;           /* 自动模式采样率(Hz)    */

    /* ---- 显示 ---- */
    OscDispMode_t disp_mode;                  /* 当前显示模式          */
    OscVScale_t   vdiv;                       /* 垂直灵敏度(V/div)     */
    uint8_t       coupling_dc[2];             /* 1=DC耦合, 0=AC(CH0/CH1) */
    uint8_t       atten_enabled;              /* 1=×6衰减修正启用(仅1V档)  */

    /* ---- 测量(每通道独立) ---- */
    OscMeasure_t measure[2];                  /* CH0/CH1测量结果       */

    /* ---- FFT ---- */
    float   fft_in[FFT_SIZE * 2];              /* FFT输入缓冲(实部+虚部)  */
    float   fft_out[FFT_SIZE];                 /* FFT输出缓冲              */
    float   fft_sample_rate;                   /* FFT当前采样率(Hz)        */
    WaveType_t  wave_type;                     /* 波形类型(FFT检测结果)    */
    uint8_t     fft_channel;                   /* FFT数据通道(0=CH0,1=CH1) */

    /* ---- 运行 ---- */
    uint8_t running;                          /* 1=ADC+DMA正在运行     */
} Oscilloscope_t;

/* =========================== 全局实例 =========================== */
extern Oscilloscope_t g_osc;
extern const TimebaseEntry_t g_tb_table[TB_NUM];
extern const VScaleEntry_t  g_vscale_table[VSCALE_NUM];
extern volatile uint8_t g_enc_ui_dirty;   /* 编码器中断触发UI刷新 */
extern volatile uint8_t g_trig_adj_mode;  /* 1=触发调节模式, SW2调电平 */
extern volatile uint8_t  g_calib_state;   /* 0=空闲 1=校正中 2=校正完成 */
extern volatile uint32_t g_calib_start_ms;

/* =========================== API 声明 =========================== */
void Osc_Init(void);
void Osc_Start(void);
void Osc_Stop(void);
void Osc_SetTimebase(OscTimebase_t tb);
void Osc_SetTrigMode(OscTrigMode_t mode);
void Osc_SetTrigEdge(OscTrigEdge_t edge);
void Osc_SetTrigLevel(uint16_t level);
void Osc_SetVScale(OscVScale_t vs);
void Osc_ToggleCoupling(uint8_t ch);
void Osc_Capture(void);
void Osc_DoMeasurements(void);
void Osc_SwitchDispMode(void);
void Osc_AutoSet(void);
void Osc_AutoTimebase(void);
void Osc_TimebaseFineTune(int8_t dir);
uint32_t Osc_GetCurrentSampleHz(void);
uint16_t Osc_GetAdcSpan(void);
void     Osc_ToggleAtten(void);
uint8_t  Osc_IsAttenEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __OSCILLOSCOPE_H__ */
