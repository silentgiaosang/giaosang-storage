# Dual ADC Independent Sampling Design

**Date:** 2026-06-11
**Status:** Approved
**Target:** STM32F407VET6 — Scope firmware

## Goal

Split the two oscilloscope channels from single-ADC scan mode (ADC1 scanning CH14→CH15) into **two independent ADCs** (ADC1 on PC4, ADC2 on PB1), both triggered by the same TIM2 TRGO. Each channel gets the full sample rate instead of half.

## Motivation

- **Current bottleneck:** Single ADC1 scans two channels sequentially. Each TIM2 TRGO fires two conversions, so effective per-channel sample rate = TRGO rate / 2 (max ~700kHz shared, ~350kHz per channel).
- **Target:** ADC1 and ADC2 convert in parallel on each TRGO edge. Per-channel rate = TRGO rate (each ADC's single-channel limit ~1.4MHz at 12-bit).

## Data Structure Change

### Before
```c
#define OSC_ADC_BUF_SIZE  4096U
uint16_t adc_buf[OSC_ADC_BUF_SIZE];   // interleaved [CH0, CH1, CH0, CH1, ...]
```

### After
```c
#define OSC_ADC_BUF_SIZE  2048U
uint16_t adc_buf[2][OSC_ADC_BUF_SIZE]; // CH0 and CH1 independent
```

Memory footprint unchanged (4096 × 2 bytes = 8KB). Each channel gets its own circular DMA buffer of 2048 half-words.

## Hardware Configuration

### ADC1
| Parameter | Value |
|-----------|-------|
| Instance | ADC1 |
| Channel | IN14 (PC4) |
| Scan mode | Disabled (single channel) |
| Trigger | TIM2 TRGO, rising edge |
| DMA | DMA2 Stream0, Channel 0, circular |
| Resolution | 12-bit |
| Clock | PCLK2/4 (21 MHz) |

### ADC2 (new)
| Parameter | Value |
|-----------|-------|
| Instance | ADC2 |
| Channel | IN9 (PB1) |
| Scan mode | Disabled (single channel) |
| Trigger | TIM2 TRGO, rising edge |
| DMA | DMA2 Stream2, Channel 1, circular |
| Resolution | 12-bit |
| Clock | PCLK2/4 (21 MHz) |

Both ADCs fire on the same TIM2 TRGO edge — conversions are hardware-synchronized.

### DMA Stream Allocation

| Stream | Channel | Peripheral | Destination |
|--------|---------|------------|-------------|
| DMA2_Stream0 | 0 | ADC1 | `adc_buf[0]` |
| DMA2_Stream2 | 1 | ADC2 | `adc_buf[1]` |

Both DMA streams are configured identically: circular mode, half-word transfers, same NDTR.

## Code Change Inventory

### `oscilloscope.h`
- `OSC_ADC_BUF_SIZE` 4096 → 2048
- `adc_buf[4096]` → `adc_buf[2][OSC_ADC_BUF_SIZE]`
- `OSC_MAX_SAMPLE_HZ` 700kHz → ~1.4MHz (single-ADC limit)

### `adc.h` / `adc.c`
- **Add** `extern ADC_HandleTypeDef hadc2;`
- **Add** `extern DMA_HandleTypeDef hdma_adc2;`
- **Add** `void MX_ADC2_Init(void);` — configures ADC2_IN9 (PB1), DMA2_Stream2
- **Modify** `MX_ADC1_Init()`: Remove CH15 (PC5), keep only CH14 (PC4) single-channel
- **Modify** `HAL_ADC_MspInit()`: ADC1 section uses only `GPIO_PIN_4` (was `GPIO_PIN_4 | GPIO_PIN_5`)
- **Add** ADC2 section in `HAL_ADC_MspInit()`: Enable GPIOB clock, init PB1 as analog
- **Add** ADC2 section in `HAL_ADC_MspDeInit()`: DeInit PB1, disable ADC2 clock

### `oscilloscope.c`

| Function | Change |
|----------|--------|
| `Osc_ADC_DMA_Start()` | Start both ADCs: `HAL_ADC_Start_DMA(&hadc1, adc_buf[0], ...)` and `&hadc2, adc_buf[1], ...` |
| `Osc_ADC_DMA_Stop()` | Stop both ADCs |
| `Osc_DMA_GetWritePos()` | Read NDTR from either DMA (both in sync); return normalised 0..2047 |
| `Osc_ScanTrigger()` | Linear scan over `adc_buf[OSC_CH0]` (no step=2, no even-index alignment) |
| `Osc_Capture()` | Extract CH0 from `adc_buf[0]`, CH1 from `adc_buf[1]` directly (no `*2`, no interleaving math) |
| `Osc_DoMeasurements()` | Frequency measurement: read each channel's own buffer linearly |
| `Osc_AutoSet()` | Scan `adc_buf[OSC_CH0]` directly |
| `Osc_AutoTimebase()` | Update `OSC_MAX_SAMPLE_HZ` constant |

### `fft.c` / `fft.h`
- `FFT_Process()` signature unchanged, but internal logic simplified: drop `ch_off` alignment and `step=2` skip; read `adc_buf[ch][idx]` directly
- `FFT_SetChannel()` unchanged

### `main.c`
- Add `MX_ADC2_Init();` in initialization sequence (before `Osc_Init()`)

### `Core/Src/gpio.c`
- Update comment: `PC4=CH0, PC5=CH1` → `PC4=CH0(ADC1), PB1=CH1(ADC2)`

### `dma.h` / `dma.c` (CubeMX)
- CubeMX will regenerate these with ADC2 DMA handle. Manual edits may be needed if CubeMX is not re-run.

## Data Flow (post-change)

```
TIM2 TRGO (rising edge)
    │
    ├─→ ADC1 (CH14/PC4) → conversion → DMA2_Stream0 → adc_buf[0][...]
    │
    └─→ ADC2 (CH9/PB1)  → conversion → DMA2_Stream2 → adc_buf[1][...]

33ms display tick:
    Osc_Capture()
      ├─ Osc_DMA_GetWritePos() → current DMA write position
      ├─ Osc_ScanTrigger(adc_buf[0], ...) → find trigger in CH0
      ├─ Copy OSC_DISP_WIDTH samples:
      │     disp_buf[0][i] = adc_buf[0][(start + i) % 2048]
      │     disp_buf[1][i] = adc_buf[1][(start + i) % 2048]
    Osc_DoMeasurements()
      ├─ CH0 stats from disp_buf[0]
      ├─ CH1 stats from disp_buf[1]
      ├─ CH0 frequency from adc_buf[0][trig_pos..]
      └─ CH1 frequency from adc_buf[1][trig_pos..]
    UI_DrawWaveform / UI_DrawFFT
```

## Error Handling

- `MX_ADC2_Init()` follows same convention as `MX_ADC1_Init()` — calls `Error_Handler()` on HAL failure.
- If ADC2 fails to start, `Osc_ADC_DMA_Start()` should stop ADC1 before calling `Error_Handler()` to avoid half-running state.

## Testing Plan

1. **Smoke:** Power on, verify LCD renders waveform view with both channels
2. **AUTO:** Press AUTO button, verify auto-set works (timebase + trigger level)
3. **Encoder:** Rotate timebase and V/div encoders, verify display updates
4. **FFT:** Switch to FFT mode, verify spectrum renders on both channels
5. **Coupling:** Toggle CH1/CH2 DC/AC, verify relay toggles
6. **Test signal:** Apply external 1kHz square wave to CH0, verify correct waveform display
7. **Frequency:** Verify measured frequency matches external test signal

## Files NOT Changed

- `hardware/lcd.c` / `lcd.h` — display layer, only reads `disp_buf`
- `hardware/ui.c` / `ui.h` — rendering, only reads `disp_buf` and `g_osc`
- `Core/Src/gpio.c` / `tim.c` / `spi.c` — peripherals, no ADC dependency (gpio.c comment-only)
- `Core/Src/stm32f4xx_it.c` — no ADC ISR (DMA-driven)
