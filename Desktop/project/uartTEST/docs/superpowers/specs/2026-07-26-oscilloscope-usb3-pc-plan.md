# PC 上位机示波器 — 实现计划

**日期:** 2026-07-26
**基于:** `docs/superpowers/specs/2026-07-26-oscilloscope-usb3-pc-design.md`

---

## Phase 1: MCU USBSS 枚举 + 连续流传输

**目标:** MCU 跑通 USB 3.0 设备枚举 + HSADC 40Msps 采集 + Bulk 连续发送

### Task 1.1 — USBSS 设备枚举
- 配置 USBSS 时钟和 PHY
- 实现 USB 3.0 设备描述符（VID/PID 自定义）
- 配置 Bulk IN/OUT 端点
- 验证：PC 设备管理器能识别设备

### Task 1.2 — HSADC 40Msps 配置
- 配置 HSADC 时钟（PLL → CLKDIV 分频 → 40Msps 目标采样率）
- 配置 DMA 乒乓缓冲（双 1024 点 Buffer）
- 配置 DMA 完成中断
- 验证：串口打印 buffer 前 10 个采样值，确认非全零

### Task 1.3 — 10-bit 打包函数
- 实现 `pack_samples(uint16_t* samples, uint8_t* packed, int count)`：4 采样 → 5 字节
- 单元测试：已知输入验证输出
- 性能：在 144MHz 主频下，打包 1024 点应在 DMA 半满间隔内完成

### Task 1.4 — 数据包协议封装
- 实现 `build_packet()`：同步头 + 序号 + 模式标志 + 长度 + 载荷 + CRC16
- CRC16-CCITT 查表法实现
- 验证：已知数据 → 已知 CRC

### Task 1.5 — 主循环：乒乓缓冲 → 打包 → 发送
- 主循环轮询 DMA 标志
- 半满/全满中断 → 打包当前半缓冲 → USB Bulk IN 发送
- 双 Buffer 交替，保证零丢点
- 验证：PC 端用 USB 抓包工具（Wireshark USB）确认数据流

**Phase 1 交付物:** MCU 连续发送 40Msps × 10-bit 数据到 PC，Bulk 传输稳定

---

## Phase 2: PC WPF 框架 + USB 通信 + 基础波形显示

**目标:** PC 端能接收 USB 数据并显示波形

### Task 2.1 — WPF 项目结构搭建
- 创建 MVVM 目录结构：`Models/`, `ViewModels/`, `Views/`, `Services/`
- 安装 NuGet 包：`LibUsbDotNet`, `MathNet.Numerics`
- 配置 `.csproj`：目标 x64，.NET 8.0+

### Task 2.2 — UsbService 实现
- 设备枚举：VID/PID 匹配找到 MCU 设备
- 打开 Bulk IN/OUT 端点
- 收包线程：BackgroundWorker 或 Task.Run
- 同步头检测（AA 55 对齐）、CRC 校验、序号连续性检查
- 丢包统计
- 发命令函数：`SendCommand(byte cmd, uint value)`

### Task 2.3 — DataBuffer（环形缓冲）
- `RingBuffer` 类：64MB byte[]，SpinLock
- `Write(byte[] data)` — USB 线程调用
- `Read(ref int readPos, byte[] dest)` — 渲染线程调用
- 覆盖策略：满时覆盖最老数据

### Task 2.4 — 10-bit 解包
- `UnpackSamples(byte[] packed, int count)` → `int[]` (转换为 32-bit 便于后续处理)
- 与 MCU 端打包函数互逆

### Task 2.5 — WaveformRenderer
- `WriteableBitmap` 后台缓冲区写入
- 水平压缩：屏幕宽 N 像素 → 将采样数据分成 N 列 → 每列取 min/max 画竖线
- 垂直缩放：10-bit → 屏幕像素（根据垂直档位）
- 网格绘制：10 格 × 8 格，灰色细线
- 60fps `CompositionTarget.Rendering` 驱动刷新

### Task 2.6 — MainWindow 基础布局
- 左侧波形区 + 右侧空白面板
- 底部状态栏：采样率、USB 状态、丢包率

**Phase 2 交付物:** PC 端实时显示连续流波形，可辨认信号形状

---

## Phase 3: 触发捕获模式 + 触发控制面板

**目标:** 支持触发模式，MCU 检测边沿后发送触发帧，PC 显示静止波形

### Task 3.1 — MCU 触发模式实现
- 环形缓冲区：`g_ringbuf[4096]`，循环写入
- 边沿检测：比较相邻采样点，检测上升/下降穿越阈值
- 触发后：取触发点前 512 + 后 512 = 1024 点
- 打包触发帧（含触发信息头）→ USB 发送
- 支持 PC 命令切换连续流/触发模式

### Task 3.2 — PC 触发控制面板
- 边沿选择：上升/下降 ComboBox
- 触发电平：Slider + 数值输入
- 模式切换按钮：连续流 ↔ 触发捕获
- 命令下发：ViewModel → UsbService.SendCommand()

### Task 3.3 — 触发模式渲染
- 收到触发帧 → 替换 WaveformRenderer 当前显示缓冲区
- 触发点标记：垂直虚线，颜色高亮
- 静止显示（停止滚动）

**Phase 3 交付物:** 触发捕获完整可用

---

## Phase 4: FFT 频谱 + 自动测量

**目标:** FFT 频谱显示 + 频率/峰峰值等自动测量

### Task 4.1 — FftProcessor
- 使用 MathNet.Numerics 的 FFT
- 输入：1024 采样点（加 Hanning 窗）
- 输出：dB 幅度谱（仅正频率部分，512 点）
- 后台线程执行，完成回调到 UI 线程

### Task 4.2 — FFT 频谱区
- 波形区下方可折叠面板
- 横轴：频率（Hz/kHz/MHz），纵轴：dB
- 峰值标注（频率 + dB 值）

### Task 4.3 — 自动测量
- 频率：FFT 峰值对应频率（触发模式），或过零检测（连续流模式）
- 峰峰值：max - min（考虑 10-bit → 电压映射）
- 周期：1/频率
- 平均值：直流分量
- 测量值显示在右侧面板

**Phase 4 交付物:** FFT + 自动测量可用

---

## Phase 5: 打磨

**目标:** 优化体验，处理边界

### Task 5.1 — USB 容错
- 设备断开检测 + 自动重连
- 连接状态指示（状态栏图标）

### Task 5.2 — 性能优化
- 波形渲染 Profile（确认不丢帧）
- 解包函数 SIMD 优化（如有必要）
- 减少 GC 压力（对象池、Span<T>）

### Task 5.3 — UI 细节
- 深色主题全局应用
- Fira Code/Fira Sans 字体嵌入
- 网格亮度、波形辉光微调
- 窗口缩放响应式布局

### Task 5.4 — 错误处理完善
- 异常日志（NLog 或 Serilog）
- 用户友好的错误提示
- 边界条件保护

**Phase 5 交付物:** 产品级完成度

---

## 依赖关系

```
Phase 1 ──→ Phase 2 ──→ Phase 3
                    └──→ Phase 4
                              │
                    Phase 3 ──┤
                              │
                    Phase 4 ──┴──→ Phase 5
```

Phase 3 和 Phase 4 可在 Phase 2 后并行开发。

---

## 关键验证节点

| 节点 | 验证方法 |
|------|----------|
| 1.3 完成 | MCU 端单元测试：100 个随机 10-bit 输入 → 打包 → 解包 → 100% 一致 |
| 1.5 完成 | PC Wireshark USB 抓包 → 确认连续数据流 + 序号连续 + CRC 正确 |
| 2.5 完成 | 信号发生器输入 100kHz 正弦波 → PC 显示清晰正弦波形 |
| 3.1 完成 | 触发模式 + 信号发生器 → 波形稳定不抖动 |
| 4.1 完成 | 100kHz 输入 → FFT 峰值在 100kHz ± 20kHz |
