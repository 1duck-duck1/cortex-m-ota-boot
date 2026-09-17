# Changelog

本项目的所有重要变更都会记录在此文件。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 说明

当前无未发布变更。下一版本 v0.2.0 计划：UART + YMODEM 升级到非激活槽，见下方版本规划索引。

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