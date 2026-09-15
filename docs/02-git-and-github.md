---
title: Git 与 GitHub 开发指南
aliases:
  - Git 指南
  - 开发指南
  - Git and GitHub
tags:
  - project/guide
  - git
  - github
  - workflow
status: active
created: 2026-09-15
updated: 2026-09-16
---

# 02 Git 与 GitHub 开源指南

> [!NOTE]
> 面向"没用过 Git"的嵌入式工程师。本文只讲这个项目实际会用到的部分。
> 环境：Windows + PowerShell。

---

## 0. 环境检查结果（实测记录）

| 检查项 | 结果 |
|---|---|
| `git --version` | ❌ 命令不可用 |
| `C:\Program Files\Git` | ❌ 不存在 |
| `D:\Git\Git\bin\git.exe` | ⚠️ 存在，版本 **2.40.1**（2023 年版，未注册、未加入 PATH） |
| `winget install Git.Git`（2.55.0.3） | ️ 终端报告"已成功安装"，但注册表与磁盘均无痕迹（提权安装被拦截，实际未生效） |
| `git config user.name / user.email` | ❌ 未配置 |
| `winget` | ✅ 可用（v1.29.290） |

**结论**：本机只有一个 2023 年的 Git 2.40.1，且不在 PATH 中。有两条可行路径，见第 1.1 节。

> [!WARNING]
> **安全提示**：Git 2.40.1 早于 2.45.1，存在已公开的 Windows 平台递归克隆远程代码执行漏洞（CVE-2024-32002 等）。若经常克隆来源不明的仓库，建议升级到 2.45.1 以上版本。

---

## 1. 安装与初始化配置

### 1.1 安装 Git

#### 路径 A：装新版（推荐）

用 winget 安装（**必须在带界面的终端里执行**，因为安装程序会弹出 UAC 提权确认框）：

```powershell
winget install --id Git.Git -e --source winget
```

> [!CAUTION]
> **注意**：如果提权被拦截，winget 仍会打印"已成功安装"，但实际什么也没装。**装完必须验证**：

```powershell
git --version
# 或直接查注册表，这是唯一可靠的判断
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*" |
  Where-Object { $_.DisplayName -like "*Git*" } | Select-Object DisplayName, DisplayVersion
```

若查不到，说明没装成功。此时手动下载安装包并右键"以管理员身份运行"：
https://git-scm.com/download/win

安装时可选择安装目录，若选非默认目录需记住该路径。

#### 路径 B：复用已有的 2.40.1（立即可用）

本机 `D:\Git\Git` 已有一份可用的 Git 2.40.1，只是没加入 PATH。把它加进去即可：

```powershell
# 追加到当前用户的 PATH（不需要管理员权限）
$old = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$old;D:\Git\Git\cmd", "User")
# 之后重开终端
git --version   # 应输出 git version 2.40.1.windows.1
```

不需要 Git 时随时可以从 PATH 中删掉这一段，不影响系统。

安装完成后**必须重开一个终端**，然后验证：

```powershell
git --version
```

> [!TIP]
> 如果仍然提示找不到命令，说明 Git 的可执行目录没进 PATH。把**实际安装路径下的 `cmd` 目录**（例如 `C:\Program Files\Git\cmd` 或 `D:\Git\Git\cmd`）加入环境变量 Path，重开终端即可。

### 1.2 三项必要配置（Windows 上尤其重要）

```powershell
# 1) 身份信息 —— 会写进每一个 commit，必须和 GitHub 账号一致
git config --global user.name "你的名字或昵称"
git config --global user.email "你的GitHub注册邮箱"

# 2) 换行符处理 —— Windows 上不配这个，将来会满屏假改动
git config --global core.autocrlf true

# 3) 让中文文件名正常显示（本项目有中文文档）
git config --global core.quotepath false
```

再加两条提升手感的配置：

```powershell
# 中文 commit message 用 UTF-8 编码
git config --global i18n.commitEncoding utf-8

# 默认分支名用 main（GitHub 现在的默认）
git config --global init.defaultBranch main
```

验证配置：

```powershell
git config --global --list
```

**`user.email` 必须是你 GitHub 账号下已验证的邮箱**，否则 commit 不会关联到你的头像和贡献图。

---

## 2. Git 与 GitHub 的区别（先把概念理清）

| 名词 | 是什么 |
|---|---|
| **Git** | 装在你电脑上的**版本控制工具**，负责记录代码的每一次变化。完全离线可用。 |
| **GitHub** | 一个网站，托管 Git 仓库，提供协作、Issue、CI 等功能。Git 的"远程备份 + 社交平台"。 |

两者是独立的。你可以只用 Git 不用 GitHub，也可以有 Git 仓库后随时再推到 GitHub。

### 三个区域（理解这个才算入门）

```
工作区                暂存区                版本库
(你编辑的文件)  --add-->  (待提交清单)  --commit-->  (历史记录)
                                                      │
                                                  push │ pull
                                                      ▼
                                                  远程仓库 (GitHub)
```

- **工作区**：你在编辑器里改的文件。
- **暂存区（stage / index）**：你挑出来"这次提交要包含哪些改动"的清单。
- **版本库**：已经形成快照的、不可变的历史。

**为什么要有暂存区**：你可以改 10 个文件，但只把其中 3 个相关的文件提交成一个 commit。这让每个 commit 都是"一件完整的事"，而不是一坨混杂的改动。

---

## 3. 日常够用的 9 个命令

背下这 9 个，覆盖 90% 的场景。

| 命令 | 作用 | 使用频率 |
|---|---|---|
| `git status` | 看当前有哪些改动 | 最高，随时敲 |
| `git diff` | 看具体改了什么内容 | 极高 |
| `git add <文件>` | 把改动加入暂存区 | 极高 |
| `git add -p` | 分块挑选要提交的改动 | 高（进阶但值得学） |
| `git commit -m "消息"` | 提交成一个版本 | 极高 |
| `git log --oneline --graph` | 看提交历史 | 高 |
| `git push` | 推到 GitHub | 高 |
| `git pull` | 拉取远程更新 | 中 |
| `git restore <文件>` | 丢弃工作区改动（慎用） | 中 |

### 一次典型的提交流程

```powershell
git status                          # 1. 看改了哪些文件
git diff                            # 2. 确认改动内容符合预期
git add core/boot_fsm.c core/boot_fsm.h   # 3. 只加相关文件，别用 git add .
git commit -m "feat(fsm): 实现 VERIFIED 到 PENDING_TEST 的切换"   # 4. 提交
git push                            # 5. 推到 GitHub
```

**不要习惯性使用 `git add .`**，它会把临时文件、调试代码、密钥一起提交进去。这个项目里尤其危险（可能有密钥、厂商固件）。

---

## 4. `.gitignore`：必须在第一次提交前建好

**这是新手最容易犯的错**：把编译产物、IDE 中间文件、几百 MB 的二进制提交进去，仓库立刻变得又大又乱，而且**历史记录里的垃圾很难彻底清除**。

### 本项目的 `.gitignore`（首次提交前创建）

```gitignore
# ---- 构建产物 ----
build/
output/
*.o
*.d
*.a
*.elf
*.map
*.hex
*.bin
*.pkg
!docs/assets/*.bin          # 文档里的示例固件除外

# ---- CMake ----
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
*.cmake
!cmake/*.cmake

# ---- Keil MDK ----
MDK-ARM/*.uvguix.*
MDK-ARM/*.uvoptx
MDK-ARM/*.uvprojx.bak
MDK-ARM/DebugConfig/
MDK-ARM/RTE/
*.scvd
*.dep
*.crf
*.htm
*.lnp
*.axf
*.iex

# ---- IAR ----
IAR/**/Debug/
IAR/**/Release/
*.ewt
*.ewd
*.ewp.bak

# ---- 编辑器 ----
.vscode/*
!.vscode/settings.json
!.vscode/launch.json
!.vscode/extensions.json
.idea/
*.swp
*~

# ---- Python ----
__pycache__/
*.py[cod]
.venv/
venv/
dist/
*.egg-info/
.pytest_cache/

# ---- 密钥与本地配置（务必保留，防止误提交） ----
*.pem
*.key
secrets.h
boot_keys/
local_config.h

# ---- 操作系统 ----
.DS_Store
Thumbs.db
desktop.ini
```

### 已经被误提交了怎么办

```powershell
# 从版本库移除但保留本地文件
git rm --cached build/output.elf
git commit -m "chore: 移除误提交的构建产物"
```

注意：`--cached` 只从 Git 移除，文件还在磁盘上，不会删掉你的东西。

---

## 5. Commit 消息规范

用 **Conventional Commits**（开源界事实标准），好处是能自动生成 CHANGELOG，也让历史一目了然。

### 格式

```
<类型>(<范围>): <简短描述>

<可选正文：为什么这么改>

<可选脚注：Closes #12>
```

### 本项目用的类型

| 类型 | 用途 | 示例 |
|---|---|---|
| `feat` | 新功能 | `feat(meta): 元数据双副本轮换写入` |
| `fix` | 修 Bug | `fix(fsm): 修复 boot_attempts 未在跳转前提交的问题` |
| `refactor` | 重构，不改行为 | `refactor(core): 抽出 erase_unit 抽象以支持 F4 sector` |
| `perf` | 性能优化 | `perf(flash): 减少擦除单元重复查询` |
| `test` | 测试相关 | `test(meta): 增加写入过程中断电注入用例` |
| `docs` | 文档 | `docs(port): 补充 F4 RamFunc 移植说明` |
| `build` | 构建系统 | `build: 添加 arm-none-eabi-gcc 交叉编译选项` |
| `ci` | CI 配置 | `ci: 增加 host 端单元测试任务` |
| `chore` | 杂项 | `chore: 更新 .gitignore` |

### 范围（scope）建议用模块名

`core` / `fsm` / `meta` / `image` / `port` / `transport` / `tools` / `tests` / `docs`

### 好坏对比

```
❌ 更新代码
❌ 修改了一下
❌ 123
✅ feat(image): 实现固件头 CRC 与 HW ID 校验
✅ fix(port-f4): 修复擦写 Flash 时从 Flash 取指导致的 HardFault
```

**判断标准**：半年后你回来看这条 commit，能不能立刻知道它干了什么、为什么干。

---

## 6. 分支策略（第一次开源用最简的）

用 **GitHub Flow**，只有两类分支：

```
main ─●────●────────────●──────●──►   永远可编译、可发布
         \              /
          ●──●──●──────●              feature/xxx  开发分支
```

### 规则

1. `main` **永远保持可编译、可运行**，任何人都能 clone 下来直接跑；
2. 每个功能开一个分支，命名如 `feature/ymodem-receiver`、`fix/meta-crc`；
3. 功能完成后合并回 `main`（自己一个人开发可以直接本地 `merge` 再 push）；
4. 合并前必须保证 CI 通过；
5. **不要在 `main` 上直接做大改动**。

### 单人开发的简化流程

```powershell
git switch -c feature/ymodem-receiver     # 从 main 建并切到新分支
# ... 写代码、提交若干次 ...
git switch main
git merge --no-ff feature/ymodem-receiver
git push
git branch -d feature/ymodem-receiver     # 删掉已合并的分支
```

`--no-ff` 会保留一条"合并记录"，让历史能看出"这个功能是一个整体"，对开源项目可读性更好。

### 什么时候可以不用分支

只改文档、修错别字、单行修复，可以直接在 `main` 上提交。**判断标准：改动是否需要多天、是否可能中途失败。** 是，就开分支。

---

## 7. 把仓库推到 GitHub

### 7.1 先在本地初始化

```powershell
# 进入项目目录（注意：路径含 & 时必须加引号）
cd "f:\A_Git\2_OTA&Boot"

# 初始化仓库
git init

# 先建好 .gitignore 和 LICENSE，再提交
git add .gitignore LICENSE README.md CHANGELOG.md docs .github
git commit -m "chore: 初始化仓库，添加文档、许可证与 CI 配置"
```

### 7.2 配置 SSH 免密（推荐，一次配置长期省事）

```powershell
# 1. 生成密钥
ssh-keygen -t ed25519 -C "你的GitHub注册邮箱"
# 一路回车即可，密码短语可以留空

# 2. 查看公钥并复制
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub"
```

然后到 GitHub：**Settings → SSH and GPG keys → New SSH key**，粘贴公钥内容，标题随便起（如 `my-windows-pc`）。

验证：

```powershell
ssh -T git@github.com
# 出现 "Hi <你的用户名>! You've successfully authenticated" 即成功
```

### 7.3 在 GitHub 上创建仓库并关联

1. GitHub 右上角 **+ → New repository**；
2. **Repository name** 建议用纯英文短横线命名，例如 `cortex-m-ota-boot`；
3. **不要**勾选 "Add a README file"（本地已有，勾了会产生冲突）；
4. License 也不选（本地放了 LICENSE 文件）；
5. 创建后按提示关联：

```powershell
git remote add origin git@github.com:<你的用户名>/cortex-m-ota-boot.git
git branch -M main
git push -u origin main
```

`-u` 只需第一次加，之后直接 `git push` 即可。

### 7.4 关于仓库名的建议

| 项 | 建议 |
|---|---|
| 仓库名 | `cortex-m-ota-boot` / `micro-boot` / `ab-ota-boot`（纯英文、短、能搜索到） |
| Topics | `bootloader` `ota` `stm32` `cortex-m` `firmware-update` `ymodem` `embedded` |
| 描述 | 直接抄 `00-project-plan.md` 里的一句话定位 |

---

## 8. GitHub 仓库必备文件清单

| 文件 | 作用 | 优先级 |
|---|---|---|
| `README.md` | **最重要**，决定别人是否 star | 必须有 |
| `LICENSE` | 许可证。本项目建议 **Apache-2.0**（含专利授权，对安全类项目友好；想更宽松可用 MIT） | 必须有 |
| `.gitignore` | 见第 4 节 | 必须有 |
| `CHANGELOG.md` | 每个版本的变更记录 | 高 |
| `CONTRIBUTING.md` | 贡献流程、代码风格、提交规范 | 中 |
| `CODE_OF_CONDUCT.md` | 社区行为准则 | 中（GitHub 有模板） |
| `.github/ISSUE_TEMPLATE/bug_report.yml` | 标准化 Bug 反馈 | 中 |
| `.github/ISSUE_TEMPLATE/feature_request.yml` | 标准化功能请求 | 中 |
| `.github/PULL_REQUEST_TEMPLATE.md` | PR 检查清单 | 中 |
| `.github/workflows/ci.yml` | 自动编译 + 跑单元测试 | **高，从第一天就加** |

### LICENSE 怎么选

| 许可证 | 特点 | 适合本项目 |
|---|---|---|
| MIT | 最宽松，随便用，不担保 | 想最大传播可用 |
| **Apache-2.0** | 宽松 + 明确专利授权 + 要求保留声明 | ✅ 推荐，安全/商用场景更放心 |
| GPL-3.0 | 传染性，衍生作品也必须开源 | 部分企业会因此不用，不建议 |

选 Apache-2.0：到 https://choosealicense.com/licenses/apache-2.0/ 或直接在 GitHub 新建文件时选模板。

### 最小 CI 示例（`.github/workflows/ci.yml`）

```yaml
name: CI

on:
  push:
    branches: [ main ]
  pull_request:

jobs:
  build-target:
    name: 交叉编译 (arm-none-eabi)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: 安装工具链
        run: sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi cmake ninja-build
      - name: 编译 F103 与 F407
        run: |
          cmake -B build/f103 -G Ninja -DBOARD=stm32f103_demo
          cmake --build build/f103
          cmake -B build/f407 -G Ninja -DBOARD=stm32f407_demo
          cmake --build build/f407

  test-core:
    name: core 单元测试 (host)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: 编译并运行测试
        run: |
          cmake -B build/test -G Ninja -DBOOT_BUILD_TESTS=ON
          cmake --build build/test
          ctest --test-dir build/test --output-on-failure
```

这段配置的价值：**每次 push 都自动验证"别人的机器上能不能编译通过"**，这是开源项目最基本的质量门槛。

---

## 9. 版本发布：Tag 与 Release

### 打 tag（对应 `00-project-plan.md` 里的里程碑）

```powershell
git tag -a v0.1.0 -m "首个可运行版本：F103 双向跳转"
git push origin v0.1.0
```

### 在 GitHub 上建 Release

1. 仓库页右侧 **Releases → Draft a new release**；
2. 选择刚推的 tag；
3. 写清这一版的变更（可从 CHANGELOG 复制）；
4. **附上验收证据**：升级成功的截图/日志，断电测试视频。
5. 可以附加编译好的 `.pkg` 示例固件（这是 GitHub 允许的大文件存放方式，不要直接提交进仓库）。

**每个里程碑都发一个 Release**。这是项目"活着"的最直观信号，也方便别人引用具体版本。

---

## 10. 常见错误清单

| # | 错误 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 用 `git add .` 无脑全加 | 提交构建产物、密钥 | 按文件名逐个 add |
| 2 | 先提交再建 `.gitignore` | 垃圾进历史，难清理 | **首次提交前就建好** |
| 3 | commit 消息写"更新"、"修改" | 历史无意义 | 用 Conventional Commits |
| 4 | 一个 commit 混多个不相关的改动 | 无法回滚单个功能 | 拆成多个 commit |
| 5 | 在 `main` 上直接做大改动 | 中途失败时仓库不可用 | 开 feature 分支 |
| 6 | `git push --force` 到 `main` | 覆盖他人提交，事故 | 对 `main` 永不 force push |
| 7 | 提交 `.elf` / `.bin` 大文件 | 仓库体积爆炸 | 用 Release 附件 |
| 8 | 提交密钥、私钥、token | 安全事故，且历史永久留存 | 用 `.gitignore` + 环境变量 |
| 9 | 忘记配 `user.email` | commit 不关联账号 | 第 1.2 节配置 |
| 10 | 未配 `core.autocrlf` | 满屏假改动 | 第 1.2 节配置 |

### 出错了怎么救（保命三条）

```powershell
git status                          # 先看状态，永远先看状态！

# 想撤销某次 commit 但保留改动
git reset --soft HEAD~1

# 想丢弃工作区某个文件的改动（会丢内容，确认后再执行）
git restore core/boot_fsm.c

# 提交到错误分支了：撤销 commit，切到正确分支重新提交
git reset --soft HEAD~1
git stash
git switch 正确的分支
git stash pop
git commit -m "..."
```

**记住一条**：`git status` 几乎总是会告诉你下一步该干什么。

---

## 11. 项目专属建议

### 11.1 目录命名（已调整）

原路径下的两个目录 `1_Code` / `2_解释` 已重命名为 `code` / `docs`，仓库内目录名全部改为英文：

| 原名称 | 新名称 | 原因 |
|---|---|---|
| `1_Code` | `code` | 英文目录名，且不再依赖数字前缀排序 |
| `2_解释` | `docs` | 英文目录名，符合开源惯例 |

文档文件名也一并改为英文：`00-project-plan.md`、`01-architecture.md`、`02-git-and-github.md`。**内容仍为中文**——对国内读者友好，同时避免工具链处理非 ASCII 文件名时出现问题。

**仍存在的一个遗留问题**：仓库所在的父路径 `f:\A_Git\2_OTA&Boot` 中含 `&` 字符。影响：

- PowerShell 中引用该路径时必须加引号；
- 少数构建脚本 / CI 环境可能因未转义而报错；
- **不影响 Git 本身**，也不影响推送到 GitHub（GitHub 只关心仓库内部结构）。

如果后续觉得麻烦，可以把整个目录改名为 `f:\A_Git\cortex-m-ota-boot`。重命名后本地仓库与远程的关联不受影响，因为远程地址存在 `.git/config` 里，与本地路径无关。

完整的仓库内部结构见 `01-architecture.md` 第 3 节。

### 11.2 本项目的 Git 操作时间线

| 阶段 | Git 动作 |
|---|---|
| 已完成 | 重命名目录；建 `.gitignore` / `LICENSE` / `README.md` / `CHANGELOG.md` / CI；建 `code/` 仓库骨架 |
| 下一步 | 配置 Git 身份 → `git init` → 首次提交 |
| 建远程仓库 | GitHub 建空仓库 → 配 SSH → `git remote add` → `push` |
| v0.1.0 开发 | 开 `feature/minimal-boot` 分支；每完成一个功能点提交一次；CI 跑通后合并 |
| v0.1.0 发布 | 打 `v0.1.0` tag → 建 Release → 附验收证据 |
| 之后每个版本 | 重复"分支 → 提交 → 合并 → tag → Release"，并更新 `CHANGELOG.md` |

### 11.3 学习资源

| 资源 | 说明 |
|---|---|
| 《Pro Git》（中文版免费） | https://git-scm.com/book/zh/v2 —— 官方书，第 1–3 章足够 |
| GitHub Skills | https://skills.github.com/ —— 交互式练习 |
| Conventional Commits | https://www.conventionalcommits.org/zh-hans/ —— 提交规范 |

---

## 12. 初始化执行记录（已完成）

以下操作已在本机执行完毕，记录备查。换新机器时可按 12.1 重放。

| 步骤 | 结果 |
|---|---|
| 让 `git` 可用 | winget 安装被提权拦截（报告成功但未生效），改用已有的 `D:\Git\Git`（2.40.1），其 `cmd` 目录已写入用户级 PATH |
| 身份配置 | `user.name=1duck-duck1` · `user.email=2382085108@qq.com` |
| 其他配置 | `core.autocrlf=true` · `core.quotepath=false` · `init.defaultBranch=main` |
| 清理失效代理 | 删除 `http.proxy` / `https.proxy`（原指向未运行的 `127.0.0.1:1080`），删除后直连 GitHub 实测通过 |
| SSH 密钥 | `ed25519`，`~/.ssh/id_ed25519`，公钥已添加到 GitHub |
| 首次提交 | `7113c0e` chore: 初始化仓库，添加项目文档、许可证与 CI 配置 |
| 远程 | `origin` = `git@github.com:1duck-duck1/cortex-m-ota-boot.git` |
| 推送 | `git push -u origin main` 成功，`main` 已跟踪 `origin/main` |

> [!WARNING]
> `winget install Git.Git` 在本机会报告「已成功安装」，但实际未写入磁盘与注册表 —— 提权环节被静默拦截。若要安装新版 Git，必须手动下载安装包并右键「以管理员身份运行」，装完务必用 `git --version` 验证版本号。

### 12.1 换新机器时的重放命令

```powershell
# 1) 配置身份与行为
git config --global user.name "1duck-duck1"
git config --global user.email "2382085108@qq.com"
git config --global core.autocrlf true
git config --global core.quotepath false
git config --global init.defaultBranch main

# 2) 生成 SSH 密钥并添加到 GitHub
ssh-keygen -t ed25519 -C "2382085108@qq.com"
# 把 ~/.ssh/id_ed25519.pub 的内容贴到 https://github.com/settings/keys
ssh -T git@github.com          # 出现 Hi 用户名! 即成功

# 3) 克隆
git clone git@github.com:1duck-duck1/cortex-m-ota-boot.git
```

---

## 13. 文档写作规范（Obsidian 兼容）

`docs/` 下的文档同时服务两个读者：**本地 Obsidian 知识库** 与 **GitHub 网页**。两者的 Markdown 支持范围并不完全重叠，因此约定只使用**两端都能正确渲染的语法子集**。

### 13.1 允许使用的语法

| 语法 | Obsidian | GitHub | 说明 |
|---|:---:|:---:|---|
| 标题 / 表格 / 代码块 / 任务列表 | 支持 | 支持 | 基础语法 |
| callout：`NOTE` `TIP` `IMPORTANT` `WARNING` `CAUTION` | 支持 | 支持 | **仅这 5 种两端通用** |
| Mermaid 代码块 | 支持 | 支持 | 架构图、流程图、状态机一律用它 |
| YAML frontmatter | 支持 | 渲染为表格 | 可接受 |
| 标准相对链接 | 支持 | 支持 | 见 13.3 |
| 折叠块 `<details>` | 支持 | 支持 | 用于放次要内容 |

### 13.2 禁止使用的语法

| 语法 | 原因 |
|---|---|
| `[[wikilink]]` | GitHub 不识别，会显示成字面文本 |
| `[!info]` `[!todo]` `[!success]` `[!danger]` `[!bug]` `[!example]` `[!quote]` | Obsidian 独有，GitHub 上退化为字面文本 |
| ASCII 艺术图 | 跨平台编码易损坏，窄屏下错乱，改用 Mermaid |
| 绝对路径链接 | 换机器即失效 |

> [!CAUTION]
> ASCII 框图是实测踩过的坑：本项目的架构图最初以 ASCII 绘制，写入文件时框线字符大量丢失，文档直接不可读。所有图形此后统一改用 Mermaid。

### 13.3 让 Obsidian 使用标准链接

Obsidian 默认生成 `[[wikilink]]`，需要改掉：

**设置 → 文件与链接 → 新链接格式 → 选择「相对路径」**

改完之后，Obsidian 生成和识别的都是标准 Markdown 链接，GitHub 上也能点击，两边统一。文档内引用一律写成 `[显示文字](01-architecture.md)`。

### 13.4 frontmatter 字段约定

```yaml
---
title: 文档标题
aliases:            # Obsidian 中用于别名搜索与 [[ 补全
  - 别名一
tags:               # Obsidian 标签树，用 / 分层
  - project/planning
status: active      # active | draft | archived
created: 2026-09-15
updated: 2026-09-16
---
```

命名约定：文件名 `NN-kebab-case.md`，`NN` 为两位序号，决定 Obsidian 侧边栏排序。当前使用：

| 文件 | 序号 | tags |
|---|:---:|---|
| `00-project-plan.md` | 00 | `project/planning` |
| `01-architecture.md` | 01 | `project/design` |
| `02-git-and-github.md` | 02 | `project/guide` |
