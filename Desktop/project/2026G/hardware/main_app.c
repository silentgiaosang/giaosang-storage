/**
 * 周期信号测量分析装置 - 串口屏主程序
 * 2026电赛G题 — HMI refactor
 */

#include "main.h"
#include "tjc_screen.h"
#include "measure.h"
#include "wavegen.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "stdio.h"
/* ================================================================
 *  显示状态
 * ================================================================ */
static DispMode_t g_disp_mode = MODE_WAVEFORM;
static Cycle_t    g_cycle     = CYC_3;          /* default: 3-cycle */

/* 最新测量结果缓存 (由 App_Loop 在 DataReady 时刷新) */
static MeasureResult_t g_latest_result;
static PeakResult_t    g_latest_peaks;
static WaveType_t      g_latest_type = WAVE_SINE;

/* ================================================================
 *  内部刷新函数
 * ================================================================ */
static void RefreshWaveform(void)
{
    /* Static buffers — too large for stack (600*4 + 600*2 = 3.6KB, stack=1KB) */
    static float    wave_buf[WAVE_PTS];
    static uint16_t scr_buf[WAVE_PTS];

    /* 1. 合成波形数据 */
    uint16_t n = WaveGen_Generate(wave_buf, WAVE_PTS, g_latest_type,
                                  (g_cycle == CYC_1) ? 1 : 3,
                                  &g_latest_result, &g_latest_peaks);

    /* 2. 缩放到屏幕 Y 坐标 */
    float vmin = wave_buf[0], vmax = wave_buf[0];
    for (uint16_t i = 1; i < n; i++) {
        if (wave_buf[i] < vmin) vmin = wave_buf[i];
        if (wave_buf[i] > vmax) vmax = wave_buf[i];
    }
    float range = vmax - vmin;
    if (range < 1.0f) range = 1.0f;
    float margin = range * 0.1f;
    range += margin * 2.0f;
    vmin -= margin;

    for (uint16_t i = 0; i < n; i++) {
        float v = (wave_buf[i] - vmin) / range;
        uint16_t y = (uint16_t)(v * GRAPH_H);
        if (y > GRAPH_H) y = GRAPH_H;
        scr_buf[i] = y;
    }

    /* 3. 发送到屏幕 */
    TJC_DrawWaveform(scr_buf, n);

    /* 4. 更新参数文本 */
    TJC_UpdateParams(&g_latest_result);

    /* 5. 状态栏 */
    const char *type_str = "?";
    switch (g_latest_type) {
        case WAVE_SINE:     type_str = "正弦"; break;
        case WAVE_SQUARE:   type_str = "方波"; break;
        case WAVE_TRIANGLE: type_str = "三角波"; break;
        case WAVE_HARMONIC: type_str = "谐波"; break;
    }
    TJC_SetStatus("%s | f=%.1fkHz | Vpp=%.0fmV | %dT",
                  type_str,
                  (double)(g_latest_result.f_base_hz / 1000.0),
                  (double)g_latest_result.vpp_mv,
                  (g_cycle == CYC_1) ? 1 : 3);
}

static void RefreshSpectrum(void)
{
    /* Build spectrum from latest peaks */
    uint16_t spec_amps[3] = {0};
    float    spec_freqs[3] = {0};

    /* Find max amplitude for scaling */
    float amax = 0.0f;
    for (int i = 0; i < g_latest_peaks.count && i < 3; i++) {
        if (g_latest_peaks.vpp_mv[i] > amax) amax = g_latest_peaks.vpp_mv[i];
    }
    if (amax < 1.0f) amax = 1.0f;

    for (int i = 0; i < g_latest_peaks.count && i < 3; i++) {
        spec_freqs[i] = g_latest_peaks.freq_hz[i];
        spec_amps[i]  = (uint16_t)(g_latest_peaks.vpp_mv[i] / amax * GRAPH_H * 0.8f);
    }

    TJC_DrawSpectrum(spec_freqs, spec_amps, g_latest_peaks.count);

    /* Update params text */
    TJC_UpdateParams(&g_latest_result);

    TJC_SetStatus("频谱 | f=%.1fkHz | 谐波数=%d",
                  (double)(g_latest_result.f_base_hz / 1000.0),
                  g_latest_peaks.count);
}

/* ================================================================
 *  触摸事件处理
 *
 *  TJC 触摸事件格式 (屏幕→MCU):
 *    0x65 + page_id(1B) + ctrl_id(1B) + event(1B) + value(4B) + 0xFF 0xFF 0xFF
 *    event: 0x01=按下, 0x02=释放
 * ================================================================ */

#define TJC_RX_BUF_SIZE 32
static uint8_t  tjc_rx_buf[TJC_RX_BUF_SIZE];
static uint8_t  tjc_rx_idx = 0;

void TJC_RxByteCallback(uint8_t byte)
{
    /* Detect frame end: 3 consecutive 0xFF */
    if (tjc_rx_idx >= 2 &&
        tjc_rx_buf[tjc_rx_idx-2] == 0xFF &&
        tjc_rx_buf[tjc_rx_idx-1] == 0xFF &&
        byte == 0xFF)
    {
        if (tjc_rx_idx >= 5 && tjc_rx_buf[0] == 0x65) {
            uint8_t page  = tjc_rx_buf[1];
            uint8_t ctrl  = tjc_rx_buf[2];
            uint8_t event = tjc_rx_buf[3];
            /* uint8_t value = tjc_rx_buf[4];  (unused for buttons) */

            if (event == 0x01 && !tjc_busy) {
                TJC_HandleTouch(page, ctrl, 0);
            }
        }
        tjc_rx_idx = 0;
        return;
    }

    if (tjc_rx_idx < TJC_RX_BUF_SIZE) {
        tjc_rx_buf[tjc_rx_idx++] = byte;
    } else {
        tjc_rx_idx = 0;
    }
}

/* ================================================================
 *  按钮分发 (button IDs from HMI project)
 * ================================================================ */
void TJC_HandleTouch(uint8_t page, uint8_t ctrl_id, uint8_t value)
{
    if (page != PAGE_MAIN) return;
    (void)value;

    switch (ctrl_id) {

    case 10:  /* b_start — 开始测量 → 波形模式 + 3T */
        g_disp_mode = MODE_WAVEFORM;
        g_cycle     = CYC_3;
        TJC_ClearGraph();
        RefreshWaveform();
        break;

    case 11:  /* b_wave — 输出波形 → 波形模式 + 3T */
        g_disp_mode = MODE_WAVEFORM;
        g_cycle     = CYC_3;
        TJC_ClearGraph();
        RefreshWaveform();
        break;

    case 12:  /* b_spec — 输出频谱 */
        g_disp_mode = MODE_SPECTRUM;
        TJC_ClearGraph();
        RefreshSpectrum();
        break;

    case 13:  /* b_cyc1 — 切换1周期 (不清屏，直接覆盖) */
        g_cycle = CYC_1;
        if (g_disp_mode == MODE_WAVEFORM) {
            RefreshWaveform();
        }
        break;

    case 14:  /* b_cyc3 — 切换3周期 (不清屏，直接覆盖) */
        g_cycle = CYC_3;
        if (g_disp_mode == MODE_WAVEFORM) {
            RefreshWaveform();
        }
        break;

    default:
        break;
    }
}

/* ================================================================
 *  初始化
 * ================================================================ */
void App_Init(UART_HandleTypeDef *huart)
{
    TJC_Init(huart);

    /* Button initial state */
    TJC_BtnSetActive(HMI_BTN_WAVE, 1);
    TJC_BtnSetActive(HMI_BTN_SPEC, 0);
    TJC_BtnSetActive(HMI_BTN_CYC3, 1);     /* default: 3-cycle active */
    TJC_BtnSetActive(HMI_BTN_CYC1, 0);

    TJC_ClearGraph();
    TJC_SetStatus("就绪 | 请按[开始测量]");

    printf("App Init OK (HMI refactor)\r\n");
}

/* ================================================================
 *  主循环 — 连续测量 + 自动刷新显示
 * ================================================================ */
void App_Loop(void)
{
    static uint32_t heartbeat_tick = 0;
    uint32_t now = HAL_GetTick();

    /* Heartbeat: every 5s to confirm main loop is alive */
    if (now - heartbeat_tick >= 5000) {
        printf("[HB] main loop alive, mode=%d cyc=%d\r\n",
               (int)g_disp_mode, (int)g_cycle);
        heartbeat_tick = now;
    }

    /* 1. 测量在后台持续运行 */
    Measure_Process();

    /* 2. 缓存最新结果 (供按钮触发时使用)，不自动刷新显示 */
    if (Measure_DataReady()) {
        Measure_GetLatest(&g_latest_result, &g_latest_peaks, &g_latest_type);
    }
}
