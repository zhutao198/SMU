# SMU 源表固件（M1 框架）

> 依据：《硬件原理分析与固件设计.md》+《产品需求文档 PRD V1.1》
> 状态：**M1 已完成并通过编译链接验证**（arm-none-eabi-gcc 10.3，零错误零警告）
> 资源占用：Flash 113 KB / 512 KB，RAM 71 KB / 128 KB

## 1. 目录结构

```
code/firmware/
├── Core/
│   ├── Inc/                    应用层头文件（与 CubeMX 生成代码解耦）
│   │   ├── smu_board.h         引脚/量程/系数（引用设计文档章节）
│   │   ├── smu_types.h         类型/状态结构/错误码/状态字位域
│   │   ├── smu_app.h           初始化入口 SMU_AppInit()
│   │   ├── smu_meas.h/.c       ADS1115 驱动 + 工程量换算 + 滤波
│   │   ├── smu_cal.h/.c        标定系数/网络配置 Flash 存取（双备份+CRC）
│   │   ├── smu_mode.h/.c       模式状态机 + 安全切换时序（SR-4）
│   │   ├── smu_ctrl.h/.c       输出执行：PWM/GPIO + CV 前馈 + CC 比例环
│   │   ├── smu_prot.h/.c       过压/过流/ADC 饱和保护（锁存语义）
│   │   ├── smu_cmd.h/.c        命令层（SCPI 子集，串口与 Web 共用）
│   │   ├── smu_web.h/.c        内置 HTTP 服务（LwIP netconn）
│   │   ├── smu_webpage.h       网页 C 数组（自动生成，勿手改）
│   │   ├── smu_diag.h/.c       上电自检 + 看门狗喂狗
│   │   ├── smu_uart.h/.c       串口中断接收 + 行组装
│   │   ├── smu_net.h           平台网络轮询接口
│   │   ├── smu_rtos.h          FreeRTOS 兼容宏（pdMS_TO_TICKS 兜底）
│   │   ├── smu_ver.h           版本号
│   │   └── main.h              外设句柄声明（CubeMX 工程以其为准）
│   └── Src/
│       ├── main.c              CubeMX 骨架 + SMU_AppInit 挂接（USER CODE 块内）
│       └── smu_*.c             应用实现
├── tools/
│   ├── build.ps1               一键构建（自动定位 HAL/FreeRTOS/LwIP）
│   ├── web2c.py                网页源码 → C 数组（≤64KB 检查）
│   └── verify/                 独立构建用的平台层与配置
│       ├── gpio.c i2c1.c tim2.c usart1.c iwdg.c eth.c lwip.c freertos.c
│       ├── ethernetif.c        F4 HAL ETH + LwIP 零拷贝收包适配
│       ├── lan8720.c           PHY 驱动（地址默认 0，待实测）
│       ├── sys_arch.c          LwIP OS 抽象（FreeRTOS 原生 API）
│       ├── sys_now.c freertos_hooks.c smu_net.c
│       ├── stm32f4xx_it.c      中断服务
│       ├── stm32f4xx_hal_conf.h / FreeRTOSConfig.h / lwipopts.h
│       ├── main.h / STM32F407VETx_FLASH.ld
└── build/                      构建产物（elf/bin/map/compile.log）
```

**任务与优先级**（PRD FR-WEB-8：网络任务不得影响控制环）：

| 任务 | 周期 | 优先级 | 职责 |
|---|---|---|---|
| ctrl | 250Hz | 4 | 模式序列/ADC 轮询/占空比/保护/喂狗 |
| cmd  | 事件 | 2 | 串口命令行 |
| net  | 5ms | 2 | ETH 收包轮询（100ms 顺带查 PHY） |
| web  | 事件 | 1 | HTTP 服务（netconn） |
| mon  | 1Hz | 0 | 诊断预留 |

## 2. 快速构建（本机验证路径）

无需 make/CubeIDE，直接调用 arm-none-eabi-gcc：

```powershell
cd code\firmware
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

默认从 `$env:TEMP\cube_fw` 找 CubeFW（HAL/FreeRTOS/LwIP 源码）。
换用自己的 CubeFW 目录：

```powershell
.\tools\build.ps1 -Fw D:\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.1
```

产物：`build/smu.elf`、`smu.bin`、`smu.hex`、`smu.map`、`compile.log`。

**修改网页后重新生成内嵌资源**（构建脚本不会自动做）：

```powershell
C:\Python311\python.exe tools\web2c.py ..\web\src\index.html Core\Inc\smu_webpage.h
```

## 3. 用 CubeMX 生成正式工程（推荐后续使用）

应用层代码（`Core/Src/smu_*.c` + `Core/Inc/smu_*.h`）与 CubeMX 生成代码**零冲突**，
把本目录交给 CubeMX 生成即可（CubeMX 只会新建/覆盖它自己的文件）。

### 3.1 工程参数

| 项 | 值 |
|---|---|
| MCU | STM32F407VETx（LQFP100） |
| 工具链 | Makefile / CMake / CubeIDE 均可 |
| 固件库 | 勾选 "Copy only the necessary library files" |

### 3.2 时钟树（HSE 8MHz → 168MHz）

- HSE = 8MHz（Y2，设计文档 §6）
- PLLM=8, PLLN=336, PLLP=2, **PLLQ=7**（Q 供 ETH 的 50MHz RMII 时钟）
- AHB=168MHz, APB1=42MHz, APB2=84MHz, Flash Latency = 5 WS

### 3.3 外设清单

| 外设 | 模式 | 参数 |
|---|---|---|
| TIM2 | PWM Generation CH2 | 内部时钟，PSC=0，ARR=839（≈100kHz），GPIO 自动配 PB3（AF1） |
| I2C1 | I2C | 100kHz，7 位地址，PB6/PB7 自动配（AF4 OD） |
| USART1 | 异步 | 115200-8N1，**勾选 NVIC 中断**（RX 中断驱动） |
| IWDG | 看门狗 | 32kHz/32 → ≈100ms，**取消 "Hardware Stop in Debug"** 无关项即可 |
| ETH | RMII | 勾选 RMII；勾选 NVIC；PHY 地址 0；**勾选 "ETH interrupt"** |
| GPIO 输出 | PB4 / PD1 / PD2 | 推挽输出，Label 见 smu_board.h |
| GPIO 输入 | PB5 / PC11 / PC12 | 上拉输入（RDY_1 / J18） |
| DMA | （无） | 全部用 PIO/中断，不配 DMA |
| 中断分组 | NVIC Priority Group = 4 | 与 FreeRTOSConfig 一致 |

> CubeMX 会自动生成 `gpio.c / i2c1.c / tim2.c / usart1.c / iwdg.c / eth.c /
> freertos.c / lwip.c`，功能等价于 `tools/verify/` 下的同名文件（verify 版仅用于
> 独立编译验证）。以太网收包：CubeMX 版默认中断驱动；若用轮询，请自行提供
> `SMU_NetPoll/SMU_NetPhyPoll`（参见 tools/verify/smu_net.c）。

### 3.4 中间件

**FreeRTOS**（CMSIS-RTOS 接口选 **Disable**，用原生 API）：

| 项 | 值 |
|---|---|
| Version | V1.x（10.3+） |
| Total heap size | 40 KB 起步（RAM 预算见 §5） |
| configUSE_MALLOC_FAILED_HOOK | 1 |
| configCHECK_FOR_STACK_OVERFLOW | 2 |
| configUSE_TIMERS | 0 |
| configTICK_RATE_HZ | 1000 |

**LwIP**：

| 项 | 值 |
|---|---|
| Version | LwIP v2.x |
| IPv4 启用，DHCP 关（M1 静态 IP；应用层可扩展） |
| LWIP_NETCONN = 1，LWIP_SOCKET = 0 |
| LWIP_TCPIP_CORE_LOCKING = 1 |
| **LWIP_HTTPD = 0**（用自研 SMU_Web，不用 LwIP httpd） |
| MEM_SIZE = 16 KB |

### 3.5 main.c 挂接

CubeMX 生成的 `main.c` 在 USER CODE 块中加入（本仓库 `Core/Src/main.c` 已含，
重新生成时这些块会被保留）：

```c
/* USER CODE BEGIN Includes */
#include "smu_app.h"
/* USER CODE END Includes */
...
  /* USER CODE BEGIN 2 */
  SMU_AppInit();          /* 创建 FreeRTOS 任务 */
  /* USER CODE END 2 */

  vTaskStartScheduler();
```

并把 `Core/Src/smu_*.c`、`Core/Inc/smu_*.h` 纳入编译（Makefile 工具链下
`Core/Src/*.c` 会被自动 glob，无需改 Makefile）。

## 4. 上电验证（首检）

J-Link SWD 烧录（已验证通过，114 KB / 校验 OK）：

```powershell
& "C:\Program Files\SEGGER\JLink_V942\JLink.exe" -Device STM32F407VE -If SWD -Speed 4000 `
    -CommanderScript tools\flash.jlink
```

插上板子 USB（Type-C，CH340N）后，确认设备管理器中
`USB-SERIAL CH340` 的 COM 号，然后跑串口首检：

```powershell
.\tools\serial_test.ps1 -Port COMx          # x = CH340 的实际口
```

预期输出（banner + 命令响应）：

```
=== SMU-V1 FW 0.1.0 ===
SELFTEST: OK (ADS1115 OK, NV OK)
CAL: V k=0.001197 b=0.000000 | I k=0.002452 b=-0.032353
NET: STATIC 192.168.1.10/255.255.255.0
READY. Send *IDN? to verify.

  *IDN?         -> SMU-V1, FW 0.1.0, SN:0001
  STAT?         -> 5                (READY+ADC_OK)
  MODE?         -> HIZ
  MEAS:ALL?     -> V=0.0003,I=0.0012
  J18?          -> 0
  RAW?          -> AIN0=..,AIN1=..,AIN2=..,AIN3=..
```

若 `SELFTEST: FAIL (ADS1115 FAIL ...)` → I2C 链路问题，先查 PB6/PB7 与 0x48 地址。

## 5. 命令集（M1 已实现，PRD §6.2）

> **命令格式：关键字与参数之间必须有空格**（如 `MODE CV`，不是 `MODECV`）。
> 大小写敏感；`?` 结尾为查询。换行符可选——固件在收到字符后 200ms 无新数据也会成行
> （兼容不带换行的串口工具）。

```
*IDN?  *RST  *CLS  SYST:VERS?  SYST:ERR?  SYST:SELF
MODE CV|CC|VM|IM|HIZ   MODE?
VOLT <v>  VOLT?   CURR <i>  CURR?
VOLT:PROT <v>  VOLT:PROT?   CURR:PROT <i>  CURR:PROT?
OUTP ON|OFF  OUTP?
MEAS:VOLT?  MEAS:CURR?  MEAS:ALL?
STAT?  J18?  RAW?  I2C?  ADC?  ADCT?
CAL:VOLT:ZERO / CAL:VOLT:GAIN <v> / CAL:CURR:ZERO / CAL:CURR:GAIN <i>
CAL:SAVE / CAL:DEF
```

`STAT?` 状态字：bit0=READY / bit1=PROT / bit2=ADC_OK / bit3=OUT_ACTIVE（`13`=就绪+ADC在线+输出已建立）。
`I2C?` / `ADC?` / `ADCT?` 为诊断命令（扫描 I2C 总线、读 4 通道原始码值、分步测转换链）。

Web 端点（PRD §6.3）：`/`（页面）、`/data.json`、`/set?...`、`/cmd`。

## 6. 已知限制与下一步（M2）

1. **尚未在硬件上做过功能验证**——固件已烧录并复位运行，串口/以太网/输出闭环待实测。
2. LAN8720 的 PHY 地址按默认 0 实现（PRD O-3），首次联网若不通先读 `RAW?` 确认 I2C，再查 PHY。
3. CC 控制是比例环（M2 换 PI 并整定）；CV 为前馈 + 软启动。
4. DHCP 未实现（M1 静态 IP，默认 192.168.1.10，`CAL:SAVE` 可存）。
5. 片内温度监控、批量扫描、TCP SCPI 通道在后续里程碑。

## 7. 硬件联调记录（2026-10-26，STM32F407 实机验证通过）

按时间顺序记录实际排掉的坑，每条都是**能复现的具体故障**：

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | 上电后 `malloc failed hook` 死循环 | `lwipopts.h` 宏名写错（`LWIP_TCPIP_TASK_STACKSIZE` 不存在），tcpip 线程栈=0 → `pvPortMalloc(0)` 必失败 | 改为 `TCPIP_THREAD_STACKSIZE` 等 LwIP 正式宏名 |
| 2 | 网络初始化时 `HAL_Delay` 死等 | FreeRTOS 独占 SysTick（`xPortSysTickHandler`→`SysTick_Handler`），`HAL_IncTick()` 从不被调用 | `SysTick_Handler` 同时调 `HAL_IncTick()` + `xPortSysTickHandler()` |
| 3 | 调度器启动前 banner 处 BusFault | `HAL_Init` 已启用 SysTick，FreeRTOS 的 `xTaskIncrementTick` 在链表未初始化时遍历野指针 | 所有带延时的初始化移入任务上下文（`task_plat`），main 只创建任务 |
| 4 | `snprintf`/`sprintf` 触发 BusFault（BFAR=0x00B8xxxx） | nosys 环境下 newlib printf 的 `_dtoa`/`_REENT` 链失效 | 自建定点格式化库 `smu_fmt.c`（`smu_cat_*`），**不用 printf %f** |
| 5 | J-Link 突然连不上（AHB-AP 无响应） | `ethernetif.c` 用了 F7 风格零拷贝 API，RX 描述符 `Buffer1Addr` 未设置，DMA 收到包写到地址 0，写飞总线 | 按 ST 官方 F4 模板重写：custom pbuf 池 + `HAL_ETH_ReadData` |
| 6 | ctrl 任务不跑（uptime=0） | `PHY_READ_TO=0xFFFF`（65.5s），PHY 无响应时 MDIO 访问霸占 CPU，饿死低优先级任务 | `hal_conf.h` 定义 `PHY_READ_TO/PHY_WRITE_TO=100`；PHY 轮询移到最低优先级任务 |
| 7 | 串口命令无响应（RXNE 一直挂起） | USART1 中断优先级 6 ≥ FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY(5)`，被 BASEPRI=0x50 屏蔽 | 不用 FreeRTOS API 的中断优先级设 **4**（< 5） |
| 8 | `ADC?` 全通道 ERR | ① 自检强制 `config==0x8583`，器件未断电时保留旧值 → 误判；② RDY 轮询：I2C 读操作会清除 RDY，永远等不到 | 自检只要求 I2C 通信成功；转换改用固定延时（860SPS 等 2ms） |
| 9 | **L3 电感严重发烫**（负载仅 2.5mA） | PWM 100kHz 时纹波电流 ΔI=Vin·D(1−D)/(L·f)≈**4.3A**，远超 L3(10µH/1.5A) 额定 → 磁芯饱和 → 电流失控 | PWM 提到 **400kHz**（ΔI≈1.07A，降 4 倍）→ 实测明显减轻。**硬件改版建议**：L3 换 22~47µH/2A |
| 10 | 电压读数偏差 ~1.6 倍 | ADS1115 用了单端模式（AINx-GND）。**实际设计是差分**：AIN0−AIN1 测电压、AIN2−AIN3 测电流，AIN1/AIN3 接 VREF(1.643V) 做比率参考 | 改为差分 MUX（000/011）；换算 `V=(code·LSB+VREF)·9.66`、`I[mA]=code·LSB·1000/(1Ω·50.4)` |
| 11 | 电压/电流读数串味、V 恒定虚高 16V | **ADS1115 config 漏了 MODE 位**（`0x00E3` 应为 `0x01E3`）：器件在**连续转换模式**工作，OS 位不清零 → OS 轮询"假完成"、MUX 切换读到旧通道数据，8 点滤波缓冲被污染 | 补 MODE=1（单次模式）；写后立即读回校验 MUX/PGA；LSB=FSR/32768=62.5µV |
| 12 | 单次转换读数约 30% 概率串味（`ADC?` 时而 V=-26275/I=17、时而 V=17/I=-26275） | mutex 只包住**单次寄存器访问**，"写 config→校验→等转换→读 conv" 是 4 次独立 I2C 事务，ctrl 任务（P4）能在事务中间抢占 cmd 任务（P2）改写 config | 锁提升到**整个转换事务**（`i2c_write_raw`/`i2c_read_raw` 为无锁内部函数，`ADS1115_ConvertOnce` 整体持锁） |
| 13 | 改用 RDY 引脚等待后全盘卡死：`ADCT? wr=-1`、`-120 Mode transition busy` | 该板 ADS1115 的 ALERT/RDY 为**开漏输出且无外部上拉**（网表 `RDY_1` 只有 U6.2 + U1.91），器件也不拉低该脚；ctrl 任务每读一次卡 20ms×3 重试并**持锁**，导致 cmd 任务 I2C 全部 50ms 超时 | 放弃 RDY，改**固定延时 2ms**（单次模式 860SPS 转换仅 1.2ms），不用 OS 位轮询（曾偶发"立即通过"读到上一次的 conv 值） |
| 14 | 事务级加锁后仍约 30% 串味；用 OS 轮询等待时又出现 `wr=-1` / 模式 busy | 根因是**两个任务都在操作 ADS1115**：cmd 任务（诊断/标定）与 ctrl 任务（控制环）抢占同一器件，锁只能保证单次传输原子，无法避免"写 config 被别人改写"与"锁内长时间等待把对方饿死" | **单一所有者**：只有 ctrl 任务访问 ADS1115；`ADC?`/`ADCT?`/`CAL:*` 全部改读 ctrl 任务的滤波缓存（`g_smu.adc[]`、`SMU_MeasGetAvgCode()`），cmd 任务不再碰 I2C |
| 15 | `CAL:SAVE` 返回 `-140 "Self-test failed"` | NV 区地址取自**1MB 版本**的扇区布局：F407VE 是 512KB，只有 sector 0~7，`FLASH_SECTOR_11`/`0x080E0000` 越界，擦除与编程全部失败 | 改用 sector 6（0x08040000）与 sector 7（0x08060000）分存 A/B 备份，**乒乓写入**（只擦即将写的那一槽，另一槽保留上一份好数据），单次只擦一个 128KB 扇区 |

**当前状态**：调度器/5 任务/串口命令层/ADS1115 **差分**测量/模式状态机/保护阈值/CV+CC 输出全部实机验证通过。
**待办**：以太网 PHY（LAN8720 MDIO 无响应，见 §6）、CV/CC 的 PI 闭环整定与 CC 震荡修复（M2）。

**关键设计约束（硬件）**：
- L3 = 10µH/1.5A，PWM 频率不得低于 ~400kHz（否则纹波电流超额定导致饱和发热）
- ADS1115 为**差分比率测量**：AIN0−AIN1（电压）、AIN2−AIN3（电流），VREF=1.65V 共模参考
- INA129 增益 = 1 + 49.4k/RG = **50.4**（RG=R50=1kΩ），采样电阻 R133=1Ω
- LM317(U32) 需正确输出 ~11V 给 LM5106 VDD（实测偏低会导致驱动不足、输出电压异常）

## 8. 版本记录

| 版本 | 日期 | 内容 |
|---|---|---|
| 0.1.0 | 2026-10-23 | M1：框架 + 驱动 + 命令层 + Web + 自检；J-Link SWD 烧录验证通过（verify OK），加上电 banner 与串口首检脚本 |
| 0.1.1 | 2026-10-26 | **实机联调通过**：修复 8 项集成故障（见 §7）；ADS1115 四通道/串口命令层/模式状态机/保护阈值默认值全部验证通过；新增 `I2C?`/`ADC?`/`ADCT?`/`SYST:SELF` 诊断命令与 `tools/flash_retry.ps1` |
