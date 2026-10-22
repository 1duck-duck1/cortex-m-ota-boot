<div align="center">

# 🚀 Cortex-M OTA Boot

**面向 STM32F407（Cortex-M4）的 A/B 双分区 OTA Bootloader**

单芯片专用 · 掉电安全回滚 · 核心逻辑可在 PC 上做断电注入测试

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-STM32F407-orange.svg)](#-硬件要求)
[![Language](https://img.shields.io/badge/language-C99-00599C.svg)](#-仓库结构)
[![CI](https://github.com/1duck-duck1/cortex-m-ota-boot/actions/workflows/ci.yml/badge.svg)](https://github.com/1duck-duck1/cortex-m-ota-boot/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-v0.2.0--dev-blue.svg)](CHANGELOG.md)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](#-贡献)

</div>

---

## 📖 目录

- [🚀 Cortex-M OTA Boot](#-cortex-m-ota-boot)
  - [📖 目录](#-目录)
  - [🎯 这是什么](#-这是什么)
  - [✨ 为什么用它](#-为什么用它)
  - [🔧 硬件要求](#-硬件要求)
  - [🧰 工具链与版本](#-工具链与版本)
  - [🗺️ Flash 分区布局](#️-flash-分区布局)
  - [🚀 快速开始](#-快速开始)
  - [🔍 特性对标](#-特性对标)
  - [📁 仓库结构](#-仓库结构)
  - [🤝 贡献](#-贡献)
  - [📄 许可证](#-许可证)

---

## 🎯 这是什么

一个裸机 Bootloader + OTA 升级方案，只面向 STM32F407VGT6（Cortex-M4，1 MB Flash）。

三个设计目标：

| 目标 | 含义 |
|---|---|
| 🧩 **A/B 双槽** | 新固件只写入非激活槽；切换只是提交一次元数据，全程不碰正在运行的固件 |
| ⚡ **掉电安全** | 升级过程中任意时刻断电，重新上电必然回到一个可启动的状态 |
| 🧪 **可测试** | 核心逻辑与硬件解耦，可在 PC 上跑断电注入单元测试，无需目标板 |

## ✨ 为什么用它

| 差异点 | 同类项目现状 |
|---|---|
| 只把 F407 这一件事做透：12 个大小不均的 sector 全部对齐，无"理论可用但没在真板验证"的分支 | 通用项目往往留着一堆没验过的移植分支 |
| 掉电安全提供**可复现的测试数据**，而非仅声称支持 | 多数项目只写一句"支持掉电恢复" |
| 核心逻辑可在 **PC 上单元测试**，包括断电注入测试 | 几乎没有同类项目做 |
| 分区与数据格式**按 sector 粒度推导并写进文档**，含"元数据日志必须独占整个擦除单元"这类硬约束 | 分区表往往一句话带过，坑留给使用者踩 |

## 🔧 硬件要求

| 平台 | 型号 | Flash | SRAM |
|---|---|---|---|
| **Cortex-M4** | STM32F407VGT6 | 1 MB / 12 sector（16 / 64 / 128 KB 三种粒度） | 128 KB（另有 64 KB CCM，本项目不使用） |


## 🧰 工具链与版本

| 工具 | 版本（实测） | 用途 |
|---|---|---|
| Keil MDK-ARM | 5.38+，ARM Compiler 5（V5.06 update 7 / build 960） | Boot / App 的 Keil 工程 |
| arm-none-eabi-gcc | xPack 15.2.1（20251203）；CI 使用发行版 `gcc-arm-none-eabi` | GCC 构建路径 |
| CMake | ≥ 3.20，实测 4.4.4 | 构建系统 |
| Ninja | 1.12.1 | CMake 生成器 |
| .NET SDK | 10.0，目标框架 `net10.0-windows` | C# 上位机 |
| STM32CubeMX | 6.13.0 | 生成 Core / HAL 工程骨架 |
| STM32F4 HAL / CMSIS | 仓库内 vendored 最小子集 | 芯片驱动 |

> [!NOTE]
> Keil 工程当前按 **ARM Compiler 5（AC5）** 配置（`uAC6=0`）。切换到 AC6 需要自行验证启动文件与汇编语法，两者的编译结果不作为等价产物。

## 🗺️ Flash 分区布局

**STM32F407VGT6 · 1 MB · 12 sector**

| 区域 | 地址范围 | 大小 | 占用 sector |
|---|---|---|---|
| Bootloader | `0x08000000` – `0x0800FFFF` | 64 KB | 0 – 3 |
| 参数数据区（NVS） | `0x08010000` – `0x0801FFFF` | 64 KB | 4 |
| Slot A | `0x08020000` – `0x0807FFFF` | 384 KB | 5 – 7 |
| Slot B | `0x08080000` – `0x080DFFFF` | 384 KB | 8 – 10 |
| Metadata | `0x080E0000` – `0x080FFFFF` | 128 KB | 11 |

> [!IMPORTANT]
> **Metadata 为什么是追加日志**：F4 的擦除单元是整个 sector。sector 11 只有一个擦除单元，放不下两份互相独立的副本，因此改为扇区内追加写：每条记录 32 字节，写满整个扇区才擦除回收一次。追加写本身掉电安全（半截记录 CRC 不通过，上一条仍有效）；唯一的无记录窗口是日志写满后整扇区擦除到新记录写完之间，此时设备停在升级模式，可恢复但不会自动启动 App。

> [!NOTE]
> 两槽**等大**（各 384 KB），打包工具只需一条容量规则。sector 5–11 共 7 个 128 KB 扇区，是奇数，无法等分成两个 448 KB；384 + 384 之后必然剩下一个扇区，正好留给 Metadata。sector 4 是唯一的 64 KB 扇区，划入任一槽都会破坏等容，因此独立作参数数据区（NVS）。


## 🚀 快速开始

两条构建路径，产物地址与分区逐项一致，按手头工具任选其一。

### 方式一：Keil MDK-ARM

用 Keil 分别打开两个工程，编译并烧录：

```text
Keil_OTA_Boot/MDK-ARM/Keil_OTA_Boot.uvprojx   ← Bootloader（0x08000000，64 KB 边界）
Keil_App/MDK-ARM/Keil_App.uvprojx             ← 应用工程（0x08020000，Slot A）
```

烧录顺序与邮箱观察方法见 [实现详解 §6](docs/04-boot-implementation.md)。

### 方式二：CMake + arm-none-eabi-gcc

无需 Keil 授权，三目标一次编出：

```bash
git clone https://github.com/1duck-duck1/cortex-m-ota-boot.git
cd cortex-m-ota-boot
cmake -B build/f407 -G Ninja -DBOARD=stm32f407_demo
cmake --build build/f407
```

产物落在 `build/f407/`：

| 目标 | 产物 | 烧录地址 | 说明 |
|---|---|---|---|
| `boot` | `boot.hex` / `boot.bin` | `0x08000000` | Bootloader，占 sector 0-3 共 64 KB |
| `app_slot_a` | `app_slot_a.hex` / `.bin` | `0x08020200` | App，链接于槽首 +0x200 |
| `app_slot_b` | `app_slot_b.hex` / `.bin` | `0x08080200` | App（Slot B） |

> [!IMPORTANT]
> App 的链接基址 `0x08020200` 落在页内偏移 0x200，链接器会把首个 LOAD 段下对齐到 `0x08020000`。**用调试器直烧 `.elf` 会把槽首头区写成 ELF 头字节**，请一律烧 `.hex` / `.bin`（两者起始地址已是 `0x08020200`）。

### 首次上电与 `ota adopt`

若 App 是调试器直烧进槽的（没走 Boot 的 YMODEM 接收），元数据里没有它的记录，Boot 不会跳转。此时从串口（USART2，115200-8-N-1）进 Boot CLI 补录：

```text
ota adopt A 0x00010000     # 扫描 Slot A 向量表，反推镜像长度与 CRC32，写入元数据并标为 pending
reset                      # 复位后按决策流程启动该槽，App 自检通过即自动确认
```

随后即可走正式升级：

```text
ota recv 0x00020000        # 进入 YMODEM 等待，目标槽由 Boot 自行推导（非激活槽）
```

再用上位机（`Tool/固件升级上位机/`，C# WinForms）发送已签名的固件包。Python 版 `ota flash --port COM3 firmware.pkg` 属 v0.5.0，**尚未实现**。

> [!IMPORTANT]
> **默认强制验签**：`boot_conf.h` 的 `BOOT_REQUIRE_SIGNATURE` 出厂为 `1`，未签名的固件会被 Boot 拒绝。首次使用先执行 `固件升级上位机.exe --genkey signing.key` 生成密钥对，把公钥填入 `Keil_OTA_Boot/Boot/Crypto/Src/boot_pubkey.c` 并重编译 Boot；私钥 `signing.key` 放在上位机同目录即可自动签名。开发期如需跳过验签，把该开关置 `0` —— 这也是 `ota adopt` 唯一可用的前提（设备侧无私钥，无法为直烧的槽补签名）。

**工具链版本**：详见 [工具链与版本](#-工具链与版本)。

> [!TIP]
> `v0.3.0` 是项目的质变点：它证明"任意时刻断电都能恢复"这条不变式真的成立。这条验收标准是硬性的 —— 做不到就回退重构，不许用"多试几次就好了"掩盖。

## 🔍 特性对标

对标 MCUboot · OpenBLT · stm32-bootloader · X-CUBE-SBSFU · TinyUF2 等主流实现，提炼出六个可独立裁剪的维度；每个版本的路线都对应"达到某项目某项能力"：

| 维度 | 本项目路线 |
|---|---|
| 传输通道 | UART-YMODEM（v0.2）→ CAN / USB-DFU / UF2（v1.0+ 候选） |
| 槽策略 | A/B 双槽，切换仅提交元数据 |
| 安全等级 | CRC（v0.2）→ **Ed25519 签名（已落地）** + 防回滚（v0.4） |
| 回滚机制 | 计数器确认（F-04）→ 状态机自动回滚（v0.3） |
| 入口方式 | App 回跳请求 → 超时窗口（候选） |
| 元数据存放 | `.noinit` SRAM 邮箱（运行期）+ Flash 追加日志（持久期，独占 sector 11） |

## 📁 仓库结构

```text
.
├── Keil_OTA_Boot/            Bootloader 工程（Keil MDK-ARM）
│   ├── Boot/                 业务模块（Config / Core / Mailbox / Storage / Protocol / Crypto）
│   ├── Core/                 CubeMX 生成（时钟 / GPIO / 中断）
│   ├── Drivers/              ST 官方代码（CMSIS + HAL 最小子集）
│   └── MDK-ARM/              工程文件 + 链接脚本（64 KB 边界）
├── Keil_App/                 应用工程（Keil MDK-ARM，链接到 Slot A 0x08020200）
│   ├── App/                  业务模块（Config / Platform / Services / BootClient）
│   ├── Core/                 CubeMX 生成（USART1/2 · TIM2）
│   ├── Drivers/              ST 官方代码
│   └── MDK-ARM/              工程文件 + 链接脚本
├── cmake/                    GCC 工具链文件、启动文件与三个链接脚本（boot / app_slot_a / app_slot_b）
├── Common/ota/               Boot 与 App 共享的硬件无关模块（YMODEM / CLI / CRC32 / 镜像头 / 升级请求）
├── Tool/                     上位机（C# WinForms 固件升级工具，含 Ed25519 签名与 --genkey）
├── docs/                     设计与实现文档
└── .github/workflows/        CI
```

> [!TIP]
> 规划中的 `core` / `mcu` / `transport` 分层（纯逻辑层 PC 可测，见 [架构设计](docs/01-architecture.md)）将在 v0.2.0 引入传输层时落地，v0.1.0 按"最小闭环"纪律直接以 Keil 工程交付。


## 🤝 贡献

欢迎提交 Issue 与 PR。开始之前请阅读 [开发指南](docs/02-git-and-github.md)，其中包含提交规范与分支策略。

- 🐛 发现 Bug → 提 Issue
- 💡 有新想法 → 先提 Issue 讨论，避免做无用功
- 💻 想贡献代码 → 从标记 `good first issue` 的任务开始

## 📄 许可证

[Apache License 2.0](LICENSE) · 允许商用，需保留版权声明与许可声明

<div align="center">

**如果这个项目对你有帮助，点个 ⭐ 是最好的鼓励**

</div>