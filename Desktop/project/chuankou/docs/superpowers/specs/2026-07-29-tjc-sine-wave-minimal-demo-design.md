# TJC 串口屏一周期正弦波最小示例 — 设计文档

## 概述

在现有 STM32F4 + TJC 串口屏工程基础上，将 `main.c` 简化为一个纯净的最小正弦波示例：生成一周期正弦波数据（600 点），通过 USART6 以 `addt` 透传模式持续循环发送到 TJC 屏幕的波形控件。

## 架构

```
SystemClock_Config() → MX_GPIO_Init() → MX_USART6_UART_Init()
                           ↓
                   预生成正弦波 600 点 (0~255)
                           ↓
                    while(1) 循环:
                      TJC_SendCmd("addt s0.id,0,600")
                      DelayMs(100)
                      TJC_SendRaw(wave_buf, 600)
                      DelayMs(550)
```

## 简化范围

相比现有 `main.c`，去掉以下内容：

| 去掉 | 原因 |
|------|------|
| USART1 初始化 / printf 重定向 | 最小示例不需要调试串口 |
| USART6 RX 中断 / `HAL_UART_RxCpltCallback` | 不需要按钮交互 |
| 按钮事件处理逻辑 | 同上 |
| 复合波形 (10kHz + 50kHz 谐波) | 简化为纯正弦波 |
| `GenSineWave()` 独立函数 | 内联到 main，减少跳转 |
| `fputc`/`fgetc` 重定向 | 无调试输出需求 |
| `tjc_rx_buf` / `tjc_frame_ready` 等 RX 变量 | 无 RX 需求 |

## 保留内容

- `TJC_SendCmd()` — 发送 ASCII 指令 + FF FF FF
- `TJC_SendRaw()` — 发送二进制数据
- `DelayMs()` — 毫秒延时
- USART6 初始化（TX: PC6, RX: PC7, 115200 baud）

## 正弦波参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 点数 | 600 | 填满 s0 控件宽度 |
| 周期数 | 1 | 一个完整正弦周期 |
| 值域 | 0~255 | 控件底部=0, 顶部=255 |
| 中值 | 127.5 | 直流偏移 |
| 幅度 | 127 | 峰峰值 254 |

生成公式: `val = 127.5 + 127.0 * sin(2π * i / 600)`, i = 0..599

## 发送时序

每帧约 650ms：
- `addt` 指令: 100ms
- 透传 600 字节: ~52ms（115200 baud, 10 bit/byte）
- 帧间延时: 500ms

## 文件改动

| 文件 | 操作 |
|------|------|
| `Core/Src/main.c` | 重写 |

不修改 `usart.c`、`usart.h`、`gpio.c`、`gpio.h` 等文件。

## TJC 屏幕前提

- 页面中已放置波形控件，ID 为 `s0`
- 控件宽度 ≥ 600px
- 控件高度建议 ≥ 255px
- 屏幕与 STM32 通过 USART6 (PC6=TX, PC7=RX) 连接
