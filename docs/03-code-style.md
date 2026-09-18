---
title: 代码风格与命名规范
aliases:
  - 代码规范
  - Code Style
tags:
  - project/design
  - ota
  - bootloader
  - coding-convention
status: active
created: 2026-09-17
updated: 2026-09-17
---

# 03 代码风格与命名规范

> [!NOTE]
> 本文约束**本项目源码**（`Boot/`、`App/` 等）。CubeMX 生成的 `Core/` 与 ST 的 `Drivers/` 是第三方产物，**保持原样、不重命名**——重新生成时 CubeMX 只认自己的命名。

---

## 1. 目录组织

```
Keil_OTA_Boot/
├── Core/                    CubeMX 生成：入口 main.c、时钟、中断
├── Drivers/                 CubeMX 管理：HAL + CMSIS
├── Boot/                    ★ Bootloader 业务代码（只进 Boot target）
│   ├── boot_conf.h          分区表 / 魔数 / 阈值——唯一事实来源
│   ├── boot_flag.c/.h       备份寄存器标志（跨复位通信）
│   ├── boot_jump.c/.h       校验 + 剥离环境 + 跳转
│   └── boot_main.c/.h       主流程 boot_run()
├── App/                     ★ App 侧代码（只进 fake_app target）
│   ├── app_main.c           fake_app 入口
│   └── boot_client.c/.h     App 调用 Boot 约定的最小库
└── MDK-ARM/                 Keil 工程与双 target（Keil_OTA_Boot / fake_app）
```

**职责边界**：`Core/Src/main.c` 的 USER CODE 区只允许一行 `boot_run()` 调用；所有业务逻辑在 `Boot/`。跳转类代码（改 MSP、改 VTOR）只允许出现在 `Boot/boot_jump.c`。

## 2. 命名规则

| 对象 | 规则 | 示例 |
|---|---|---|
| 文件 | 全小写 + 下划线，模块前缀开头 | `boot_flag.c` |
| 函数 | `模块_动词`，小写下划线 | `boot_jump_slot()` |
| 静态函数 | 同上（靠 `static` 区分，不加 `s_` 前缀） | `vector_table_valid()` |
| 类型 | 小写下划线 + `_t` 后缀 | `boot_mcu_t` |
| 宏 / 常量 | `BOOT_` 前缀 + 全大写 | `BOOT_SLOT_A_ADDR` |
| 魔数 | 一律定义在 `boot_conf.h`，带注释说明含义 | `BOOT_MAGIC_REQUEST` |
| 头文件保护伞 | 文件名全大写 + `_H` | `BOOT_FLAG_H` |

**前缀语义**：`boot_` = Bootloader 侧；`app_` / `boot_client_` = App 侧。两侧代码物理隔离（不同 target），前缀用于防止将来共享代码时命名冲突。

## 3. 文件头与函数注释

- 每个新文件带统一头注释：`@file`（文件名）、`@brief`（一句话职责）；复杂模块可加 `@note` 说明关键约束（如"计数必须先于跳转提交"）。
- 对外函数（头文件声明）写 `@brief` / `@param` / `@retval`；`static` 函数只在逻辑不自明时写一行 `@brief`。
- 注释解释**为什么**（约束、顺序、坑），不复述代码。例：

```c
/* 7. 恢复全局中断（PRIMASK=0）。
 *    必须在改 MSP 之前做完：换栈之后 C 语句就不可靠了。 */
__enable_irq();
```

## 4. 排版

- 缩进 4 空格（与 CubeMX 生成代码一致），大括号风格跟随 ST：函数体换行开、语句块同行开。
- 一行一条语句；指针星号靠近名字：`boot_mcu_t *mcu`。
- `#include` 顺序：自身头文件 → 本项目头（`boot_*.h`）→ HAL/CMSIS → 标准库。

## 5. 硬性纪律（违反即 bug）

1. **所有地址、魔数、阈值只在 `boot_conf.h` 定义一次**，其他文件 include 使用——改分区表时只改一处加一处文档。
2. **CubeMX USER CODE 区外不改**；USER CODE 区内尽量薄（调用，不实现）。
3. **跳转前必须消费的标志：`boot_attempts++` 先于跳转提交**（架构文档 §7.4）。
4. **Bootloader 禁止 `malloc` / 浮点 / 可变长数组**（全程静态分配）。
5. 修改 `Boot/` 下任何跳转逻辑前，先读 [01-architecture.md](01-architecture.md) §8.2 清单。

## 6. 提交规范

沿用约定式提交前缀：`feat:`（新功能）、`fix:`（修复）、`docs:`（文档）、`chore:`（工程配置）、`refactor:`（重构）。提交信息用中文描述，例：`feat(boot): 实现 v0.1.0 双向跳转闭环`。
