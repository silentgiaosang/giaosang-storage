# AD9220 Voltage & Frequency Measurement Design

**Date:** 2026-07-29
**Chip:** STM32F407VETx (168MHz)
**ADC:** AD9220 12-bit parallel, 0-3.3V input

## Overview

Use AD9220 12-bit parallel ADC to measure signal Vpp (mV) and frequency (kHz). Results are printed via USART1 every 3 seconds. Adaptive sample rate ensures Vpp accuracy across 10kHz–500kHz input range.

---

## 1. Pin Mapping

| AD9220 | STM32F407 | Config |
|--------|-----------|--------|
| D0–D11 | PE0–PE11 | Input, no pull |
| CLK    | PA8       | AF push-pull (TIM1_CH1) |

- PE0–PE11 read as lower 12 bits of `GPIOE->IDR`
- PA8 outputs PWM square wave as AD9220 sample clock

## 2. DMA Architecture

**Channel:** DMA2 Stream 5 Channel 6 (TIM1_UP trigger)

| Parameter | Value |
|-----------|-------|
| Direction | Peripheral-to-Memory |
| Source | `(uint32_t)&GPIOE->IDR` (fixed) |
| Destination | `uint16_t buffer[4096]` (increment) |
| Transfer size | 4096 |
| Peripheral width | 32-bit |
| Memory width | 16-bit |
| Mode | Normal (single-shot) |

**Timing note:** AD9220 has pipeline delay — data for sample N stabilizes on DOUT after the N-th CLK rising edge (typically t_OD ≈ 13 ns max). TIM1 UEV fires at CNT=0, which coincides with PWM rising edge. DMA reads GPIOE at UEV, which may capture data from the *previous* sample rather than the just-clocked one. To compensate, DMA reads are offset by one sample in software (discard first sample, or accept 1-sample skew across 4096 points — negligible).

**Flow:**
1. Enable TIM1 → CLK output on PA8 starts
2. Enable DMA → one GPIOE read per TIM1 update event (UEV), triggered with a 1-cycle pipeline offset
3. DMA TC interrupt → stop TIM1, set flag
4. Main loop polls flag, processes 4096 samples, prints result
5. 3-second delay, repeat

## 3. Sample Rate Tiers

System clock 168 MHz, TIM1 prescaler = 0 (PSC=0). Rate set by ARR.

| Tier | Sample Rate | ARR | Freq Resolution (4096-pt FFT) |
|------|-------------|-----|-------------------------------|
| Low  | 200 kSPS    | 839 | 48.8 Hz |
| Mid  | 2 MSPS      | 83  | 488 Hz  |
| High | 9.88 MSPS   | 16  | 2.41 kHz |

CH1 PWM: mode 1, 50% duty (`CCR1 = ARR/2 + 1`).

### Auto-Detect Logic

```
1. Start with Mid (2 MSPS) → sample 4096 points
2. Run FFT → find dominant frequency
3. If f < 50 kHz  → switch to Low  (200 kSPS), resample
   If 50–200 kHz → keep Mid    (2 MSPS)
   If > 200 kHz  → switch to High (~10 MSPS), resample
4. Compute final Vpp + frequency from resampled data
```

## 4. FFT Frequency Computation

**Library:** CMSIS-DSP (`arm_rfft_f32`)

**Steps:**
1. Extract D0–D11 from `uint16_t` buffer → `float32_t fft_in[4096]`, subtract DC mean
2. Apply Hanning window (pre-computed or runtime via `arm_cos_f32`)
3. `arm_rfft_f32(&S, fft_in, fft_out)` — real FFT, outputs 2048 complex bins
4. `arm_cmplx_mag_f32(fft_out, mag, 2048)` — magnitude spectrum
5. Find max in `mag[1..2047]` (skip DC bin 0)
6. Frequency = `bin_index × (sample_rate / 4096)`
7. Parabolic interpolation on peak + adjacent bins for sub-bin accuracy

## 5. Vpp Computation

**Steps:**
1. Extract D0–D11, center data (subtract DC mean)
2. Moving average filter, window size = 5 → `float32_t filtered[4092]`
3. Scan for `vmax`, `vmin`
4. Vpp_mV = `(vmax - vmin) × 3300.0 / 4096.0`

AD9220 LSB = 3.3V / 4096 ≈ 0.8057 mV

## 6. Serial Output

USART1, baud rate as configured in CubeMX. Output every 3 seconds:

```
Freq: 125.032 kHz  Vpp: 1523 mV
```

Format string: `"Freq: %.3f kHz  Vpp: %.0f mV\r\n"`

## 7. File Layout

All new code goes under `hardware/`:

| File | Purpose |
|------|---------|
| `hardware/ad9220.h` | Public API: `void AD9220_Init(void)`, `void AD9220_Start(void)`, `uint8_t AD9220_DataReady(void)`, global buffer, result struct |
| `hardware/ad9220.c` | TIM1 config, DMA config, GPIO init, ISR handlers |
| `hardware/measure.c` | FFT processing, Vpp computation, auto-detect state machine |
| `hardware/measure.h` | `void Measure_Process(void)` — called from main loop |
| `main.c` | Add `Measure_Process()` call in while(1) |

CMSIS-DSP enabled via CubeMX (pin `CMSIS-DSP` library in project settings).

## 8. Processing Time Budget

- 4096-pt real FFT via CMSIS-DSP on M4 @ 168 MHz: ~2–5 ms (well within 3 s interval)
- Moving average + min/max scan: negligible (<1 ms)
- Total worst-case (two samples: coarse + fine): ~10 ms ≪ 3000 ms
