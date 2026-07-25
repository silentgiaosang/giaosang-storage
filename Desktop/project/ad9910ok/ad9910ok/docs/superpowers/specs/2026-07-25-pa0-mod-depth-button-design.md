# PA0 Button: Modulation Depth Cycling

**Date**: 2026-07-25
**Status**: Approved

## Overview

Add push-button on PA0 to cycle AM modulation depth through 6 preset values. Each short press advances to the next depth. On power-up, default depth is 80%.

## Requirements

- **Button**: PA0, input with internal pull-up (press = LOW)
- **Range**: 30% to 80%, step 10% → 6 levels: [0.30, 0.40, 0.50, 0.60, 0.70, 0.80]
- **Operation**: Short press cycles forward (30→40→…→80→30…)
- **Default**: 80% (index 5) at power-up
- **Debounce**: 20ms delay with double-sampling
- **Output behavior**: Brief interruption during re-init is acceptable (reuse existing `AD9910_RAM_AM_Init()`)
- **Anti-repeat**: Wait for button release before accepting next press

## Architecture

```
PA0 (GPIO Input, pull-up)
       │
       ▼
  main() polling loop (every 10ms)
       │
       ├── button released → no-op
       └── button pressed  → debounce 20ms → confirm → advance index → AD9910_RAM_AM_Init() → wait release
```

## Files Changed

| File | Change |
|------|--------|
| `AD9910.ioc` | Add PA0 as GPIO_Input with pull-up |
| `Core/Src/gpio.c` | Generate PA0 input init code (CubeMX) |
| `Core/Src/main.c` | Add depth table, button polling loop, state machine |

## Key Implementation Details

### Depth Table & State
```c
static const float mod_depth_table[] = {0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f};
#define DEPTH_COUNT 6
static uint8_t depth_index = 5;  // default 80%
```

### Main Loop Logic
```c
while (1) {
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
        HAL_Delay(20);  // debounce
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
            depth_index = (depth_index + 1) % DEPTH_COUNT;
            AD9910_RAM_AM_Init(35000000, 2000000, mod_depth_table[depth_index]);
            while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET);  // wait release
        }
    }
    HAL_Delay(10);
}
```

## Carriers & Mod Frequency

- Carrier: 35 MHz (unchanged from existing)
- Mod: 2 MHz (unchanged from existing)

## What Stays the Same

- `AD9910_RAM_AM_Init()` — no changes to AD9910 driver
- SPI, PLL, CFR register configuration
- All other GPIO pin assignments
- All other AD9910 control signals

## Edge Cases

1. **Button held down**: `while(PRESSED)` blocks until release — prevents rapid-fire cycling
2. **Bounce on release**: harmless, next 10ms poll catches stable high
3. **Power glitch during re-init**: AD9910_RAM_AM_Init does full reset, inherently self-recovering
