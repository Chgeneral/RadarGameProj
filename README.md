# RadarGameProj —— STM32F407 + FreeRTOS 多设备输入掌机

基于开源项目 **N|Watch**（作者 Zak Kemble，GPL v3）改写的嵌入式练习工程：
在 STM32F407 上用 FreeRTOS 把**旋转编码器、体感（MPU6050）、毫米波雷达**三个异构输入
接进一个 128×64 OLED 打砖块游戏——旋钮和体感都能控制挡板（谁在动就听谁），
雷达只负责判断人在不在，人一离开游戏自动暂停。

自写与移植代码约 2200 行（不含 HAL / FreeRTOS / CubeMX 生成部分）。

## 功能

- **多设备同时输入**：编码器与体感两个采集任务常驻，靠**队列集**隐式仲裁，"谁在动就用谁"，无需设置菜单手动切换。
- **人体存在感应**：雷达检测到无人时游戏自动暂停并显示 `PAUSED / NO PLAYER`，有人回来自动恢复并整屏重绘。
- **双页面**：表盘页（温湿度位预留、显示 MPU ID）与游戏页，单键切换，同一时刻只有一个任务写帧缓存。
- **局部刷新**：每帧只把球、挡板、被打掉的砖块各自的小矩形推到屏上，整屏重绘仅在必要时触发。

## 硬件平台

| 部件 | 芯片 / 接法 | 备注 |
|------|------------|------|
| MCU | STM32F407ZGT6（Cortex-M4, 168MHz） | HAL 库 + STM32CubeMX 生成；时钟源 **HSI 16MHz** → PLL M=8/N=168/P=2 |
| 显示 | SSD1306 OLED，I2C1（PB6=SCL / PB7=SDA，地址 0x78） | 与 6050 共享一条 I2C 总线 |
| 姿态 | MPU6050/MPU6500，I2C1（地址 0xD0） | 实测芯片 WHO_AM_I=0x70，为 MPU6500 兼容片 |
| 旋钮 | 旋转编码器，TIM3 编码器模式（PC6/PC7，AF2） | 硬件正交解码，TI1+TI2 四倍频 |
| 按键 | PB5（内部上拉，按下拉低） | 表盘 / 游戏页面切换，软件 20ms 消抖 |
| 雷达 | XenG101G 毫米波，USART2（PA2=TX / PA3=RX，115200） | 6 字节定长二进制帧，`HAL_UARTEx_ReceiveToIdle_IT` 收帧 |

> I2C 上拉属于总线本身（不依赖任何可拔插模块），SCL/SDA 各接 2.2kΩ 到 3.3V。

> HAL 时基已从 SysTick 改到 **TIM6**，避免与 FreeRTOS 调度器抢占同一个 SysTick。

## 软件架构

三层数据流，采集与游戏解耦：

```text
  采集层(各自定时轮询)        仲裁层(阻塞等待)         应用层(消费者)
  ┌──────────────┐
  │ EncoderTask  │─xQueueEncoder─┐
  │  TIM3差分20ms│               │   ┌───────────┐
  └──────────────┘               ├──►│xQueueSet  │
  ┌──────────────┐               │   │   Input   │
  │ MpuTask      │─xQueueMpu ────┘   └─────┬─────┘
  │  I2C+死区20ms│                         │ xQueueSelectFromSet
  └──────────────┘                   ┌─────▼─────┐      ┌────────────┐
                                     │ InputTask │─────►│ game1Task  │
                                     └───────────┘ xQueue│  50ms一帧  │
  ┌──────────────┐                                Paddle└──────▲─────┘
  │ Radar_Task   │──g_sysEvent: EVT_PLAYER_PRESENT ───────────┘
  │  100ms轮询   │         (事件组，只传状态)
  └──────────────┘
  ┌──────────────┐  vTaskSuspend/Resume + eTaskGetState 握手
  │ pageSwitch   │────► defaultTask(表盘)  ⇄  game1Task(游戏)
  │  PB5 10ms    │
  └──────────────┘        两者都要先拿 g_oledMutex 才能碰 I2C1
```

| 任务 | 栈(words) | 周期 | 职责 |
|------|-----------|------|------|
| `StartDefaultTask` | 512 | 100ms | 表盘页：OLED 打印 + 读 MPU ID |
| `EncoderTask` | 256 | 20ms | TIM3 差分 → `xQueueEncoder`（`diff==0` 不发） |
| `MpuTask` | 256 | 20ms | 读加速度 → ±15° 死区过滤 → `xQueueMpu` |
| `InputTask` | 256 | 阻塞 | 队列集 select → 翻译成统一命令 → `xQueuePaddle` |
| `game1Task` | 256 | 50ms | 收命令、碰撞判定、局部绘制 |
| `pageSwitch` | 128 | 10ms | PB5 消抖 + 页面切换握手 |
| `Radar_Task` | 256 | 100ms | 解析雷达帧 → 置/清 `EVT_PLAYER_PRESENT` |

heap_4 共 15360 字节，实际占用约 9KB（任务栈 + TCB 约 8.3KB，内核对象约 0.5KB，砖块数组 160B），余量约 40%。空闲任务走静态分配，不占堆。

### 三条设计原则

- **队列传"数据"，事件组传"状态"，互斥量护"资源"**。挡板命令是会被消费掉的数据；"有没有人"是会持续存在、可能被多方读取的状态（`xEventGroupWaitBits` 的 `xClearOnExit` 必须为 `pdFALSE`）；I2C1 是同一时刻只能一人用的资源。
- **绝不从外部挂起可能持锁的任务**。页面切换用 `switchToGame` 标志通知对方退出，对方在不持锁的安全点自己调 `vTaskSuspend(NULL)`，切换方只用 `eTaskGetState` 等待。从外部强行挂起会把 `g_oledMutex` 一起冻住，造成全系统死锁。
- **隐式仲裁的前提是所有源在静止时都保持沉默**。编码器 `diff==0` 天然不发，MPU 必须加死区——少了任何一边的沉默，"谁在动用谁"就退化成"永远是某一个"。

## FreeRTOS 组件选型

| 内核对象 | 用到的 API | 解决了什么问题 |
|----------|-----------|----------------|
| 任务 | `xTaskCreate`、`vTaskDelay`、`vTaskSuspend(NULL)`/`vTaskResume`、`eTaskGetState` | 7 个任务各干一件事；两个页面互斥占屏 |
| 队列 | `xQueueCreate`、`xQueueSend/Receive`（超时均为 0） | 两个输入源各写自己的队列，游戏只收统一命令 |
| 队列集 | `xQueueCreateSet`、`xQueueAddToSet`、`xQueueSelectFromSet` | 一个任务同时阻塞在两个队列上，实现"谁在动用谁" |
| 互斥量 | `xSemaphoreCreateMutex`、`xSemaphoreTake/Give`、`xSemaphoreGetMutexHolder` | 串行化 I2C1 总线事务；排查死锁时读出"锁在谁手里" |
| 事件组 | `xEventGroupCreate`、`SetBits`/`ClearBits`、`xEventGroupWaitBits` | 雷达只置状态位、不碰输入链也不画屏 |
| 堆与静态分配 | `pvPortMalloc`、`vApplicationGetIdleTaskMemory` | 砖块数组只申请一次；空闲任务不占堆 |
| 诊断钩子 | `vApplicationStackOverflowHook`、`vApplicationMallocFailedHook` | 栈溢出/堆耗尽不再静默，`__BKPT(0)` 直接停在调试器 |

关键配置（均写在 `FreeRTOSConfig.h` 的 `USER CODE` 区段内，CubeMX 重新生成不会覆盖）：
`configUSE_QUEUE_SETS=1`、`configCHECK_FOR_STACK_OVERFLOW=2`、`configUSE_MALLOC_FAILED_HOOK=1`、
`INCLUDE_xSemaphoreGetMutexHolder=1`、`INCLUDE_uxTaskGetStackHighWaterMark=1`。

## 当前进度

- [x] 初步 FreeRTOS 框架（任务 / 队列 / 队列集 / 互斥量 / 事件组）
- [x] 自主编写 MPU6050 驱动（体感倾斜控制）
- [x] 自主编写旋转编码器驱动（硬件正交解码）
- [x] OLED 显示 + 表盘/游戏页面切换
- [x] 多设备输入玩游戏（编码器 + 体感同时控制挡板）
- [x] 雷达模块：XenG101G 人体接近感应 + 无人自动暂停
- [ ] DHT11 温湿度接入主界面
- [ ] W25Q64 存分数 + 计分板界面

## 待改进

一次完整代码复盘后记录的问题，按优先级排列（完整 10 条见笔记）：

1. **CMSIS 优先级枚举与裸优先级混用**：`osThreadCreate` 会把 `osPriorityNormal` 映射成 FreeRTOS 优先级 3，而 `xTaskCreate(..., osPriorityNormal + 1)` 是把 1 原样传入，导致表盘任务反而成了全系统最高优先级，与设计意图相反。两套写法需统一。
2. **MPU 读取应改突发读**：现在对 6 个 16 位量做了 12 次单字节 `HAL_I2C_Mem_Read`，既慢又可能让高低字节跨采样撕裂。应从 `0x3B` 一次连读 14 字节。
3. **`acos` 开销不必要**：F407 的 FPU 只有单精度硬件，double 版 `acos` 全靠软件库；而需求只是"左/右/死区"三态，直接用原始加速度比阈值即可。
4. **DMA 配了未用**：CubeMX 已配 `DMA1_Stream5 → USART2_RX` 并 `__HAL_LINKDMA`，但代码用的是 `..._IT` 版本，改成 `HAL_UARTEx_ReceiveToIdle_DMA` 即可。
5. **雷达阈值方向待实测**：`if (frame[1] > GATE)` 按"距离字节、0x00 表示无人"的注释，人离得很近时会被判成无人，需上板核对。
6. 其他：裸 tick 数与 `pdMS_TO_TICKS` 混用；死代码 `btnExit`/`btnRight`/`btnLeft` 待删；队列对象应移到 `main.c` 的 `RTOS_QUEUES` 段统一创建；部分文件注释编码为 GBK/乱码，待统一 UTF-8。

## 目录结构

- `Core/`：CubeMX 生成的 `main.c` / `freertos.c` / 中断 / MSP / TIM6 时基
- `Drivers/UserDriver/`：自写外设驱动（MPU6050、编码器、雷达、OLED、按键、LCD 封装）
- `Nwatch/`：移植自 N|Watch 的游戏与绘图层（`game1.c` / `draw.c` / `page_manager.c` / `radar_manager.c`）
- `Middlewares/`、`Drivers/STM32F4xx_HAL_Driver/`：FreeRTOS 与 HAL 库源码
- `MDK-ARM/`：Keil 工程（`RadarGameProj.uvprojx`）

## 构建

用 Keil µVision 打开 `MDK-ARM/RadarGameProj.uvprojx` 编译下载；工程根目录含 `.ioc`，也可用 STM32CubeIDE 导入。

## 相关笔记

本工程的完整复盘记录在仓库外的 `嵌入式系统笔记.md`，两节：

- **「FreeRTOS游戏机项目——I2C总线死锁排查案例」**：复位无效、拔插外设才恢复的总线死锁实战排查，核心诊断法则是"状态在片内还是片外"。
- **「FreeRTOS游戏机项目——整体架构与FreeRTOS组件选型总结」**：三层架构、组件选型依据、8 个值得记录的 C 语言写法（无符号回绕差分、超时判定在场、`volatile` + `__DMB()` 握手、滑窗找帧头、位域写入、局部刷新等）、10 条待改进项。

## 许可

改写自 N|Watch（© Zak Kemble），遵循 **GNU GPL v3**。
