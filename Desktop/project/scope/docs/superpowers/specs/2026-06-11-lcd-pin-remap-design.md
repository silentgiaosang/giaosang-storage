# LCD Pin Remap & DAC Removal Design

**Date:** 2026-06-11
**Status:** Approved
**Target:** STM32F407VET6 — Scope firmware

## Goal

Remap the three LCD control pins (CS, DC, RST) and remove the unused DAC test signal peripheral as part of a PCB layout change.

## Pin Map Changes

| Signal | Old Pin | New Pin | Notes |
|--------|---------|---------|-------|
| LCD CS | PB8 | **PA3** | GPIO output (previously unused) |
| LCD DC | PB9 | **PA4** | GPIO output (was DAC_OUT1) |
| LCD RST | PA9 | **PA6** | GPIO output (was SPI1_MISO) |

The LCD uses 3-wire SPI (SCK + MOSI only), so SPI1_MISO was unused and PA6 is free to reclaim.

DAC is completely removed — the 1kHz test square wave function is no longer needed.

## Code Change Inventory

### `hardware/lcd.h`
- `LCD_CS_PORT` / `LCD_CS_PIN`: PB8 → PA3
- `LCD_DC_PORT` / `LCD_DC_PIN`: PB9 → PA4
- `LCD_RST_PORT` / `LCD_RST_PIN`: PA9 → PA6

### `hardware/lcd.c`
- `LCD_Init()`: Add explicit PA6 GPIO init as push-pull output (RST), since it is no longer initialized as MISO by SPI MspInit
- PA4 (DC): Also needs explicit GPIO init, since it was previously initialized as analog by DAC MspInit

### `Core/Src/spi.c`
- `MX_SPI1_Init()`: Change `SPI_DIRECTION_2LINES` to `SPI_DIRECTION_1LINE` (TX only, no MISO needed)
- `HAL_SPI_MspInit()`: Remove PA6 from SPI1 GPIO init (PA5 SCL, PA7 MOSI remain)
- `HAL_SPI_MspDeInit()`: Remove PA6 from GPIO deinit

### `Core/Src/main.c`
- Remove `#include "dac.h"`
- Remove `MX_DAC_Init()` call from init sequence
- Remove `DAC_TestSignal_Init()` and `DAC_TestSignal_Process()` functions (entire USER CODE 4 section)
- Remove `DAC_TEST_FREQ_HZ` define
- Remove `dac_high`, `dac_accum_us`, `last_dac_ms` static variables
- Remove `DAC_TestSignal_Process()` call from main while loop

### `Core/Inc/dac.h` / `Core/Src/dac.c`
- Delete both files

### `Core/Src/gpio.c`
- Update comments: remove references to PA9/LCD and PA6/SPI1_MISO

### MDK-ARM project
- Remove `dac.c` from source group (Keil project file `scope.uvprojx`)

## Files NOT Changed

- `oscilloscope.c` / `oscilloscope.h` — no dependency on DAC or LCD pins
- `fft.c` / `fft.h` — no dependency
- `ui.c` / `ui.h` — no dependency
- `adc.c` / `adc.h` — no dependency
- `tim.c` / `tim.h` — no dependency
- `dma.c` / `dma.h` — no dependency

## Testing Plan

1. **Power-on smoke:** LCD lights up, shows waveform grid and status bar
2. **Display refresh:** Waveform and FFT views render correctly on LCD
3. **No DAC:** Verify no test square wave is generated (expected — DAC removed)
4. **Buttons/encoders:** All controls work (MODE, AUTO, encoders, K1-K6)
5. **ADC sampling:** Both channels still sample and display correctly
