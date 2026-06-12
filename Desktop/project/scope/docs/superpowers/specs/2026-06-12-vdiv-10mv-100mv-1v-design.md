# V/div 档位调整：10mV / 100mV / 1V

**Date:** 2026-06-12
**Status:** Approved
**Target:** STM32F407VET6 — Scope firmware

## Goal

将垂直灵敏度档位从当前的 2V/1V/500mV 三档改为 **10mV / 100mV / 1V** 三档，纯软件改动，不涉及硬件。

## Motivation

当前档位（500mV–2V）适合大信号测量。改为 10mV–1V 覆盖更低幅度的小信号场景。

## Code Change Inventory

### `hardware/oscilloscope.h` — 枚举

```c
typedef enum {
    VSCALE_10MV = 0,    /* 10mV/div  */
    VSCALE_100MV,       /* 100mV/div */
    VSCALE_1V,          /* 1V/div    */
    VSCALE_NUM
} OscVScale_t;
```

### `hardware/oscilloscope.c` — 参数表

```c
const VScaleEntry_t g_vscale_table[VSCALE_NUM] = {
    {   62, "10mV"  },     /* 满屏=0.05V (5div×10mV)                  */
    {  620, "100mV" },     /* 满屏=0.5V  (5div×100mV)                 */
    { 4095, "1V"    },     /* 满屏=3.3V  (ADC满量程,钳位)             */
};
```

adc_span 计算：`(5div × VperDiv) / 3.3V × 4095`

### `hardware/oscilloscope.c` — 默认值

```c
g_osc.vdiv = VSCALE_1V;    /* 默认1V/div */
```

## Files NOT Changed

- `hardware/ui.c` / `ui.h` — `_adc_to_y()` 通过 `adc_span` 参数自适应，无需修改
- `hardware/oscilloscope.c` — `Osc_SetVScale()` / `Osc_GetAdcSpan()` 通用逻辑不变
- `hardware/oscilloscope.c` — `Osc_AutoSet()` 不涉及 V/div 调整，不动
- 所有其他文件 — 不动

## 已知限制

- **10mV/div 档**：满屏仅 62 个 ADC 计数，波形呈现明显量化台阶。这是 12-bit ADC 无前端放大的硬件极限，不影响功能正确性。
