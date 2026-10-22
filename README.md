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

> [!NOTE]
> **当前状态：v0.2.0 开发中，v0.4.0 的 X-01 镜像验签已落地**
> v0.1.0 双向跳转闭环已交付；v0.2.0 已完成 Flash 分区对称化（A/B 各 384 KB）、Boot/App 两侧链接边界收窄、跨复位通信迁移至 `.noinit` SRAM 邮箱，并打通 UART + YMODEM 升级链路——Boot 侧 `boot_update` 与 App 侧 `app_ota_service` 共用 `Common/ota/` 的硬件无关模块（YMODEM / CLI / CRC32 / 镜像头）。App 侧 `app_boot_client` 已集成：试运行固件在启动自检通过后自动确认并复位。**X-01 镜像验签已落地**：镜像头升至 v2、Boot 侧 `boot_verify` 以内置公钥验 Ed25519 签名、上位机负责签名与传签（`--genkey` 生成密钥对）。待办：X-02 防回滚、IWDG 看门狗回滚（v0.3）与真板验收。构建路径已双轨：Keil MDK-ARM（AC5）与 CMake + arm-none-eabi-gcc 均可编出同一套产物，地址与分区逐项一致。

---

## 📖 目录

- [🚀 Cortex-M OTA Boot](#-cortex-m-ota-boot)
  - [📖 目录](#-目录)
  - [🎯 这是什么](#-这是什么)
  - [✨ 为什么用它](#-为什么用它)
  - [🔧 硬件要求](#-硬件要求)
  - [🗺️ Flash 分区布局](#️-flash-分区布局)
  - [🏗️ 架构](#️-架构)
  - [🔄 升级流程](#-升级流程)
  - [✅ 功能清单](#-功能清单)
    - [核心功能](#核心功能)
  - [🚀 快速开始](#-快速开始)
  - [🛣️ 路线图](#️-路线图)
  - [🔍 特性对标](#-特性对标)
  - [📁 仓库结构](#-仓库结构)
  - [📚 文档](#-文档)
  - [⚠️ 风险提示](#️-风险提示)
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

市面上常见的 F407VGT6 开发板即可，无需自制硬件。

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

## 🏗️ 架构

```mermaid
flowchart TD
    APP["app/ ── 应用侧库<br/>boot_confirm 确认新固件<br/>boot_request_update 请求升级"]

    subgraph CORE["core/ ── 纯逻辑层（零硬件依赖 · PC 可测）"]
        direction LR
        C1["boot_fsm<br/>升级状态机"]
        C2["boot_meta<br/>元数据追加日志"]
        C3["boot_image<br/>固件头解析"]
        C4["boot_proto<br/>YMODEM / 命令"]
        C5["boot_flash<br/>通用 Flash 操作"]
    end

    MCU["mcu/ 芯片层（只有 stm32f4）<br/>时钟 · Flash 擦写 · 跳转 · 串口"]
    TRAN["transport/ 传输层<br/>uart（v0.2）· can · eth"]
    HW["STM32F407VGT6（Cortex-M4）"]

    APP --> CORE
    CORE --> MCU
    CORE --> TRAN
    MCU --> HW
    TRAN --> HW

    classDef core fill:#e3f2fd,stroke:#1976d2,color:#0d47a1
    classDef hw fill:#f5f5f5,stroke:#9e9e9e,color:#424242
    class C1,C2,C3,C4,C5 core
    class HW hw
```

> [!NOTE]
> 上图是**目标形态**（规划的 `core` / `mcu` / `transport` 分层）；当前实际布局是 `Keil_OTA_Boot/`（Boot）+ `Keil_App/`（应用）+ `Common/ota/`（两侧共享的硬件无关模块），详见 [📁 仓库结构](#-仓库结构) 与 [代码风格规范](docs/03-code-style.md)。

依赖方向严格单向：`core` 只依赖 `mcu` 与 `transport` 的**接口**。`core` 被保留下来的唯一理由是"能在 PC 上跑断电注入测试"——掉电安全这件事必须能自动化验证，而不是靠手拔电碰运气。

## 🔄 升级流程

升级拆成**两个正交的状态层**：一次 OTA 传输走"接收会话"，每次上电走"启动决策"；两者只通过持久元数据与 SRAM 邮箱衔接。

```mermaid
flowchart TD
    subgraph RECV["接收会话 · boot_update_state_t"]
        direction LR
        R0(["IDLE"]) --> R1["HANDSHAKE"]
        R1 --> R2["RECEIVE"]
        R2 --> R3["FINAL"]
        R2 -.->|超时 / 重传超限 / 中止| RF["FAILED"]
        R3 -.->|复核失败| RF
        R3 --> RD["DONE"]
        RF --> R0
    end

    subgraph DECIDE["启动决策 · boot_decision_t"]
        direction TB
        DD{"boot_main_decide()"}
        DD -->|pending 有效且未超次数| D4["RUN_PENDING"]
        DD -->|active 有效| D3["RUN_ACTIVE"]
        DD -->|active 失效、另一槽可用| D2["RUN_RECOVER"]
        DD -->|两槽都不可用| D1["WAIT_UPDATE"]
        D4 -.->|confirm 转正| D3
        D4 -.->|计数用尽、clear_pending| D3
    end

    RD -->|软复位后重新决策| DECIDE
```

> [!IMPORTANT]
> **核心不变式：任意时刻断电，下次上电必然回到一个可启动的状态。**
> 这是整个项目最重要的一条设计约束，全部实现细节都服务于它。

## ✅ 功能清单

图例：✅ 已实现 · 🚧 进行中 · 📋 计划中

### 核心功能

| 编号 | 功能 | 状态 |
|---|---|:---:|
| F-01 | 应用有效性校验（magic / CRC / HW ID） | ✅ 镜像头 + 载荷 CRC32 + 向量表 + 签名四道关（`boot_image` + `boot_verify`） |
| F-02 | 应用跳转（VTOR / MSP / 外设反初始化） | ✅ |
| F-03 | 应用侧请求进入 Bootloader | ✅ `app_boot_client_request_boot()`（SRAM 邮箱 + 软复位） |
| F-04 | 启动失败保护 | ✅ 试运行计数耗尽即回滚到 active 槽 |
| F-05 | 传输抽象层 | 📋 |
| F-06 | YMODEM 接收（含重传与超时） | ✅ `boot_update` + `Common/ota/ota_ymodem` |
| F-08 | 固件头解析 | ✅ `Common/ota/ota_image` + `boot_image` |
| F-09 | 固件合法性校验 | ✅ 镜像头自洽 + 载荷 CRC32 + SP/PC |
| F-10 | 元数据追加日志更新 | ✅ `boot_metadata` 扇区内追加写 load/save，随升级链路串联 |
| F-11 | 升级状态机 | ✅ `boot_update_state_t` 接收会话状态机 |
| F-12 | A/B 分区切换 | ✅ mark_pending / confirm / clear_pending |
| F-13 | 掉电安全 | 🚧 元数据追加日志 + 邮箱已就绪，断电注入测试待 v0.3 |
| F-14 | 启动失败自动回滚 | 🚧 计数耗尽回滚已连通；无看门狗，卡死固件需自身复位（v0.3） |
| F-15 | 应用侧确认接口 | ✅ `app_boot_client_confirm_and_reset()` |
| F-16 | 自身分区写保护 | ✅ 目标槽由 Boot 推导，受保护槽不擦除 |
| F-17 | 固件打包工具 | 🚧 `ota adopt` 可从既有槽反推长度/CRC 落元数据；PC 打包器待补 |
| F-18 | 上位机升级工具 | 🚧 C# 上位机可用（`Tool/固件升级上位机/`）；Python 版归 v0.5.0 |
| F-19 | 串口命令行 | ✅ `Common/ota/ota_cli` + Boot/App 各自命令层 |
| F-21 | PC 端单元测试 | 📋 |
| X-01 | Ed25519 签名校验 | ✅ 镜像头 v2 + Boot 侧 `boot_verify` 验签；上位机签名 + 第 0 包传签（`--genkey` 生成密钥对） |

<details>
<summary><b>计划中的增强功能</b></summary>

防回滚、固件加密、CAN 传输、双 Bank 无搬运升级、Factory 救援固件。

</details>

<details>
<summary><b>明确不做的事（防止范围蔓延）</b></summary>

Bootloader 自更新、差分升级、云平台/服务器端、图形化上位机、全厂商芯片覆盖、RTOS 深度集成、动态内存分配。

</details>

完整的功能范围与取舍理由见 [项目规划](docs/00-project-plan.md)。

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

**工具链依赖**：Keil 路径需 MDK-ARM 5.38+（AC5）；CMake 路径需 `arm-none-eabi-gcc` · `cmake` ≥ 3.20 · `ninja`。

## 🛣️ 路线图

| 版本 | 内容 | 验收标准 |
|:---:|---|---|
| **v0.1.0** | 最小 Bootloader，双向跳转 | STM32F407VGT6 真板上双向跳转成功 |
| **v0.2.0** | UART + YMODEM 升级到非激活槽 | 手动完成一次完整升级 |
| **v0.3.0** | 掉电安全状态机 + 回滚 | PC 模拟 500 次随机断电全恢复 · 真板拔电 100 次全恢复 |
| **v0.4.0** | Ed25519 签名 + 防回滚 | 篡改固件被拒绝（签名已落地，防回滚待做） |
| **v0.5.0** | Python 上位机（pip 可装） | 一条命令完成升级 |
| **v1.0.0** | 接口冻结 + 完整文档 | 外部开发者照文档在 F407 上独立完成一次升级 |

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

> [!TIP]
> 裁剪 = 编译期关闭 `boot_conf.h` 开关，未选中的模块不进固件。完整对标矩阵与裁剪纪律见 [生态对标](docs/05-bootloader-landscape.md)。

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

## 📚 文档

| 文档 | 内容 |
|---|---|
| [项目规划](docs/00-project-plan.md) | 定位、功能清单、里程碑与验收标准、开源运营、风险清单 |
| [架构设计](docs/01-architecture.md) | 分层架构、芯片层接口、分区布局、数据格式、升级状态机、F4 平台约束 |
| [开发指南](docs/02-git-and-github.md) | Git 配置、提交规范、分支策略、文档写作规范 |
| [代码风格](docs/03-code-style.md) | 命名规范、CubeMX 区纪律、注释与格式硬性纪律 |
| [实现详解](docs/04-boot-implementation.md) | 实现决策记录：SRAM 邮箱通信、跳转清单逐项分析、掉电窗口、已知妥协、上板验收手册 |
| [生态对标](docs/05-bootloader-landscape.md) | 主流 Bootloader 实现调研、六维度裁剪矩阵、分版本对标路线 |

## ⚠️ 风险提示
> [!CAUTION]
> **固件升级操作有使设备无法启动的风险。**
>
> - 本方案的掉电安全机制经充分测试后，可保证升级过程断电不损坏已有固件
> - 但**首次烧录 Bootloader 本身**仍需使用 SWD / JTAG 编程器
> - 若设备已无法启动，始终可以通过 SWD 接口重新烧录恢复
> - 请勿在量产设备上直接测试未经充分验证的版本

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