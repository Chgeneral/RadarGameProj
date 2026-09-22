# RadarGameProj —— STM32F407 + FreeRTOS 多设备输入掌机

基于开源项目 **N|Watch**（作者 Zak Kemble，GPL v3）改写的嵌入式练习工程：
在 STM32F407 上用 FreeRTOS 搭建一个多设备输入的打砖块小游戏，并逐步扩展到雷达模块。

## 硬件平台

| 部件 | 芯片 / 接法 | 备注 |
|------|------------|------|
| MCU | STM32F407ZGT6（Cortex-M4, 168MHz） | HAL 库 + STM32CubeMX 生成 |
| 显示 | SSD1306 OLED，I2C1（PB6=SCL / PB7=SDA，地址 0x78） | 与 6050 共享一条 I2C 总线 |
| 姿态 | MPU6050/MPU6500，I2C1（地址 0xD0） | 实测芯片 WHO_AM_I=0x70，为 MPU6500 兼容片 |
| 旋钮 | 旋转编码器，TIM3 编码器模式（PC6/PC7） | 硬件正交解码 |
| 按键 | PB5（内部上拉，按下拉低） | 表盘 / 游戏页面切换 |

> I2C 上拉属于总线本身（不依赖任何可拔插模块），SCL/SDA 各接 2.2kΩ 到 3.3V。

## 软件架构

三层数据流，采集与游戏解耦：

```
采集层                        仲裁层                     游戏层
EncoderTask ── xQueueEncoder ┐
                             ├─ InputTask(队列集) ── xQueuePaddle ── game1_draw(移动挡板)
MpuTask     ── xQueueMpu ─────┘
```

- **共享资源保护**：OLED 与 MPU 共用 I2C1，所有总线访问统一经 `g_oledMutex` 串行化（`WaitForI2C/ReleaseI2C`、`draw_flushArea/draw_end` 均已上锁）。
- **页面切换**：`StartPageSwitchTask` 读按键，用 `vTaskSuspend/Resume` 在「表盘任务」与「游戏任务」之间切换。
- **自写驱动**：`driver_mpu6050.c`、`driver_rotary_encoder.c`、`driver_oled.c`（OLED 移植自 N|Watch）。

## 当前进度

- [x] 初步 FreeRTOS 框架（任务 / 队列 / 队列集 / 互斥量）
- [x] 自主编写 MPU6050 驱动（体感倾斜控制）
- [x] 自主编写旋转编码器驱动（硬件正交解码）
- [x] OLED 显示 + 表盘/游戏页面切换
- [x] 多设备输入玩游戏（编码器 + 体感同时控制挡板）
- [ ] **雷达模块（下一步）**

## 目录结构

- `Core/`：CubeMX 生成的 `main.c` / `freertos.c` / 中断 / MSP
- `Drivers/UserDriver/`：自写外设驱动（MPU6050、编码器、OLED、按键、LCD 封装）
- `Nwatch/`：移植自 N|Watch 的游戏与绘图层（`game1.c` / `draw.c` / `page_manager.c` 等）
- `Middlewares/`、`Drivers/STM32F4xx_HAL_Driver/`：FreeRTOS 与 HAL 库源码
- `MDK-ARM/`：Keil 工程（`RadarGameProj.uvprojx`）

## 构建

用 Keil µVision 打开 `MDK-ARM/RadarGameProj.uvprojx` 编译下载；工程根目录含 `.ioc`，也可用 STM32CubeIDE 导入。

## 踩坑记录

I2C 总线死锁（复位无效、拔插外设才恢复）等实战排查过程，记录在仓库外的
`嵌入式系统笔记.md`「FreeRTOS游戏机项目——I2C总线死锁排查案例」一节。

## 许可

改写自 N|Watch（© Zak Kemble），遵循 **GNU GPL v3**。
