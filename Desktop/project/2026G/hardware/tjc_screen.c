/**
 * TJC8048X270 串口屏驱动 - 实现文件
 * 周期信号测量分析装置 (2026电赛G题)
 *
 * TJC 串口指令协议:
 *   - 所有指令以 ASCII 字符串发送
 *   - 每条指令以 0xFF 0xFF 0xFF 结尾
 *   - 例: "page 0\xFF\xFF\xFF" 切换到页面0
 *
 * 关键指令参考:
 *   page <n>                    切换页面
 *   <obj>.txt="<str>"           设置文本
 *   <obj>.val=<n>               设置数值控件
 *   <obj>.bco=<color>           设置背景色
 *   <obj>.pco=<color>           设置前景色
 *   cle <id>,<ch>               清除曲线通道
 *   add <id>,<ch>,<val>         添加曲线数据点
 *   addt <id>,<ch>,<n>          批量添加 n 个点
 *   fill <x1>,<y1>,<x2>,<y2>,<color>  填充矩形
 *   vis <obj>,<n>               设置可见性 (0/1)
 *   ref <obj>                   刷新控件
 */

#include "tjc_screen.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 帧尾定义
 * ================================================================ */
static const uint8_t TJC_END[3] = {0xFF, 0xFF, 0xFF};

/* ================================================================
 * 内部变量
 * ================================================================ */
static UART_HandleTypeDef *tjc_uart = NULL;
static char cmd_buf[256];   // 指令缓冲区

/* ================================================================
 * 底层发送
 * ================================================================ */
static void uart_send(const uint8_t *data, uint16_t len)
{
    if (tjc_uart) {
        HAL_UART_Transmit(tjc_uart, (uint8_t *)data, len, 100);
    }
}

static void send_end(void)
{
    uart_send(TJC_END, 3);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * 初始化串口屏
 */
void TJC_Init(UART_HandleTypeDef *huart)
{
    tjc_uart = huart;

    // 等待屏幕启动完成 (约2秒)
    HAL_Delay(2000);

    // 发送握手或初始配置 (可选: 设置波特率到115200)
    // 注意: 屏幕这边也要同步改波特率!
    // TJC_SendCmd("bauds=115200");  // 如果要用高速率

    // 切换到主页面
    TJC_PageMain();

    // 初始状态
    TJC_SetStatus("就绪 | 模式: 波形 | 1周期");
}

/**
 * 切换到主页面
 */
void TJC_PageMain(void)
{
    snprintf(cmd_buf, sizeof(cmd_buf), "page %d", PAGE_MAIN);
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
}

/**
 * 设置状态栏文字
 */
void TJC_SetStatus(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = snprintf(cmd_buf, sizeof(cmd_buf),
                       "%s.txt=\"", HMI_STATUS);
    len += vsnprintf(cmd_buf + len, sizeof(cmd_buf) - len - 4, fmt, args);
    len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "\"");
    va_end(args);

    uart_send((uint8_t *)cmd_buf, len);
    send_end();
}

/**
 * 清除图形显示区
 */
void TJC_ClearGraph(void)
{
    // 清除曲线控件 (通道0)
    snprintf(cmd_buf, sizeof(cmd_buf), "cle %s,0", HMI_CURVE);
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
}

/**
 * 更新所有测量参数显示
 */
void TJC_UpdateParams(const MeasureResult_t *r)
{
    if (!r) return;

    // --- Vpp ---
    snprintf(cmd_buf, sizeof(cmd_buf),
             "%s.txt=\"Vpp: %.1f mV\"", HMI_VPP, (double)r->vpp_mv);
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
    HAL_Delay(30);

    // --- Vrms ---
    snprintf(cmd_buf, sizeof(cmd_buf),
             "%s.txt=\"Vrms: %.1f mV\"", HMI_VRMS, (double)r->vrms_mv);
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
    HAL_Delay(30);

    // --- 基频 ---
    snprintf(cmd_buf, sizeof(cmd_buf),
             "%s.txt=\"f1: %.1f kHz\"", HMI_F1, (double)(r->f_base_hz / 1000.0f));
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
    HAL_Delay(30);

    // --- 基波幅值 ---
    snprintf(cmd_buf, sizeof(cmd_buf),
             "%s.txt=\"U1: %.1f mV\"", HMI_U1, (double)r->amp_mv[0]);
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
    HAL_Delay(30);

    // --- 谐波1 ---
    if (r->harmonic_count >= 2) {
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"f2: %.1f kHz\"", HMI_F2,
                 (double)(r->freq_hz[1] / 1000.0f));
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);

        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"U2: %.1f mV\"", HMI_U2, (double)r->amp_mv[1]);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);
    } else {
        // 没有谐波时显示 "---"
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"f2: --- kHz\"", HMI_F2);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"U2: --- mV\"", HMI_U2);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);
    }

    // --- 谐波2 ---
    if (r->harmonic_count >= 3) {
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"f3: %.1f kHz\"", HMI_F3,
                 (double)(r->freq_hz[2] / 1000.0f));
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);

        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"U3: %.1f mV\"", HMI_U3, (double)r->amp_mv[2]);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);
    } else {
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"f3: --- kHz\"", HMI_F3);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "%s.txt=\"U3: --- mV\"", HMI_U3);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(30);
    }
}

/**
 * 绘制时域波形 — HMI add 逐点模式
 *
 * 参照 HMI 项目 send_waveform():
 *   cle s0,0\xFF\xFF\xFF             ← 清空通道
 *   add s0,0,<val>\xFF\xFF\xFF       ← 逐点发送，每点以 FF FF FF 结尾
 *   ... 共 600 点
 *
 * Y轴: 0=顶部, height=底部, 需要翻转 (GRAPH_H - val)
 */
void TJC_DrawWaveform(const uint16_t *data, uint16_t len)
{
    if (!data || len == 0) return;

    uint16_t n = (len > GRAPH_W) ? GRAPH_W : len;

    /* 清空曲线通道 */
    TJC_ClearGraph();
    HAL_Delay(50);

    printf("[TJC] sending %d points...\r\n", (int)n);
    printf("[TJC] input range: [%u..%u]  output range: [%u..%u]\r\n",
           (unsigned)data[0], (unsigned)data[n-1],
           (unsigned)(GRAPH_H - data[0]), (unsigned)(GRAPH_H - data[n-1]));

    /* 逐点发送 (与 HMI send_waveform 格式一致) */
    for (uint16_t i = 0; i < n; i++) {
        uint16_t y = GRAPH_H - data[i];
        if (y > GRAPH_H) y = GRAPH_H;

        int slen = snprintf(cmd_buf, sizeof(cmd_buf),
                            "add %s,0,%u", HMI_CURVE, (unsigned)y);
        uart_send((uint8_t *)cmd_buf, slen);
        send_end();

        if ((i + 1) % 100 == 0) {
            printf("[TJC] %d/%d sent\r\n", (int)(i + 1), (int)n);
        }
    }
    printf("[TJC] all %d points sent\r\n", (int)n);
}

/**
 * 绘制频谱图 (柱状图)
 *
 * 使用 fill 指令绘制垂直柱状条
 * fill <x1>,<y1>,<x2>,<y2>,<color>
 * 颜色: RED=63488, BLUE=31, GREEN=2016, YELLOW=65504, WHITE=65535
 */
void TJC_DrawSpectrum(const float *freqs, const uint16_t *amps, uint8_t count)
{
    if (!freqs || !amps || count == 0 || count > 3) return;

    // 先清除
    TJC_ClearGraph();
    HAL_Delay(20);

    // 绘制网格参考线 (底部横线)
    // 在频谱模式下, 使用填充矩形画谱线
    // 每根谱线宽度约 80 像素, 均匀分布
    uint16_t bar_w = 60;  // 柱宽
    uint16_t spacing = GRAPH_W / (count + 1);  // 间距

    // 颜色数组: 基波红色, 谐波蓝/绿
    uint16_t colors[3] = {63488, 2016, 31};  // RED, GREEN, BLUE

    for (uint8_t i = 0; i < count; i++) {
        uint16_t x_center = spacing * (i + 1);
        uint16_t x1 = x_center - bar_w / 2;
        uint16_t x2 = x_center + bar_w / 2;

        // Y: 柱底部=GRAPH_H (屏幕底部), 顶部=GRAPH_H - 柱高
        uint16_t bar_h = amps[i];
        if (bar_h > GRAPH_H) bar_h = GRAPH_H;
        uint16_t y1 = GRAPH_H - bar_h;
        uint16_t y2 = GRAPH_H;

        // fill 指令
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "fill %d,%d,%d,%d,%d",
                 x1, y1, x2, y2, colors[i % 3]);
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(20);
    }

    // 标注频率值 (用文本控件或直接在坐标上标注)
    for (uint8_t i = 0; i < count; i++) {
        uint16_t x_center = spacing * (i + 1);

        // 在柱下方标注频率
        // 用 "xstr" 指令在指定坐标显示文字
        // xstr <x>,<y>,<w>,<h>,<font>,<color>,<bg>,<xalign>,<yalign>,<text>
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "xstr %d,%d,100,30,0,65535,0,1,1,\"%.1fk\"",
                 x_center - 50, GRAPH_H + 5,
                 (double)(freqs[i] / 1000.0f));
        uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
        send_end();
        HAL_Delay(20);
    }
}

/**
 * 发送自定义指令
 */
void TJC_SendCmd(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(cmd_buf, sizeof(cmd_buf) - 4, fmt, args);
    va_end(args);

    uart_send((uint8_t *)cmd_buf, len);
    send_end();
}

/**
 * 设置按钮高亮/灰色
 * active=1: 蓝色(选中), active=0: 灰色(未选中)
 */
void TJC_BtnSetActive(const char *name, uint8_t active)
{
    snprintf(cmd_buf, sizeof(cmd_buf),
             "%s.bco=%d", name, active ? 31 : 33808);
    uart_send((uint8_t *)cmd_buf, strlen(cmd_buf));
    send_end();
}

/**
 * 发送原始字节数据
 */
void TJC_SendBytes(const uint8_t *data, uint16_t len)
{
    uart_send(data, len);
}

/**
 * 等待屏幕处理完毕
 * (简单延时实现, 可根据需要改用屏幕的应答机制)
 */
void TJC_WaitReady(uint32_t timeout_ms)
{
    HAL_Delay(timeout_ms);
}
