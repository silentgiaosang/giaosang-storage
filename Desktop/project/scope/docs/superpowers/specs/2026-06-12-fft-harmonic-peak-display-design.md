# FFT 峰值标记：竖线按dB高度 + 谐波过滤

**Date:** 2026-06-12
**Status:** Approved
**Target:** STM32F407VET6 — Scope firmware

## Goal

修复 FFT 模式下两个问题：
1. 峰值竖线全屏贯穿，基频和谐波看起来高度一样，无法区分幅度差异
2. 显示所有谐波峰过于杂乱，改为只显示基频 + 3次 + 5次谐波

## User Story

输入 1kHz 方波 → 切换到 FFT 模式 → 看到三个峰值标记：
- 1kHz 竖线最高（基频幅度大）
- 3kHz 竖线中等（3次谐波，~1/3 幅度）
- 5kHz 竖线较矮（5次谐波，~1/5 幅度）
- 每个峰旁标注频率标签

肉眼能直接看出三个峰的幅度差异。

## Code Change Inventory

### `hardware/fft.h` — 新增结构体 + 函数声明

```c
#define FFT_MAX_HARMONICS 3

typedef struct {
    float   freq;       /* 频率(Hz)                     */
    float   db;         /* 幅度(dB)                     */
    uint16_t x;         /* 屏幕X坐标(0..239)            */
    uint16_t y;         /* 峰顶Y坐标(FFT_PLOT_TOP..BOT) */
    uint8_t  valid;     /* 1=有效谐波                    */
} HarmonicPeak_t;

void FFT_FindHarmonics(const FFTResult_t *fft_res, float sample_rate,
                       HarmonicPeak_t *harmonics);
```

### `hardware/fft.c` — 新增 `FFT_FindHarmonics()`

```
输入：FFTResult_t（包含 mag[]、peak_bin等）
      sample_rate（当前采样率Hz）

步骤：
  1. 扫描 mag[2..FFT_OUT_BINS-1]，收集所有局部极大值（bin）和dB值
  2. 在20Hz-10kHz范围内找幅度最高的峰作为基频 f0
  3. 定义三个谐波窗口：
       base   = f0 × 1 ± 10%
       harm3  = f0 × 3 ± 10%
       harm5  = f0 × 5 ± 10%
  4. 每个窗口内取幅度最高的局部极大值
  5. 计算屏幕坐标（X由bin映射，Y由dB映射）
  6. 输出 HarmonicPeak_t harmonics[FFT_MAX_HARMONICS]
```

- ±10%窗口应对于FFT bin分辨率造成的频率偏差
- 基频优先选20Hz-10kHz内幅度最高的峰（避免高频噪声被误判为基频）
- 谐波窗口内无有效峰则 `valid=0`，不绘制

### `hardware/ui.c` — `UI_DrawFFT()` 重写峰值渲染

**竖线高度：** 从峰顶Y画到基线，不再全屏贯穿。

```c
/* 改前: 全屏贯穿 */
LCD_DrawLine(pk_x[i], FFT_PLOT_TOP, pk_x[i], FFT_PLOT_BOT, UI_FFT_PEAK_COLOR);

/* 改后: 峰顶到基线 */
LCD_DrawLine(pk_x[i], py, pk_x[i], FFT_PLOT_BOT, UI_FFT_PEAK_COLOR);
```

**擦除逻辑：** 只擦旧峰的实际竖线区域（旧 py 到基线），不再擦全屏。

**频率标签：** 画在竖线右侧5px处，品红色，格式如 `"1.0kHz"`。若x>180则标签画在竖线左侧。

**差分缓存：** 记录上帧 `HarmonicPeak_t` 数组（最多3个），下帧先擦旧再画新。

### Files NOT Changed

- `hardware/fft.c` — `FFT_Process()`、`FFT_ComputeMagnitude()` 不变
- `hardware/oscilloscope.c` / `.h` — 不涉及
- `Core/Src/main.c` — FFT调用点不变（已有 `g_osc.adc_buf` 传参）

## Data Flow (post-change)

```
33ms display tick (FFT模式):

  FFT_Process(adc_buf, buf_len, dma_last_pos, sample_rate)
    → g_fft_result.mag[], max_mag, peak_bin, peak_freq

  FFT_FindHarmonics(&g_fft_result, sample_rate, harmonics_out)
    → harmonics[3]: {freq, db, x, y, valid}

  UI_DrawFFT(&g_fft_result, harmonics_out)
    ├─ 擦除上帧峰值标记（竖线+标签区域，按实际高度）
    ├─ 画dB参考线
    ├─ 画FFT频谱基线（保留完整频谱作为背景）
    ├─ 画3个谐波峰值标记：
    │   竖线(峰顶y→基线) + 三角箭头 + 频率标签
    └─ 缓存本帧标记供下帧擦除
```

## Testing Plan

1. **方波基频+谐波：** 输入1kHz方波 → FFT模式 → 应显示1.0kHz、3.0kHz、5.0kHz标记，竖线高度递减
2. **正弦波单峰：** 输入1kHz正弦波 → FFT模式 → 只显示1.0kHz基频标记（3次5次谐波无峰，不显示）
3. **无信号：** 断开输入 → 不显示任何谐波标记，只显示频谱基线
4. **模式切换：** 波形↔FFT反复切换 → 标记正确刷新，无残留
5. **时基切换：** 旋转时基编码器 → FFT随采样率变化正常刷新
