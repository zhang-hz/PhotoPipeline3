# m7r2-c-ci-budget-timings — 发布链/主干道预算实况取证

- **包**：M7-R2-C｜**日期**：2026-10-12｜**取证通道**：GitHub 公共 API（免鉴权；仓库 github.com/zhang-hz/PhotoPipeline3）
- **原始输出**：会话内 API 调用（命令逐字见 `docs/m7-evidence/release-runbook.md` §4，可复跑复核）。

## 1. warm-cache 轮次序列（播种通道生死线）

| # | run id | 提交 | 结论 | created → updated | 判读 |
|---|---|---|---|---|---|
| 8 | 38130289297 | f661a1fe | success | 10-11 09:58:49 → 10:25:43 | 播种成功（18fb355 之前） |
| 9 | 38197048625 | 97cd9da | failure | 10-12 02:45:36 → 02:45:36 | **解析期秒败**（0 job） |
| 10 | 38197048749 | 97cd9da | failure | 10-12 02:45:36 → 02:45:36 | 同上 |
| 11 | 38197935004 | b1af371 | failure | 10-12 02:58:59 → 02:58:59 | 同上 |
| 12 | 38198289487 | b1af371 | failure | 10-12 03:04:06 → 03:04:06 | 同上 |
| 13 | 38200391845 | a4cf0fc | failure | 10-12 03:36:05 → 03:36:05 | 同上 |

`created_at == updated_at` 且 jobs 空 = workflow YAML 被校验拒绝、一步没跑。根因（本包已修）：
M7-WP3（18fb355）R-063 删 job 级 `QT_DIR:` 值行时留下**空 `env:` 键**（`warm-cache.yml` 原 247 行），
YAML 上 `env: null` ⇒ GitHub「Line 247 Unexpected value」。修复 = 删空键、归因注释保留
（QT_DIR 由 checkout 后「Qt 路径」步骤经 GITHUB_ENV 注入，机制不变）。修复后本地校验：
`yaml.safe_load` 全 5 文件 PASS + 严格面（空壳映射键/缩进 TAB/重复键）全 PASS +
`tools/ci-versions-check.py` OK(10 keys)。

## 2. release.yml（发布链）job 耗时

| run | 提交 | 结论 | winzip job | appimage job | 备注 |
|---|---|---|---|---|---|
| #10（0.3.1） | 50317d0e… | success | 17m43s | 3m13s | 距 20min 硬顶 ~2min |
| #11（0.3.2） | f661a1fe | success | 17m39s | 4m39s | 同上 |
| #12 | 97cd9da | failure | —（appimage 6s 红，R-122 版本断言） | — | m7-report §3.8 |
| #13 | b1af371 | **cancelled** | **20m16s 撞顶** | 6m56s 绿 | step 12「configure+build」19m11s 未完；probe-winzip/publish 全 skipped ⇒ Release 未创建（m7-report §3.8 步骤级取证） |

## 3. main-full Windows job 群（同轮对照）

| run | 提交 | windows | winzip | tidy | linux | 关键步骤读数 |
|---|---|---|---|---|---|---|
| #37 | d049d22（10-10） | 17m11s ✓ | 14m53s ✓ | 8m40s ✓ | 6m06s ✓ | tidy **configure-only 43s**；winzip configure+build 9m14s |
| #38 | f661a1fe（10-11） | 15m30s ✓ | 18m16s ✓ | 红（棘轮 302>283） | 8m56s ✓ | configure 仍快 |
| #39 | f661a1fe（10-11） | 18m32s ✓ | 17m58s ✓ | 红（棘轮） | 6m11s ✓ | 余量已 ~1.5min |
| #41 | b1af371（10-12） | **20m10s 取消** | **20m08s 取消** | **20m07s 取消** | 9m37s ✓ | 三 Windows job 同形态撞顶 |
| #42 | a4cf0fc（10-12） | **20m09s 取消** | **20m10s 取消** | **20m06s 取消** | 8m52s ✓ | tidy **configure-only 19m02s 未完**（43s → >19min） |

判读要点：#42 的 tidy job 卡死在「configure release-dev + 导出 compile_commands.json（**不起编译**）」
一步 19 分钟——瓶颈**不是**编译，是 configure 期的 vcpkg manifest install；同时暖度断言（≥20 档）
全绿 ⇒ **计数型暖度断言的假绿实证**（档数够、ABI 不命中 ⇒ 源码重建）。Linux job 同轮全绿
（linux 8m52s / appimage 4m21s）⇒ 问题限于 Windows 侧 vcpkg 缓存有效性。

## 4. ccache save/restore 链核查（任务书逐项）

- **Linux**：key `ccache-${{ runner.os }}-${{ hashFiles('CMakeLists.txt','src/**','CMakePresets.json') }}-${{ hashFiles('cmake/**','triplets/**') }}-${{ github.run_id }}`、
  `restore-keys: ccache-${{ runner.os }}-` —— 写者 main-full `linux`（main-full.yml:481-487）与
  warm-cache `warm`（warm-cache.yml:398-406）；读者 main-full linux/appimage、nightly、pr-fast、
  release appimage（release.yml:149-155）逐字同键 ⇒ **对称完整，无需改动**。
- **Windows**：main-full 的 windows/winzip/tidy 与 release 的 winzip **均无** ccache 步；
  且 `CMakeLists.txt:126`/`CMakeLists.txt:136-144`（R-124）launcher 只挂 GNU|Clang，MSVC 构建
  **不产生** ccache 条目 ⇒ 「main-full windows save → release winzip restore 命中」在现架构下
  不存在可播/可收的内容。补空 save/restore = 伪命中（restore 绿、编译零收益），
  **本包裁定不补**（handoff「裁定登记」；引擎面解冻重议属跨界需求）。

## 5. 归因结论（如实分级）

1. **确证**：warm-cache 自 18fb355 起 YAML 非法 ⇒ 播种断供（§1 实证）；已修。
2. **确证**：Windows 侧 configure 期 vcpkg install 从 43s 膨胀到 >19min（§3 实证），且计数型
   暖度断言在此期间全绿（假绿缺口：缓存文件名纯 sha256，无法按名断言端口/ABI）。
3. **疑似（未取到 CI 日志逐字，logs API 需鉴权）**：vcpkg 二进制缓存 ABI 失配（runner 工具链
   镜像更新或缓存驱逐）⇒ 源码重建端口。本地对照：同一 manifest 在开发机 warm
   （`build/m7-wp13-build-rel.log`：vcpkg install 只列 4 个 host 工具即 done）⇒ manifest 变更
   本身不动端口 ABI 的可能性高。验证法：重跑 warm-cache 后看 §3 表的 configure 耗时回落到秒级。
4. **处置**：本包交付 = ①修复播种通道（warm-cache.yml）②发布链操作手册（前置条件/即停纪律）
   ③本取证。CI 实跑效果由 R2REL 按 runbook 验证。
