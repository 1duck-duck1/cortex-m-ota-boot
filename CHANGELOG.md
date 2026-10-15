# Changelog

本项目的所有重要变更都会记录在此文件。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Added

- `Keil_App/` 独立应用工程（CubeMX 骨架：USART1/2 + TIM2 + DMA），Slot A 链接基址 `0x08020200`（槽底 `0x08020000` + 0x200 镜像头区）
- Boot 与 App 两侧显式链接脚本（scatter）：Boot 限 32 KB（sector 0-1）并单列 `LR_RAMCODE` 段常驻 RAM 存放 Flash 擦写函数、App 限 Slot A/B 各 384 KB，SRAM2 顶部 32 字节邮箱在两侧均由链接器保留
- Boot 侧模块：`boot_metadata`（双副本 load/save、mark_pending/confirm/clear_pending）、`boot_image`（镜像头 + 载荷 CRC32 + 向量表三道关）、`boot_flash`、`boot_nvs`、`boot_update`（YMODEM 接收会话状态机）、`boot_cli`、`boot_adopt`、公共类型 `boot_types`
- App 侧模块：`app_ota_service`（OTA 接收会话）、`app_transport`（USART2 循环 DMA + IDLE + 4 KB 环形缓冲）、`app_flash`、`app_boot_client`、`app_cli`
- `Common/ota/` 硬件无关共享模块（`ota_ymodem` / `ota_cli` / `ota_crc32` / `ota_image` / `ota_request`），由 Boot 与 App 两个工程各自编译同一份源码
- C# 上位机升级工具（`Tool/固件升级上位机/`，WinForms）：握手 / 分块 / 重传 / 擦除 / 终局 ACK
- **Boot 侧 Ed25519 镜像验签（X-01）**：`Boot/Crypto/boot_verify`（`SHA-512(prologue‖载荷)` 流式摘要 + 分块读 Flash）、`Boot/Crypto/boot_pubkey`（内置公钥表，编号 1）、vendored Monocypher（CC0，`<OneElfS>` 选择性链接只保留 ed25519/SHA-512 子集）；验签接在启动校验、接收收尾、NVS 请求提交与 `ota adopt` 四处
- **镜像头升至 v2**：新增 `pubkey_id` / `sig_algorithm` / `signature[64]` 域；`OTA_IMAGE_SIGNED_PREFIX_SIZE`（= `offsetof(header_crc32)` = **28 字节**）作为上位机与 Boot 之间的唯一签名契约
- **YMODEM 第 0 包扩展签名传递**：改 1024 字节长帧（STX），第二字段追加 `sig=<128 hex>` 与 `kid=<2 hex>`；结束包不受影响仍为 SOH/128
- `Common/ota/ota_request` 升级请求扩展签名域（`sig_algorithm` / `pubkey_id` / `signature`），并调整记录 CRC32 覆盖范围
- App 侧签名透传：`app_ota_service` 解析并保存签名、`app_nvs_write_request` 落盘签名块、`app_cli` 的 `ota` 报告新增 `sig` / `kid` 字段
- C# 上位机签名支持（BouncyCastle.Cryptography）：`Crypto/FirmwareSigner` 按 28 字节 prologue 计算 `SHA-512(prologue‖image)` 后做 Ed25519 签名、`Crypto/Ed25519KeyFile` 读私钥、`Program.cs --genkey <path>` 生成密钥对（公钥写入 `<path>.pub`）
- `boot_conf.h` 新增 `BOOT_REQUIRE_SIGNATURE` 开关（量产置 1 强制验签 / 开发置 0 跳过并放行 `ota adopt`）

### Changed

- Boot 串口接收由 HAL 轮询改为逐字节中断 + 512 字节环形缓冲（`boot_transport.c`，`USART2_IRQHandler` 注册于 `stm32f4xx_it.c`）；发送侧仍为 HAL 轮询。架构 §8.3/§8.4 与项目规划 §2 的"Bootloader 串口轮询模式"表述随之修正为"发送轮询 / 接收中断+环形缓冲"
- **不兼容**：镜像头 `header_version` 由 1 升为 2（新增签名相关域），新头与旧固件、旧上位机互不兼容，须两侧同步升级
- Boot 启用 microlib（`Keil_OTA_Boot.uvprojx` 的 `<useUlib>` 置 1），以容纳验签代码并压缩 C 库
- Boot 链接脚本收紧：`LR_IROM1` 由 `0x7800` 放宽到 `0x7C00`，`LR_RAMCODE` / `ER_RAMCODE` 由 `0x08007800/0x800` 收为 `0x08007C00/0x400`，`RW_IRAM2` 改为 `0x2001BC00/0x43E0`——Boot 仍完整占满 sector 0-1 的 32 KB
- 文档同步：项目规划 §3.2 X-01 标记为已实现、§4 v0.4.0 里程碑更新；实现详解新增 §8 镜像验签与 §5.3 32 KB 预算说明，§2.1 校验档位补入验签这道关
- Flash 分区调整为对称双槽：Slot A/B 各 384 KB（sector 5-7 / 8-10）；sector 4 划为参数数据区（NVS 预留），sector 11 整块保留（原 448/448、保留区与 Slot B 共用 sector 11 的布局废弃）
- 跨复位通信由 RTC 备份寄存器迁移至 `.noinit` SRAM 邮箱（`0x2001FFE0`，magic + CRC32 保护），Boot 不再使能 PWR/RTC
- Boot Flash 擦写使用 HAL（`HAL_FLASHEx_Erase` / `HAL_FLASH_Program`），并将擦写入口经 `BOOT_RAMFUNC` 搬进 `BOOT_RAMCODE` 段常驻 RAM，避免擦写期间从 Flash 取指；HAL 超时依赖 SysTick，Boot 已开启且擦写期间不关中断（架构 §8.4）
- 文档同步：架构设计 §6 数据格式与代码实际定义对齐重写、§7.4 补 confirm 消费落盘语义；实现详解 §1/§3 重写为 SRAM 邮箱与元数据双副本视角、§7 妥协清单扩充；README 功能表 F-10/F-12 状态更新
- 文档修正：架构 §6 数据格式改为与代码一致的 ota_image_header_t（槽首 0x200 头区）与 boot_metadata_t（32 字节双副本）、§7 状态机改为「接收会话状态机 + 启动决策」两层；实现详解 §0 代码地图更新为实际目录；README 升级流程与项目规划 F-08/F-09/F-11/F-14/F-15 描述对齐代码。

### Fixed

- App 确认消费时未将元数据启动计数清零：无进行中切换（pending==NONE）时计数持续累积，反复确认最终会误触发回滚；现确认消费无条件落盘"计数清零"，仅元数据本就干净时跳过提交
- Boot 工程编译优化级别被重置为 `-O0`（target 级 `<Optim>=1`）导致代码区溢出约 7 KB、链接报 L6406E/L6407E 而无法构建；已改回 `-O2`（`<Optim>=3`，实测 `-O3` 反而更大），ROM 回到 `0x7AB0`（约 31.4 KB），构建 `0 Error / 0 Warning`
- **验签恒报 `SIGNATURE_FAILED`**：`boot_update_finalize()` 重建的 `verify_header` 经 `memset` 清零后漏填 `sig_algorithm`，使其为 0（`OTA_IMAGE_SIG_NONE`），被 `boot_verify_image()` 的"未签名直接拒绝"分支挡住——签名正确也一律失败；现补上 `sig_algorithm` / `pubkey_id`（均在 28 字节签名前缀之外，不影响被签哈希）
- **Boot 验签阶段整机静默（栈溢出）**：验签链路是本工程栈占用最深处，`armlink --callgraph` 静态分析给出的最大栈深为 **2968 字节**（`boot_image_check → boot_verify_image → crypto_ed25519_check → crypto_eddsa_check_equation → ge_tobytes → fe_invert → invsqrt → fe_sq`），而 CubeMX 生成的 `MDK-ARM/startup_stm32f407xx.s` 默认只有 `Stack_Size EQU 0x400`（1 KB）；真机一旦真正走进验签即栈溢出触发 HardFault，表现为主机上送 EOT 后既无 `OK ota=done` 也无 `ERR` 行（此链路此前被上面那条 `sig_algorithm` 漏填提前挡住、从未真正执行到，故未暴露）；现 `Stack_Size` 提到 `0x1000`（4 KB），余量约 1.1 KB

### Removed

- `Keil_OTA_Boot/App/` 演示工程 fake_app 与应用侧 boot_client（应用侧改由 `Keil_App/` 承担，`boot_client` 随 v0.2.0 重新集成）

---

## [0.1.0] - 2026-09-18

### Added

- 项目规划文档 `docs/00-project-plan.md`：定位、功能清单、里程碑与验收标准、开源运营与风险清单
- 架构设计文档 `docs/01-architecture.md`：分层架构、芯片层接口、分区布局、数据格式、升级状态机、平台约束、测试策略
- 开发指南 `docs/02-git-and-github.md`：Git 配置、提交规范、分支策略、GitHub 落地流程
- **Bootloader 最小闭环**（`Keil_OTA_Boot/Boot/`）：
  - RTC 备份寄存器跨复位通信 `boot_flag`（三步使能序列，F-03/F-04/F-15 数据层）
  - 向量表 SP/PC 校验与环境剥离跳转 `boot_jump`（F-01 简化版 + F-02，含 FPU lazy stacking 与 NVIC pending 清理）
  - 主流程 `boot_main`：确认消费 → 请求消费 → 启动计数保护 → 跳转
- **应用侧接口与演示应用**（`Keil_OTA_Boot/App/`）：`boot_client`（init / confirm / request_update / heartbeat）与 `fake_app`（5 秒自动回跳，构成双向跳转闭环）
- 两个独立 Keil MDK 工程：Boot @ `0x08000000`（sector 0-1）、fake_app @ `0x08010000`（sector 4-7），共享源码以相对路径引用，UV4 命令行批编译零错误
- 文档：代码风格规范 `docs/03-code-style.md`、实现详解 `docs/04-boot-implementation.md`（决策记录、跳转清单逐项分析、掉电窗口分析、上板验收手册）

### Changed

- README 状态从"早期规划"更新为 v0.1.0：功能清单勾选 F-02 / F-03 / F-04 / F-15，快速开始补充 Keil 工程实际路径，仓库结构反映 `Keil_OTA_Boot/` 实际形态
- 仓库结构以 `Keil_OTA_Boot/` 实际工程替代 `code/` 占位目录（规划的 core/mcu/transport 分层随 v0.2.0 传输层落地）
- `.gitignore` 补充 Keil MDK 产物目录与 J-Link 日志规则
- 确定 HAL 混合策略：App 全 HAL；Bootloader 时钟/串口用 HAL（轮询模式），Flash 擦写用 HAL + `BOOT_RAMFUNC`（擦写入口常驻 RAM，见 `docs/01-architecture.md` §8.4）

### 说明

v0.1.0 验收标准为 STM32F407VGT6 真板双向跳转成功。双工程编译零错误已达成；真板验收步骤见 `docs/04-boot-implementation.md` §6。

---

## 版本规划索引

以下为计划中的版本与验收标准，详见 `docs/00-project-plan.md`。

| 版本 | 内容 |
|---|---|
| v0.1.0 | 最小 Bootloader，双向跳转（STM32F407VGT6） |
| v0.2.0 | UART + YMODEM 升级到非激活槽 |
| v0.3.0 | 掉电安全状态机 + 自动回滚 + PC 端断电注入测试 |
| v0.4.0 | Ed25519 签名校验 + 防回滚 |
| v0.5.0 | Python 上位机工具 |
| v1.0.0 | 接口冻结与完整文档 |

<!--
后续每次发布时，把 [Unreleased] 下的内容移入新版本小节，格式：

## [0.1.0] - YYYY-MM-DD

### Added
- ...
### Fixed
- ...
-->