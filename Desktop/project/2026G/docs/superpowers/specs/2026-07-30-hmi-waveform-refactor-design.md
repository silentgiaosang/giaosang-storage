# HMI-Style Waveform Display Refactor

**Date:** 2026-07-30
**Reference:** HMI project at `C:\Users\giao\Desktop\project\HMI`
**Chip:** STM32F407VETx (168MHz)

## Overview

Refactor the 2026G signal measurement system to match the HMI project's waveform display pattern: use `add` command (point-by-point ASCII) instead of the current `addt` transparent mode, reorganize into layered architecture, enable continuous measurement with button-driven display refresh.

---

## 1. Architecture: Layered Design

```
measure.c (DSP)  ──→  wavegen.c (synthesis)  ──→  main_app.c (control)  ──→  tjc_screen.c (display)
```

| File | Status | Responsibility |
|------|--------|----------------|
| `hardware/ad9220.c/h` | No change | ADC DMA acquisition, TIM1 clock |
| `hardware/measure.c/h` | Rework | DSP only: FFT, Vpp/Vrms, peak detect → produces `MeasureResult_t` + `PeakResult_t` |
| `hardware/wavegen.c/h` | **New** | 4 waveform synthesis functions, pure math, no HW dependencies |
| `hardware/tjc_screen.c/h` | Rework | `add` command per-point ASCII send, clear graph, parameter text |
| `hardware/main_app.c/h` | Rework | Button dispatch, display mode/cycle state, connects measure→wavegen→screen |
| `hardware/app_interface.h` | Minor | Data structs (likely unchanged) |

**Data flow:**
```
measure.c continuously acquires → stores latest result globally
                                    │
main_app.c button events ──→ reads latest result → calls wavegen → calls tjc_screen
```

- `measure.c` never calls display functions
- `wavegen.c` never reads hardware
- `main_app.c` is the sole scheduler connecting them

---

## 2. Button & Mode Logic

**State variables (main_app.c):**

| Variable | Type | Description |
|----------|------|-------------|
| `g_disp_mode` | `DispMode_t` | `MODE_WAVEFORM` or `MODE_SPECTRUM` |
| `g_cycle` | `Cycle_t` | `CYC_1` or `CYC_3` |
| `g_wave_type` | `WaveType_t` | Updated each measurement cycle: `WAVE_SINE/SQUARE/TRIANGLE/HARMONIC` |

**Button actions:**

| Button | ID | Action |
|--------|----|--------|
| b_start (开始测量) | 0x0A | Switch to waveform mode + 3-cycle → refresh |
| b_wave (输出波形) | 0x0B | Switch to waveform mode + 3-cycle → refresh |
| b_spec (输出频谱) | 0x0C | Switch to spectrum mode → refresh |
| b_cyc1 (1周期) | 0x0D | Set CYC_1 → refresh if in waveform mode |
| b_cyc3 (3周期) | 0x0E | Set CYC_3 → refresh if in waveform mode |

- b_wave/b_spec are **mode toggles**, they do not trigger measurement
- b_cyc1/b_cyc3 only affect cycle count, take effect immediately in waveform mode
- Measurement runs continuously in the background; buttons only control **what** is displayed

---

## 3. Continuous Measurement (measure.c)

**Before:** State-machine triggered by `trigger_pending` flag, one-shot.

**After:** Continuous auto-loop every 1 second:

```
Measure_Process() every ~1s:
  1. AD9220_Start(coarse tier) → wait DMA done
  2. FFT → auto-select tier (low/mid/high)
  3. AD9220_Start(fine tier) → wait DMA done
  4. FFT + Vpp + Vrms + peak analysis
  5. detect_wave_type()
  6. Update global g_latest_result, g_latest_peaks, g_latest_type
  7. Set data-ready flag
```

**New public API:**

| Function | Description |
|----------|-------------|
| `Measure_Init()` | Init ADC + FFT + Hanning window (unchanged) |
| `Measure_Process()` | Called from main loop, internal auto-timing |
| `Measure_GetLatest(result, peaks, type)` | Lock-free copy of latest data |
| `Measure_DataReady()` | Returns 1 when new data available |

**Removed:** `Measure_Trigger()`, `App_MeasureRequested()` — no more manual trigger paths.

---

## 4. Waveform Generator (wavegen.c — New File)

**Unified function signature:**
```c
uint16_t WaveGen_Sine(    float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r);
uint16_t WaveGen_Square(  float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r);
uint16_t WaveGen_Triangle(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r);
uint16_t WaveGen_Harmonic(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r, PeakResult_t *p);
```

**Dispatcher (called by main_app):**
```c
uint16_t WaveGen_Generate(float *buf, uint16_t buf_len, WaveType_t type,
                          uint8_t cycles, MeasureResult_t *r, PeakResult_t *p);
```

**Algorithms:**

| Type | Method |
|------|--------|
| Sine | `r->vpp_mv/2 * sin(2π * i * cycles / buf_len)` |
| Square | Positive half = `+Vpp/2`, negative half = `-Vpp/2` |
| Triangle | Linear ramp up/down, scaled to Vpp |
| Harmonic | Sum of up to 3 sines at measured frequencies/amplitudes from `PeakResult_t` |

All use `MeasureResult_t` for Vpp and frequency; Harmonics additionally uses `PeakResult_t`.

**Fixed point count:** 600 points per frame (matches HMI reference).

---

## 5. Display Protocol (tjc_screen.c)

**Switch from `addt` to `add` command (HMI reference pattern):**

```
cle s0.id,0\xff\xff\xff              ← clear channel 0
add s0.id,0,<val1>\xff\xff\xff       ← per-point, ASCII value, FF FF FF terminator
add s0.id,0,<val2>\xff\xff\xff
...
```

**Implementation:**
```c
void TJC_DrawWaveform(uint16_t *buf, uint16_t len)
{
    TJC_ClearGraph();
    for (int i = 0; i < len; i++) {
        char cmd[32];
        int n = sprintf(cmd, "add s0.id,0,%d\xff\xff\xff", buf[i]);
        HAL_UART_Transmit(&huart6, (uint8_t *)cmd, n, HAL_MAX_DELAY);
    }
}
```

**Unchanged functions:** `TJC_UpdateParams()`, `TJC_SetStatus()`, `TJC_DrawSpectrum()` (spectrum mode preserved).

**Note:** Waveform transmission is blocking (600 points × ~15 chars = ~9KB at baud rate). Touch events are not processed during transmission.

---

## 6. App_Loop Flow (main_app.c)

```c
void App_Loop(void)
{
    Measure_Process();   // internal auto-timer, non-blocking when idle

    if (Measure_DataReady()) {
        MeasureResult_t r;
        PeakResult_t p;
        WaveType_t type;
        Measure_GetLatest(&r, &p, &type);
        g_wave_type = type;

        if (g_disp_mode == MODE_WAVEFORM)
            RefreshWaveform();
        else
            RefreshSpectrum();
    }
}

void RefreshWaveform(void)
{
    float buf[600];
    uint16_t n = WaveGen_Generate(buf, 600, g_wave_type, g_cycle, &g_latest_result, &g_latest_peaks);
    // Scale buf to screen Y coordinates (0~GRAPH_H)
    // TJC_DrawWaveform(scr_buf, n);
    // TJC_UpdateParams(&g_latest_result);
    // TJC_SetStatus(...);
}
```

---

## 7. What Does NOT Change

- `ad9220.c/h` — ADC hardware layer
- FFT library usage (CMSIS-DSP `arm_rfft_fast_f32`)
- `PeakResult_t` and `MeasureResult_t` structs
- TJC touch event packet parsing (`TJC_RxByteCallback`)
- Spectrum display logic
- `app_interface.h` data structures
