/**
 ******************************************************************************
 * @file    ui.c
 * @brief   双通道示波器UI渲染引擎 (240x320 ST7789V LCD)
 *          - CH0 (Y=0..134, 黄色): 始终显示
 *          - CH1 (Y=135..269, 绿色): 有波才显示
 *          - 状态栏 (Y=270..319)
 *          - 差分更新: 只改变化像素, 减少SPI通信
 ******************************************************************************
 */
#include "ui.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* ---- 通道Y底部(内部用) ---- */
#define CH0_BOT   134
#define CH1_BOT   269

/* ---- 每通道差分缓存 ---- */
static uint16_t s_prev_buf_ch0[OSC_DISP_WIDTH];
static uint16_t s_prev_buf_ch1[OSC_DISP_WIDTH];
static uint8_t  s_first_draw_ch0 = 1;
static uint8_t  s_first_draw_ch1 = 1;

/* ---- 触发标记缓存 ---- */
static uint8_t  s_prev_trig_found = 0;
static uint32_t s_prev_trig_pos   = 0;

/* ---- 状态栏文字缓存 ---- */
static char s_last_line1[32] = "";
static char s_last_line2[32] = "";
static char s_last_line3[32] = "";
static uint8_t s_sb_inited   = 0;

/* ---- FFT区域初始标志(可被ClearWaveAreas复位) ---- */
static uint8_t s_fft_area_inited = 0;

static inline uint16_t _min(uint16_t a, uint16_t b) { return a < b ? a : b; }
static inline uint16_t _clamp(int32_t v, uint16_t lo, uint16_t hi) {
    if (v < (int32_t)lo) return lo;
    if (v > (int32_t)hi) return hi;
    return (uint16_t)v;
}

/* 耦合偏置: DC=850mV, AC=1.7V → ADC值 */
#define ADC_BIAS_DC   1055   /* 850mV * 4096 / 3.3V */
#define ADC_BIAS_AC   2110   /* 1.7V * 4096 / 3.3V  */

/* ADC值→像素Y(根据通道耦合偏置, V/div缩放) */
static uint16_t _adc_to_y(uint16_t adc, uint8_t ch, uint16_t adc_span)
{
    uint16_t ch_bot = (ch == 0) ? CH0_BOT : CH1_BOT;
    int32_t center_adc = g_osc.coupling_dc[ch] ? ADC_BIAS_DC : ADC_BIAS_AC;
    int32_t half_h     = (UI_CH_HEIGHT - 1) / 2;  /* 67 */
    int32_t mid_y      = ch_bot - half_h;
    int32_t offset     = (int32_t)adc - center_adc;
    int32_t scaled     = offset * half_h * 2 / (int32_t)adc_span;
    return _clamp(mid_y - scaled, ch_bot - (UI_CH_HEIGHT - 1), ch_bot);
}

/* =========================== 网格绘制 ==================================== */
void UI_DrawGrids(void)
{
    uint16_t ch_top, ch_bot;
    uint8_t ch;

    for (ch = 0; ch < 2; ch++)
    {
        ch_top = (ch == 0) ? UI_CH0_TOP : UI_CH1_TOP;
        ch_bot = (ch == 0) ? UI_CH0_BOTTOM : UI_CH1_BOTTOM;

        /* 填充通道背景 */
        LCD_Fill(0, ch_top, 239, ch_bot, UI_BG_COLOR);

        /* 竖虚线(10格, 每格24px) */
        for (uint16_t gx = 0; gx <= 240; gx += 24)
        {
            for (uint16_t gy = ch_top; gy <= ch_bot; gy += 2)
                LCD_DrawPoint(gx, gy, UI_GRID_COLOR);
        }

        /* 横虚线(5格, 每格27px) */
        for (int gy = 0; gy <= 5; gy++)
        {
            uint16_t y = ch_top + gy * 27;
            if (y > ch_bot) y = ch_bot;
            for (uint16_t gx = 0; gx < 240; gx += 2)
                LCD_DrawPoint(gx, y, UI_GRID_COLOR);
        }

        /* 通道分隔线(稍亮) */
        if (ch == 1)
        {
            for (uint16_t gx = 0; gx < 240; gx += 2)
                LCD_DrawPoint(gx, UI_CH1_TOP, 0x630C);
        }
    }
}

/* =========================== 波形差分绘制(单通道) =========================== */
/**
 * @brief 绘制单通道波形(差分更新)
 * @param ch          通道号 (0或1)
 * @param disp_buf    显示buffer(240点 ADC值 0-4095)
 * @param trig_pos    触发点在disp_buf中的位置
 * @param trig_found  是否找到触发
 * @param use_trigger 是否绘制触发标记(CH0=1, CH1=0)
 */
void UI_DrawWaveform(uint8_t ch, const uint16_t *disp_buf, uint32_t trig_pos,
                     uint8_t trig_found, uint8_t use_trigger)
{
    uint8_t  *first_draw = (ch == 0) ? &s_first_draw_ch0 : &s_first_draw_ch1;
    uint16_t *prev_buf   = (ch == 0) ? s_prev_buf_ch0   : s_prev_buf_ch1;
    uint16_t wave_color   = (ch == 0) ? UI_WAVE_COLOR_CH0 : UI_WAVE_COLOR_CH1;

    uint16_t adc_span = Osc_GetAdcSpan();

    if (*first_draw)
    {
        /* === 首帧: 全量绘制 === */
        uint16_t prev_y = 0xFFFF;
        for (uint32_t xi = 0; xi < OSC_DISP_WIDTH; xi++)
        {
            uint16_t y = _adc_to_y(disp_buf[xi], ch, adc_span);
            LCD_DrawPoint((uint16_t)xi, y, wave_color);

            if (prev_y != 0xFFFF && xi > 0)
                LCD_DrawLine((uint16_t)(xi - 1), prev_y, (uint16_t)xi, y, wave_color);
            prev_y = y;
        }

        /* 触发标记(仅CH0) */
        if (use_trigger && trig_found && trig_pos < OSC_DISP_WIDTH)
        {
            uint16_t ty = _adc_to_y(disp_buf[trig_pos], 0, adc_span);
            uint16_t tx = (uint16_t)trig_pos;
            uint16_t ch_top = CH0_BOT - (UI_CH_HEIGHT - 1);
            LCD_DrawLine(tx, (ty > 3) ? (ty - 3) : ch_top,
                         tx, _min(ty + 3, CH0_BOT), UI_TRIG_COLOR);
            LCD_DrawLine((tx > 3) ? (tx - 3) : 0, ty,
                         _min(tx + 3, 239), ty, UI_TRIG_COLOR);
        }

        memcpy(prev_buf, disp_buf, OSC_DISP_WIDTH * sizeof(uint16_t));
        *first_draw = 0;
        return;
    }

    /* === 后续帧: 差分更新 === */

    /* 擦除旧触发标记(仅CH0) */
    if (use_trigger && s_prev_trig_found && s_prev_trig_pos < OSC_DISP_WIDTH)
    {
        uint16_t oty = _adc_to_y(s_prev_buf_ch0[s_prev_trig_pos], 0, adc_span);
        uint16_t otx = (uint16_t)s_prev_trig_pos;
        uint16_t ch0_top = CH0_BOT - (UI_CH_HEIGHT - 1);
        LCD_DrawLine(otx, (oty > 3) ? (oty - 3) : ch0_top,
                     otx, _min(oty + 3, CH0_BOT), UI_BG_COLOR);
        LCD_DrawLine((otx > 3) ? (otx - 3) : 0, oty,
                     _min(otx + 3, 239), oty, UI_BG_COLOR);
    }

    /* 差分更新波形点 */
    uint16_t prev_y_old = 0xFFFF;
    uint16_t prev_y_new = 0xFFFF;

    for (uint32_t xi = 0; xi < OSC_DISP_WIDTH; xi++)
    {
        uint16_t y_old = _adc_to_y(prev_buf[xi], ch, adc_span);
        uint16_t y_new = _adc_to_y(disp_buf[xi], ch, adc_span);

        /* 擦除旧连线 */
        if (prev_y_old != 0xFFFF && xi > 0)
            LCD_DrawLine((uint16_t)(xi - 1), prev_y_old, (uint16_t)xi, y_old, UI_BG_COLOR);

        if (y_old != y_new)
            LCD_DrawPoint((uint16_t)xi, y_old, UI_BG_COLOR);

        LCD_DrawPoint((uint16_t)xi, y_new, wave_color);

        /* 画新连线 */
        if (prev_y_new != 0xFFFF && xi > 0)
            LCD_DrawLine((uint16_t)(xi - 1), prev_y_new, (uint16_t)xi, y_new, wave_color);

        prev_y_old = y_old;
        prev_y_new = y_new;
    }

    /* 缓存当前帧 */
    memcpy(prev_buf, disp_buf, OSC_DISP_WIDTH * sizeof(uint16_t));

    /* 绘制新触发标记(仅CH0) */
    if (use_trigger)
    {
        s_prev_trig_found = trig_found;
        s_prev_trig_pos   = trig_pos;

        if (trig_found && trig_pos < OSC_DISP_WIDTH)
        {
            uint16_t nty = _adc_to_y(disp_buf[trig_pos], 0, adc_span);
            uint16_t ntx = (uint16_t)trig_pos;
            uint16_t ch0_top = CH0_BOT - (UI_CH_HEIGHT - 1);
            LCD_DrawLine(ntx, (nty > 3) ? (nty - 3) : ch0_top,
                         ntx, _min(nty + 3, CH0_BOT), UI_TRIG_COLOR);
            LCD_DrawLine((ntx > 3) ? (ntx - 3) : 0, nty,
                         _min(ntx + 3, 239), nty, UI_TRIG_COLOR);
        }
    }
}

/* =========================== 缓存重置 ==================================== */
void UI_ResetCache(void)
{
    memset(s_prev_buf_ch0, 0, sizeof(s_prev_buf_ch0));
    memset(s_prev_buf_ch1, 0, sizeof(s_prev_buf_ch1));
    s_first_draw_ch0 = 1;
    s_first_draw_ch1 = 1;
    s_prev_trig_found = 0;
    s_prev_trig_pos   = 0;
}

/* =========================== FFT频谱绘制(峰值检测) ================= */
void UI_DrawFFT(const FFTResult_t *fft_res)
{
    s_first_draw_ch0 = 1;
    s_first_draw_ch1 = 1;

#ifdef ARM_MATH_CM4
#define FFT_PLOT_TOP     35
#define FFT_PLOT_BOT     255
#define FFT_PLOT_H       (FFT_PLOT_BOT - FFT_PLOT_TOP)
#define FFT_DB_RANGE     50.0f   /* 可见动态范围: max_mag往下50dB */

    /* 缓存旧标记的屏幕坐标(用于精确擦除) */
    static struct {
        uint16_t x, y;     /* 峰顶屏幕坐标               */
        uint16_t lx, ly;   /* 标签左上角                  */
        uint8_t  valid;
        char     label[14];
    } s_old_mark[FFT_MAX_HARMONICS];

    if (!s_fft_area_inited)
    {
        LCD_Fill(0, 0, 239, UI_CH1_BOTTOM, UI_BG_COLOR);
        s_fft_area_inited = 1;
        for (int i = 0; i < FFT_MAX_HARMONICS; i++)
            s_old_mark[i].valid = 0;
    }

    /* ---- 查找谐波峰值(基频+3次+5次) ---- */
    HarmonicPeak_t harmonics[FFT_MAX_HARMONICS];
    FFT_FindHarmonics(fft_res, g_osc.fft_sample_rate, harmonics);

    /* ---- 变更检测: 屏幕坐标差≤1跳过(消除闪烁) ---- */
    for (uint8_t i = 0; i < FFT_MAX_HARMONICS; i++)
    {
        uint16_t nx = 0, ny = 0;
        uint16_t nlx = 0, nly = 0;
        char nlabel[14] = "";

        /* 计算新标记的屏幕坐标 */
        if (harmonics[i].valid)
        {
            nx = (uint16_t)(harmonics[i].bin - 1);
            if (nx >= 240) nx = 239;
            float ratio = 1.0f - ((fft_res->max_mag - harmonics[i].db) / FFT_DB_RANGE);
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            ny = FFT_PLOT_BOT - (uint16_t)(ratio * FFT_PLOT_H + 0.5f);
            nlx = (nx > 200) ? (nx - 52) : (nx + 6);
            nly = (ny > FFT_PLOT_TOP + 12) ? (ny - 10) : FFT_PLOT_TOP;

            float f = harmonics[i].freq;
            if (f >= 1000.0f)
                snprintf(nlabel, sizeof(nlabel), "%.1fkHz", f / 1000.0f);
            else if (f > 0.0f)
                snprintf(nlabel, sizeof(nlabel), "%.0fHz", f);
        }

        /* 坐标未变则跳过 */
        if (harmonics[i].valid && s_old_mark[i].valid)
        {
            int16_t dx = (int16_t)nx - (int16_t)s_old_mark[i].x;
            int16_t dy = (int16_t)ny - (int16_t)s_old_mark[i].y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= 1 && dy <= 2 && strcmp(nlabel, s_old_mark[i].label) == 0)
            {
                s_old_mark[i].x = nx; s_old_mark[i].y = ny;
                s_old_mark[i].lx = nlx; s_old_mark[i].ly = nly;
                continue;
            }
        }

        /* --- 擦除旧标记(用缓存的屏幕坐标, 精确命中) --- */
        if (s_old_mark[i].valid)
        {
            uint16_t ox = s_old_mark[i].x, oy = s_old_mark[i].y;
            if (ox > 0 && ox < 239)
            {
                LCD_DrawLine(ox, oy, ox, FFT_PLOT_BOT, UI_BG_COLOR);
                if (ox >= 3 && ox <= 235 && oy > FFT_PLOT_TOP + 5)
                {
                    LCD_DrawLine(ox, oy, ox - 3, oy + 5, UI_BG_COLOR);
                    LCD_DrawLine(ox, oy, ox + 3, oy + 5, UI_BG_COLOR);
                }
                LCD_Fill(s_old_mark[i].lx, s_old_mark[i].ly,
                         (s_old_mark[i].lx + 52 < 239) ? (s_old_mark[i].lx + 52) : 239,
                         (s_old_mark[i].ly + 14 < FFT_PLOT_BOT) ? (s_old_mark[i].ly + 14) : FFT_PLOT_BOT,
                         UI_BG_COLOR);
            }
        }

        /* --- 绘制新标记 --- */
        if (harmonics[i].valid)
        {
            LCD_DrawLine(nx, ny, nx, FFT_PLOT_BOT, UI_FFT_PEAK_COLOR);
            if (nx >= 3 && nx <= 235 && ny > FFT_PLOT_TOP + 5)
            {
                LCD_DrawLine(nx, ny, nx - 3, ny + 5, UI_FFT_PEAK_COLOR);
                LCD_DrawLine(nx, ny, nx + 3, ny + 5, UI_FFT_PEAK_COLOR);
            }
            if (nlabel[0])
                LCD_ShowString(nlx, nly, nlabel, UI_FFT_PEAK_COLOR, UI_BG_COLOR);
        }

        /* 缓存本帧坐标 */
        s_old_mark[i].x = nx; s_old_mark[i].y = ny;
        s_old_mark[i].lx = nlx; s_old_mark[i].ly = nly;
        s_old_mark[i].valid = harmonics[i].valid;
        if (nlabel[0]) strcpy(s_old_mark[i].label, nlabel);
        else s_old_mark[i].label[0] = '\0';
    }

    /* ---- 基线 ---- */
    for (uint16_t xp = 0; xp < 240; xp += 4)
        LCD_DrawPoint(xp, FFT_PLOT_BOT, UI_GRID_COLOR);

#else
    LCD_Fill(0, UI_CH0_TOP, 239, UI_CH1_BOTTOM, UI_BG_COLOR);
    LCD_ShowString(60, 100, "FFT: No DSP", UI_TEXT_COLOR, UI_BG_COLOR);
#endif
}

/* =========================== 状态栏 ====================================== */
/**
 * @brief 三行状态栏
 *        Line1 (Y=271): 时基+模式+触发信息
 *        Line2 (Y=285): CH0 测量值(黄色)
 *        Line3 (Y=299): CH1 测量值(绿色, 无波显示"---")
 */
void UI_DrawStatusBar(const Oscilloscope_t *osc)
{
    if (!s_sb_inited)
    {
        LCD_Fill(0, UI_STATUSBAR_TOP, 239, UI_STATUSBAR_BOTTOM, UI_BG_COLOR);
        LCD_DrawLine(0, UI_STATUSBAR_TOP, 239, UI_STATUSBAR_TOP, UI_GRID_COLOR);
        s_sb_inited = 1;
    }

    /* ---- 自校正状态覆盖整个信息栏 ---- */
    if (g_calib_state > 0)
    {
        if (g_calib_state == 1)
        {
            LCD_Fill(0, UI_STATUSBAR_TOP + 2, 239, UI_STATUSBAR_BOTTOM, UI_BG_COLOR);
            LCD_ShowString(0, UI_STATUSBAR_TOP + 10, "  Calibrating...",
                           UI_TEXT_COLOR, UI_BG_COLOR);
        }
        else
        {
            LCD_Fill(0, UI_STATUSBAR_TOP + 2, 239, UI_STATUSBAR_BOTTOM, UI_BG_COLOR);
            LCD_ShowString(0, UI_STATUSBAR_TOP + 10, "  Calibration Done",
                           UI_TEXT_COLOR, UI_BG_COLOR);
        }
        s_sb_inited = 1;
        return;
    }

    /* ---- Line1: 时基+模式+V/div ---- */
    char line1[32];
    const char *tdiv_str = g_tb_table[osc->timebase].label;

    if (g_trig_adj_mode)
    {
        float trig_v = osc->trig_level * 3.3f / 4096.0f;
        snprintf(line1, sizeof(line1), "TRIG:%.2fV %c%c[%d] %s",
                 trig_v,
                 (osc->trig_mode == TRIG_AUTO)   ? 'A' :
                 (osc->trig_mode == TRIG_NORMAL) ? 'N' : 'S',
                 (osc->trig_edge == EDGE_RISING) ? 'R' : 'F',
                 osc->trig_channel,
                 g_vscale_table[osc->vdiv].label);
    }
    else
    {
        char mode_str[8];
        if (osc->disp_mode == DISP_WAVEFORM)
            snprintf(mode_str, sizeof(mode_str), "Wav");
        else
            snprintf(mode_str, sizeof(mode_str), "F%d", osc->fft_channel);
        snprintf(line1, sizeof(line1), "%s%c %c%c[%d] %s %s",
                 tdiv_str,
                 osc->auto_tb ? 'A' : 'M',
                 (osc->trig_mode == TRIG_AUTO)   ? 'A' :
                 (osc->trig_mode == TRIG_NORMAL) ? 'N' : 'S',
                 (osc->trig_edge == EDGE_RISING) ? 'R' : 'F',
                 osc->trig_channel,
                 mode_str,
                 g_vscale_table[osc->vdiv].label);
    }
    /* ---- 波形类型标签(FFT模式下显示在Line2末尾) ---- */
    const char *type_str = "";
    if (osc->disp_mode == DISP_FFT)
    {
        switch (osc->wave_type)
        {
        case WAVE_SINE:     type_str = "Sin"; break;
        case WAVE_SQUARE:   type_str = "Sqr"; break;
        case WAVE_TRIANGLE: type_str = "Tri"; break;
        case WAVE_SAWTOOTH: type_str = "Saw"; break;
        default:            type_str = "";    break;
        }
    }
    /* ---- Line2: CH0 ---- */
    char line2[32];
    const OscMeasure_t *m0 = &osc->measure[OSC_CH0];
    if (m0->freq > 0.0f)
    {
        if (m0->freq >= 1000.0f)
            snprintf(line2, sizeof(line2), "CH0(%s):%.2fkHz %.2fVpp %s",
                     osc->coupling_dc[0] ? "DC" : "AC", m0->freq / 1000.0f, m0->vpp, type_str);
        else if (m0->freq >= 100.0f)
            snprintf(line2, sizeof(line2), "CH0(%s):%.0fHz %.2fVpp %s",
                     osc->coupling_dc[0] ? "DC" : "AC", m0->freq, m0->vpp, type_str);
        else
            snprintf(line2, sizeof(line2), "CH0(%s):%.1fHz %.2fVpp %s",
                     osc->coupling_dc[0] ? "DC" : "AC", m0->freq, m0->vpp, type_str);
    }
    else
        snprintf(line2, sizeof(line2), "CH0(%s):--- %.2fVpp %s",
                 osc->coupling_dc[0] ? "DC" : "AC", m0->vpp, type_str);

    /* ---- Line3: CH1 ---- */
    char line3[32];
    const OscMeasure_t *m1 = &osc->measure[OSC_CH1];
    if (osc->wave_present[OSC_CH1])
    {
        if (m1->freq > 0.0f)
        {
            if (m1->freq >= 1000.0f)
                snprintf(line3, sizeof(line3), "CH1(%s):%.2fkHz %.2fVpp",
                         osc->coupling_dc[1] ? "DC" : "AC", m1->freq / 1000.0f, m1->vpp);
            else if (m1->freq >= 100.0f)
                snprintf(line3, sizeof(line3), "CH1(%s):%.0fHz %.2fVpp",
                         osc->coupling_dc[1] ? "DC" : "AC", m1->freq, m1->vpp);
            else
                snprintf(line3, sizeof(line3), "CH1(%s):%.1fHz %.2fVpp",
                         osc->coupling_dc[1] ? "DC" : "AC", m1->freq, m1->vpp);
        }
        else
            snprintf(line3, sizeof(line3), "CH1(%s):--- %.2fVpp",
                     osc->coupling_dc[1] ? "DC" : "AC", m1->vpp);
    }
    else
        snprintf(line3, sizeof(line3), "CH1(%s):No Signal",
                 osc->coupling_dc[1] ? "DC" : "AC");

    /* 只在内容变化时才重写 */
    uint16_t sb_y1 = UI_STATUSBAR_TOP + 2;
    uint16_t sb_y2 = UI_STATUSBAR_TOP + 18;
    uint16_t sb_y3 = UI_STATUSBAR_TOP + 33;

    if (strcmp(line1, s_last_line1) != 0)
    {
        LCD_Fill(0, sb_y1, 239, sb_y1 + 13, UI_BG_COLOR);
        LCD_ShowString(0, sb_y1, line1, UI_TEXT_COLOR, UI_BG_COLOR);
        strcpy(s_last_line1, line1);
    }
    if (strcmp(line2, s_last_line2) != 0)
    {
        LCD_Fill(0, sb_y2, 239, sb_y2 + 13, UI_BG_COLOR);
        LCD_ShowString(0, sb_y2, line2, UI_TEXT_CH0_COLOR, UI_BG_COLOR);
        strcpy(s_last_line2, line2);
    }
    if (strcmp(line3, s_last_line3) != 0)
    {
        LCD_Fill(0, sb_y3, 239, sb_y3 + 13, UI_BG_COLOR);
        LCD_ShowString(0, sb_y3, line3, UI_TEXT_CH1_COLOR, UI_BG_COLOR);
        strcpy(s_last_line3, line3);
    }
}

void UI_ResetStatusBar(void)
{
    s_sb_inited = 0;
    s_last_line1[0] = '\0';
    s_last_line2[0] = '\0';
    s_last_line3[0] = '\0';
}

void UI_ClearWaveAreas(void)
{
    s_first_draw_ch0 = 1;
    s_first_draw_ch1 = 1;
    s_fft_area_inited = 0;
    LCD_Fill(0, UI_CH0_TOP, 239, UI_CH1_BOTTOM, UI_BG_COLOR);
}

void UI_ClearStatusBar(void)
{
    s_sb_inited = 0;
    LCD_Fill(0, UI_STATUSBAR_TOP, 239, UI_STATUSBAR_BOTTOM, UI_BG_COLOR);
}
