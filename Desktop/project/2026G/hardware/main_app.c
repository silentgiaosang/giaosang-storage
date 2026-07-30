/**
 * 周期信号测量分析装置 - 串口屏主程序
 * 2026电赛G题
 */

#include "main.h"
#include "tjc_screen.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* 当前模式 */
static DispMode_t g_disp_mode = MODE_WAVEFORM;
static Cycle_t    g_cycle     = CYC_1;

/* 测量请求标志 (在主循环里轮询) */
volatile uint8_t g_measure_requested = 0;

/* 缓存上一次测量数据 (模式/周期切换时直接刷新, 无需重测) */
static MeasureResult_t g_cached_result;
static const float     *g_cached_wave = NULL;
static uint16_t         g_cached_wave_len = 0;
static uint32_t         g_cached_sample_rate = 0;
static uint8_t          g_has_data = 0;

static void RedrawWithCache(void)
{
    if (!g_has_data) return;
    App_SubmitResult(&g_cached_result,
                     g_cached_wave, g_cached_wave_len,
                     g_cached_sample_rate, NULL, 0);
}

/** 调这个: 是否有新的测量请求 */
uint8_t App_MeasureRequested(void)
{
    if (g_measure_requested) {
        g_measure_requested = 0;
        return 1;
    }
    return 0;
}

/* ================================================================
 * 触摸事件处理
 * (屏幕通过串口返回触摸事件, STM32在UART接收中断中解析)
 * ================================================================ */

/**
 * TJC 触摸事件格式 (屏幕→MCU):
 *   0x65 + page_id(1B) + ctrl_id(1B) + event(1B) + value(4B) + 0xFF 0xFF 0xFF
 *
 *   其中 event: 0x01=按下, 0x02=释放
 *   对于按钮: value=0
 *   对于单选按钮: value=选中项索引(0,1,2...)
 */

#define TJC_RX_BUF_SIZE 32
static uint8_t  tjc_rx_buf[TJC_RX_BUF_SIZE];
static uint8_t  tjc_rx_idx = 0;

/**
 * 在 USART 接收中断中调用, 每收到一个字节
 */
void TJC_RxByteCallback(uint8_t byte)
{
    // 检测帧尾: 连续3个 0xFF
    if (tjc_rx_idx >= 2 &&
        tjc_rx_buf[tjc_rx_idx-2] == 0xFF &&
        tjc_rx_buf[tjc_rx_idx-1] == 0xFF &&
        byte == 0xFF)
    {
        // 完整帧接收, 解析
        if (tjc_rx_idx >= 5 &&
            tjc_rx_buf[0] == 0x65)  // 0x65 是触摸事件标识
        {
            uint8_t page  = tjc_rx_buf[1];
            uint8_t ctrl  = tjc_rx_buf[2];  // 控件ID (需与HMI软件中ID对应)
            uint8_t event = tjc_rx_buf[3];
            uint8_t value = tjc_rx_buf[4];  // 简化: 单字节值

            // 只在按下事件时处理
            if (event == 0x01) {
                TJC_HandleTouch(page, ctrl, value);
            }
        }
        tjc_rx_idx = 0;
        return;
    }

    if (tjc_rx_idx < TJC_RX_BUF_SIZE) {
        tjc_rx_buf[tjc_rx_idx++] = byte;
    } else {
        tjc_rx_idx = 0;  // 溢出重置
    }
}

/**
 * 触摸事件分发处理
 *
 * 注意: ctrl 是 HMI 软件中控件的 ID 号(非名称),
 * 在 HMI 软件的"控件属性"中可查到每个控件的 ID
 */
/**
 * 切换显示模式 (波形/频谱)
 */
static void ToggleMode(DispMode_t mode)
{
    g_disp_mode = mode;
    TJC_BtnSetActive(HMI_BTN_WAVE, mode == MODE_WAVEFORM);
    TJC_BtnSetActive(HMI_BTN_SPEC, mode == MODE_SPECTRUM);
    RedrawWithCache();  // 用缓存数据直接切图, 不重测
}

static void ToggleCycle(Cycle_t cyc)
{
    g_cycle = cyc;
    TJC_BtnSetActive(HMI_BTN_CYC1, cyc == CYC_1);
    TJC_BtnSetActive(HMI_BTN_CYC3, cyc == CYC_3);
    RedrawWithCache();
}

void TJC_HandleTouch(uint8_t page, uint8_t ctrl_id, uint8_t value)
{
    if (page != PAGE_MAIN) return;
    (void)value;  // 按钮不传value

    // 根据控件ID判断是哪个按钮 (ID需在HMI软件中确认!)
    switch (ctrl_id) {
        case 0x0A:  // b_wave
            ToggleMode(MODE_WAVEFORM);
            break;
        case 0x0B:  // b_spec
            ToggleMode(MODE_SPECTRUM);
            break;
        case 0x0C:  // b_cyc1
            ToggleCycle(CYC_1);
            break;
        case 0x0D:  // b_cyc3
            ToggleCycle(CYC_3);
            break;
        case 0x0E:  // b_start
            g_measure_requested = 1;
            TJC_SetStatus("测量中...");
            break;
        default:
            break;
    }
}

/* ================================================================
 * 初始化
 * ================================================================ */

void App_Init(UART_HandleTypeDef *huart)
{
    // 初始化串口屏
    TJC_Init(huart);

    // 设置按钮初始状态: 波形+1周期为选中(蓝色)
    TJC_BtnSetActive(HMI_BTN_WAVE, 1);
    TJC_BtnSetActive(HMI_BTN_SPEC, 0);
    TJC_BtnSetActive(HMI_BTN_CYC1, 1);
    TJC_BtnSetActive(HMI_BTN_CYC3, 0);

    // 显示初始状态
    TJC_ClearGraph();
    TJC_SetStatus("就绪 | 请按[开始测量]");
}

/* ================================================================
 * 主循环
 * ================================================================ */

void App_Loop(void)
{
    // 主要工作由触摸事件驱动, 这里可以放:
    // - 自动定时测量
    // - 按键扫描 (硬件按键)
    // - 看门狗喂狗
}

/* ================================================================
 * 你的接口
 * ================================================================ */

DispMode_t App_GetMode(void)
{
    if (g_disp_mode == MODE_WAVEFORM) return MODE_WAVEFORM;
    else return MODE_SPECTRUM;
}

Cycle_t App_GetCycle(void)
{
    if (g_cycle == CYC_1) return CYC_1;
    else return CYC_3;
}

void App_SubmitResult(MeasureResult_t *result,
                      const float *wave_data, uint16_t wave_len,
                      uint32_t sample_rate,
                      const float *fft_mag, uint16_t fft_len)
{
    if (!result) return;

    // 缓存数据 (模式/周期切换时直接复用)
    memcpy(&g_cached_result, result, sizeof(MeasureResult_t));
    g_cached_wave = wave_data;
    g_cached_wave_len = wave_len;
    g_cached_sample_rate = sample_rate;
    g_has_data = 1;

    // 1. 更新参数文字
    TJC_UpdateParams(result);

    // 2. 更新波形
    if (g_disp_mode == MODE_WAVEFORM && wave_data && wave_len > 0) {

        // 缩放到屏幕Y坐标 (0~GRAPH_H)
        // 用动态分配的堆空间, 避免栈溢出 (4096*2=8KB)
        uint16_t *display_buf = (uint16_t *)malloc(wave_len * sizeof(uint16_t));
        if (!display_buf) return;

        float vmin = 1e9f, vmax = -1e9f;
        for (int i = 0; i < wave_len; i++) {
            float v = wave_data[i];
            if (v > vmax) vmax = v;
            if (v < vmin) vmin = v;
        }
        float range = vmax - vmin;
        if (range < 1.0f) range = 1.0f;
        float margin = range * 0.1f;
        range += margin * 2;
        vmin -= margin;

        for (int i = 0; i < wave_len; i++) {
            float v = (wave_data[i] - vmin) / range;
            uint16_t y = (uint16_t)(v * GRAPH_H);
            if (y > GRAPH_H) y = GRAPH_H;
            display_buf[i] = y;
        }

        // 截取1或3周期
        uint16_t pts_1t = wave_len / 2;
        if (result->f_base_hz > 0 && sample_rate > 0) {
            pts_1t = (uint16_t)(sample_rate / result->f_base_hz);
        }
        if (pts_1t == 0 || pts_1t > wave_len) pts_1t = wave_len;

        uint16_t show_len = (g_cycle == CYC_1) ? pts_1t : pts_1t * 3;
        if (show_len > wave_len) show_len = wave_len;

        TJC_DrawWaveform(display_buf, show_len);
        free(display_buf);

        TJC_SetStatus("波形 | f=%.1fkHz | Vpp=%.1fmV | Vrms=%.1fmV",
                      (double)(result->f_base_hz / 1000.0),
                      (double)result->vpp_mv, (double)result->vrms_mv);

    } else if (g_disp_mode == MODE_SPECTRUM) {
        // 画频谱柱状图 (只用 result 里的峰值, 不依赖完整FFT数据)
        uint16_t spec_amps[3];
        float amax = 0;
        for (int i = 0; i < result->harmonic_count; i++) {
            if (result->amp_mv[i] > amax) amax = result->amp_mv[i];
        }
        if (amax < 1.0f) amax = 1.0f;
        for (int i = 0; i < result->harmonic_count; i++) {
            spec_amps[i] = (uint16_t)(result->amp_mv[i] / amax * GRAPH_H * 0.8f);
        }

        TJC_DrawSpectrum(result->freq_hz, spec_amps, result->harmonic_count);

        TJC_SetStatus("频谱 | f=%.1fkHz | 谐波数=%d",
                      (double)(result->f_base_hz / 1000.0),
                      result->harmonic_count);
    }
}

void App_ShowError(const char *msg)
{
    TJC_SetStatus("错误: %s", msg);
}
