# Changelog

本项目的所有重要变更都会记录在此文件。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Added

- `Keil_App/` 独立应用工程（CubeMX 骨架：USART1/2 + TIM2），链接基址 Slot A `0x08020000`
- Boot 与 App 两侧显式链接脚本（scatter）：Boot 限 32 KB、App 限 Slot A 384 KB，SRAM2 顶部 32 字节邮箱在两侧均由链接器保留
- Boot 元数据模块 `boot_metadata`（双副本 load/commit、mark_pending/confirm/rollback）、CRC 模块 `boot_crc`、公共类型 `boot_types`、Flash 驱动 `boot_flash`

### Changed

- Flash 分区调整为对称双槽：Slot A/B 各 384 KB（sector 5-7 / 8-10）；sector 4 划为参数数据区（NVS 预留），sector 11 整块保留（原 448/448、保留区与 Slot B 共用 sector 11 的布局废弃）
- 跨复位通信由 RTC 备份寄存器迁移至 `.noinit` SRAM 邮箱（`0x2001FFE0`，magic + CRC32 保护），Boot 不再使能 PWR/RTC
- Boot Flash 擦写由 HAL 改为寄存器级 + 有界轮询（架构 §8.4 兑现：消除 HAL 超时依赖 SysTick、擦写期间 tick 冻结导致的无界死循环风险）
- 文档同步：架构设计 §6 数据格式以 `boot_types.h` 实际定义为准重写、§7.4 补 confirm 消费落盘语义；实现详解 §1/§3 重写为 SRAM 邮箱与元数据双副本视角、§7 妥协清单扩充；README 功能表 F-10/F-12 状态更新

### Fixed

- App 确认消费时未将元数据启动计数清零：无进行中切换（pending==NONE）时计数持续累积，反复确认最终会误触发回滚；现确认消费无条件落盘"计数清零"，仅元数据本就干净时跳过提交

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
- 确定 HAL 混合策略：App 全 HAL；Bootloader 时钟/串口用 HAL（轮询模式），Flash 擦写用寄存器级 + RAMFUNC（见 `docs/01-architecture.md` §8.4）

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