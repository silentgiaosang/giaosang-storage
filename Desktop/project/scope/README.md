# Scope - 双通道数字示波器（STM32F407）

基于 **STM32F407VET6** 微控制器与 **240×320 ST7789V TFT LCD** 的实时双通道数字示波器，集成 FFT 频谱分析功能。

![Platform](https://img.shields.io/badge/platform-STM32F407VET6-blue)
![Display](https://img.shields.io/badge/display-ST7789V%20240×320-green)
![DSP](https://img.shields.io/badge/dsp-CMSIS--DSP-orange)

---

## 功能特性

### 示波器模式
- **双通道**独立同步采样：ADC1（PC4）+ ADC2（PB1）
- **13档时基**：5μs/div ~ 200ms/div
- **3档垂直灵敏度**：10mV/div、100mV/div、1V/div
- **触发模式**：自动（Auto）、正常（Normal）、单次（Single），支持上升沿/下降沿
- **DC/AC耦合切换**：硬件继电器控制（PE0、PE11），每通道独立
- **一键AUTO-SET**：自动配置最佳时基与触发电平
- **实时测量**：Vpp（峰峰值）、Vavg（平均值）、Vmin/Vmax、频率、周期、占空比
- **DMA循环双缓冲**采样，最高约 1.4Msps
- **自动时基**：始终保持屏幕显示约 3 个周期波形

### FFT 频谱分析模式
- **512点 FFT**：基于 CMSIS-DSP 库（`arm_rfft_fast_f32`）
- **Hanning窗**加窗处理，减少频谱泄漏
- **波形类型识别**：正弦波、方波、三角波、锯齿波、未知波形
- **自适应谐波显示**：根据识别的波形类型显示对应谐波分量
- **峰值标记**：dB 比例高度的品红色峰值标线，带频率标注
- **EMA 平滑**：幅度指数移动平均，显示稳定不抖动
- **-80dB 底噪**：智能峰值检测算法
- **频率范围**：0.5Hz ~ 20kHz

### UI 与操作
- **双旋转编码器**：SW2 调时基、SW3 调垂直幅度（EXTI 中断驱动）
- **6个按键**：AUTO、CH1耦合、CH2耦合、MODE切换、触发调节/FFT通道、自校正
- **网格叠加**：10×10 格线，暗灰色网格
- **状态栏**：显示时基、V/div、耦合方式、触发信息、测量数据
- **通道配色**：CH0 黄色波形、CH1 绿色波形
- **增量刷新**：仅重绘变化区域，杜绝闪烁

---

## 硬件配置

| 组件 | 详情 |
|------|------|
| 主控 | STM32F407VET6（Cortex-M4F @ 168MHz） |
| 屏幕 | ST7789V 240×320 TFT LCD（4线 SPI） |
| ADC | ADC1（PC4）+ ADC2（PB1），双通道同步 |
| 触发定时器 | TIM2 TRGO 同步触发两个 ADC |
| DMA | DMA2 Stream0（ADC1）+ Stream2（ADC2），循环模式 |
| DAC | 可选测试方波输出（PA4） |
| 输入耦合 | 继电器切换 DC/AC（PE0、PE11） |
| 编码器 | 旋转编码器 ×2，EXTI 下降沿中断（PD0~PD3） |
| 按键 | 轻触按键 ×6（PD8~PD13） |

### 引脚映射

```
LCD:   SPI1 — PA5(SCK), PA7(MOSI), PA3(CS), PA4(DC), PA6(RST)
ADC1:  PC4（CH0 输入）
ADC2:  PB1（CH1 输入）
DAC:   PA4（测试信号输出）
ENC1:  PD2(A) + PD3(B) — 时基调谐
ENC2:  PD0(A) + PD1(B) — 垂直幅度
BTN:   PD13(K1/AUTO), PD11(K2/触发通道), PD9(K3/同步耦合),
       PD12(K4/MODE), PD10(K5/触发), PD8(K6/自校正)
RELAY: PE0(CH1耦合), PE11(CH2耦合)
```

---

## 固件结构

```
scope/
├── Core/
│   ├── Inc/                     # HAL 外设头文件
│   │   ├── adc.h, dac.h, dma.h, gpio.h, spi.h, tim.h
│   │   ├── main.h, stm32f4xx_hal_conf.h, stm32f4xx_it.h
│   └── Src/                     # HAL 外设源文件 + 中断服务
│       ├── main.c               # 主循环、IO处理、系统初始化
│       ├── adc.c, dac.c, dma.c, gpio.c, spi.c, tim.c
│       ├── stm32f4xx_hal_msp.c
│       ├── stm32f4xx_it.c       # 中断处理（编码器 EXTI）
│       └── system_stm32f4xx.c
├── hardware/                    # 应用层
│   ├── lcd.c / lcd.h            # ST7789V SPI LCD 驱动
│   ├── oscilloscope.c / .h      # 示波器核心引擎
│   ├── fft.c / fft.h            # FFT 频谱分析（CMSIS-DSP）
│   └── ui.c / ui.h              # UI 渲染、网格、波形与 FFT 绘制
├── Drivers/                     # STM32 HAL + CMSIS 库
│   ├── CMSIS/
│   └── STM32F4xx_HAL_Driver/
├── MDK-ARM/                     # Keil MDK 工程
│   └── scope.uvprojx
└── README.md
```

---

## 编译与烧录

### 环境要求
- **Keil MDK-ARM** 5.x
- **STM32F4xx 器件包**（含 CMSIS-DSP）
- 工程预定义宏：`ARM_MATH_CM4`

### 步骤
1. 在 Keil MDK 中打开 `MDK-ARM/scope.uvprojx`
2. 确保 RTE（Run-Time Environment）中已启用 CMSIS-DSP
3. 在 C/C++ → Preprocessor Symbols 中添加 `ARM_MATH_CM4`
4. 编译（F7）并通过 ST-Link / J-Link 烧录（F8）

---

## 操作说明

| 按键 | 功能 |
|------|------|
| **K1** | AUTO-SET — 自动配置时基与触发 |
| **K2** | 切换触发通道（CH0 ↔ CH1） |
| **K3** | 切换双通道耦合方式（DC/AC 同步） |
| **K4** | 切换显示模式（波形 ↔ FFT） |
| **K5** | 触发调节模式（波形视图）/ FFT 通道切换（FFT视图） |
| **K6** | 自校正 |
| **SW2** | 旋转调节时基（Timebase） |
| **SW3** | 旋转调节垂直灵敏度（V/div） |

### 显示模式
- **波形模式**：上半屏 CH0（黄色）+ 下半屏 CH1（绿色），含网格与测量参数
- **FFT 模式**：全屏频谱图，峰值标记 + 波形类型标签

---

## 关键技术点

- **采样策略**：TIM2 TRGO 同步触发双 ADC，采样率从 640Hz 到 1.4MHz 可调
- **DMA 机制**：循环 DMA 缓冲区 + 位置追踪，每次显示刷新（约 30fps）捕获最新完整数据
- **触发检测**：可选CH0/CH1触发源，硬件边沿检测 + 软件迟滞；Auto 模式超时后自动自由运行
- **FFT 处理**：CMSIS-DSP 实 FFT + Hanning 窗 → 20×log10 dB 转换 → EMA 平滑（α=0.25）→ 谐波峰值检测
- **波形识别**：分析谐波分布模式（有无及幅度比例），分类为正弦/方波/三角/锯齿波

---

## 更新日志

- **FFT 峰值抖动修复**：缓存屏幕坐标实现精确擦除 + K5 切换 FFT 通道
- **波形类型识别**：基于 FFT 的正弦/方波/三角/锯齿波分类，自适应谐波显示
- **增量峰值更新**：峰值 bin 与 dB 不变时跳过重绘，消除闪烁
- **Y 轴参考优化**：以 max_mag 为 0dB 基准，50dB 动态范围，频谱显示稳定
- **频率迟滞**：防止相邻 bin 间峰值频率标签来回跳动

---

## 许可证

本项目仅供学习与个人使用。

---

## 作者

**silentgiaosang** — [GitHub](https://github.com/silentgiaosang)
