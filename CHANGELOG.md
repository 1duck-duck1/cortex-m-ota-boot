# Changelog

本项目的所有重要变更都会记录在此文件。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Added

- 项目规划文档 `docs/00-project-plan.md`：定位、功能清单、里程碑与验收标准、开源运营与风险清单
- 架构设计文档 `docs/01-architecture.md`：分层架构、芯片层接口、分区布局、数据格式、升级状态机、平台约束、测试策略
- 开发指南 `docs/02-git-and-github.md`：Git 配置、提交规范、分支策略、GitHub 落地流程
- 仓库骨架：`code/` 下的 `core` / `mcu` / `transport` / `app` / `bsp` / `tools` / `tests` 目录
- README、Apache-2.0 许可证、`.gitignore`、CI 工作流

### Changed

- 确定 HAL 混合策略：App 全 HAL；Bootloader 时钟/串口用 HAL（轮询模式），Flash 擦写用寄存器级 + RAMFUNC（见 `docs/01-architecture.md` §8.4）
- 目录约定新增 `Drivers/`（ST 官方代码 vendored 最小子集：CMSIS + 用到的 HAL 模块）

### 说明

当前处于早期规划阶段，尚无可用代码。

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