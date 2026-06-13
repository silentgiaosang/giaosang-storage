# Button Remap: K2/K3 功能重映射设计

## 概述

重新分配 K2、K3 按键功能，使耦合切换和触发通道选择更符合使用习惯。

## 改动前

| 按键 | 功能 |
|------|------|
| K2 (PD11) | CH1 耦合切换 (DC/AC) |
| K3 (PD9)  | CH2 耦合切换 (DC/AC) |

触发源固定为 CH1，触发标记固定显示在 CH0 波形上。

## 改动后

| 按键 | 功能 |
|------|------|
| K2 (PD11) | 触发通道切换 (CH0 ↔ CH1) |
| K3 (PD9)  | 双通道同步耦合切换 (以 CH2 为准) |

---

## 改动点

### 1. K3 — 双通道同步耦合切换

**文件：** `Core/Src/main.c`

**行为：** 按下 K3 → CH2 翻转耦合 → CH1 立即同步为 CH2 相同状态，两通道始终保持一致。

**硬件同步：** 继电器 PE0 (CH1) 和 PE11 (CH2) 同步控制，`coupling_dc[0]` 和 `coupling_dc[1]` 同步更新。

```c
else if (i == 2)  /* K3: 双通道同步耦合切换 */
{
    Osc_ToggleCoupling(OSC_CH1);                       // CH2翻转
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_11);            // CH2继电器
    g_osc.coupling_dc[OSC_CH0] = g_osc.coupling_dc[OSC_CH1]; // CH1同步
    if (g_osc.coupling_dc[OSC_CH0])
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET);
}
```

**副作用分析：** UI 状态栏读取 `coupling_dc[]` 显示 DC/AC，无需额外修改。波形显示通过 `ADC_BIAS_DC/ADC_BIAS_AC` 自动适配，无需额外修改。

### 2. K2 — 触发通道切换

#### 2.1 新增字段

**文件：** `hardware/oscilloscope.h`

在 `Oscilloscope_t` 结构体触发状态区新增：

```c
uint8_t trig_channel;  /* 触发源通道: 0=CH0, 1=CH1 */
```

默认值 `1` (CH1)，保持兼容原有行为。

#### 2.2 触发扫描改造

**文件：** `hardware/oscilloscope.c`

`Osc_ScanTrigger()` 中，将硬编码的 `OSC_CH1` 改为读取 `g_osc.trig_channel`：

```c
// 原来: uint16_t *buf = g_osc.adc_buf[OSC_CH1];
// 改为:
uint16_t *buf = g_osc.adc_buf[g_osc.trig_channel];
```

`Osc_Init()` 中追加初始化：

```c
g_osc.trig_channel = OSC_CH1;
```

**时间对齐说明：** CH0 和 CH1 由同一 TIM2 TRGO 同步触发采样，DMA 缓冲区位置一一对应。触发扫描位置 `trig_pos` 与通道无关，`Osc_Capture` 中双通道数据提取共用同一 `start_adc`，无需修改。

#### 2.3 K2 按键处理

**文件：** `Core/Src/main.c`

```c
else if (i == 1)  /* K2: 切换触发通道 */
{
    g_osc.trig_channel = !g_osc.trig_channel;
    UI_ResetStatusBar();
}
```

#### 2.4 触发标记动态分配

**文件：** `Core/Src/main.c`

`UI_DrawWaveform` 的 `use_trigger` 参数从硬编码改为按 `trig_channel` 动态判断：

```c
UI_DrawWaveform(OSC_CH0, g_osc.disp_buf[OSC_CH0],
                OSC_PRE_TRIG, g_osc.trig_found,
                g_osc.trig_channel == OSC_CH0);   // 仅触发通道画标记
UI_DrawWaveform(OSC_CH1, g_osc.disp_buf[OSC_CH1],
                0, 0,
                g_osc.trig_channel == OSC_CH1);
```

#### 2.5 状态栏显示

**文件：** `hardware/ui.c`

Line1 触发信息中追加通道号，例如 `AR[0]` 表示 CH0 触发，`AR[1]` 表示 CH1 触发：

```c
snprintf(line1, sizeof(line1), "%s%c %c%c[%d] %s %s",
         tdiv_str,
         osc->auto_tb ? 'A' : 'M',
         (osc->trig_mode == TRIG_AUTO)   ? 'A' :
         (osc->trig_mode == TRIG_NORMAL) ? 'N' : 'S',
         (osc->trig_edge == EDGE_RISING) ? 'R' : 'F',
         osc->trig_channel,
         mode_str,
         g_vscale_table[osc->vdiv].label);
```

触发调节模式下同样追加通道号。

#### 2.6 头文件注释更新

**文件：** `hardware/oscilloscope.h`

```c
uint8_t       trig_channel;   /* 触发源通道(0=CH0, 1=CH1) */
```

---

## 涉及文件汇总

| 文件 | 改动 |
|------|------|
| `Core/Src/main.c` | K2→触发通道切换，K3→双通道耦合切换，UI_DrawWaveform 调用更新 |
| `hardware/oscilloscope.h` | 新增 `trig_channel` 字段，更新注释 |
| `hardware/oscilloscope.c` | `Osc_ScanTrigger` 按通道扫描，`Osc_Init` 初始化默认值 |
| `hardware/ui.c` | 状态栏 Line1 显示触发通道 |
| `README.md` | 更新按键功能表 |

---

## 不变项

- K1 (AUTO-SET)、K4 (MODE)、K5 (触发调节/FFT通道)、K6 (自校正) 功能不变
- SW2/SW3 编码器功能不变
- 触发模式/边沿/电平调节逻辑不变
- FFT 模式不受影响
