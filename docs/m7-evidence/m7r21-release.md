# m7r21-release —— 0.3.3 发行实况取证（M7-R2.1-F7）

- **包**：M7-R2.1-F7（发布 0.3.3）｜**日期**：2026-10-13｜**机器**：开发机 Windows（win32 10.0.26100 x64）
- **授权依据**：用户明确指令「修复后发布 0.3.3」（本包允许 git push；tag 二次移动 =
  `M7-ADJ-tag-move-2` 授权的正式消耗，条件同首次：预检零 Release 零资产 / 原子操作 +
  `ls-remote` 核对 / 移动后不得再动）。
- **配套**：`docs/m7-evidence/release-runbook.md`（发布链操作手册，M7-R2-C）；前情实录
  `docs/m7-evidence/m7r2-release.md`（R2REL，2026-10-12，发行暂停于 main-full #44 linux ctest 8 条红）。
  本轮为该手册执行实录第二班：F1 已修 Linux 侧 8 条 ctest 失败，F1–F6 全部门禁绿后重走发行链。
- **终态一句话**：预检 ✓ → push ✓ → 本机复核 ✓ → **main-full #48 红（linux job 唯一失败 =
  `ui_smoke` 一条）** ⇒ 流程 3 前提「linux job 必须绿」未达成 ⇒ **停止在 tag 移动之前**：
  tag 未动（b1af371）、release.yml 未触发、GitHub Release 未创建；`M7-ADJ-tag-move-2` 授权
  **保持未消耗**。

---

## 1. 预检取证（2026-10-13 04:5x–05:01 本地，tag 移动前）

### 1.1 GitHub Release v0.3.3 不存在（零 Release）

```bash
curl -s -o build/m7r21-f7/rel_check.json -D build/m7r21-f7/rel_check.headers \
     -w "%{http_code}" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
```

- 输出：`404`（2026-10-13 05:01 本地 / 2026-10-12T21:01Z）⇒ 该 tag **无 GitHub Release**
  （发布说明/资产挂载面为空 ⇒ **零资产**）。原始 JSON/响应头留 `build/m7r21-f7/`（gitignored）。
- 配额读数（响应头逐字）：`X-RateLimit-Limit: 60`、`X-RateLimit-Remaining: 29`、
  `X-RateLimit-Reset` = 本地 2026-10-13 05:17:20。

### 1.2 releases 列表核对（零 Release 的旁证）

```bash
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases?per_page=5" …
```

| tag | published_at | assets |
|---|---|---|
| v0.3.2 | 2026-10-11T02:29:36Z | 4 |
| v0.3.1 | 2026-10-10T11:46:49Z | 4 |
| v0.3.0 | 2026-10-10T04:58:22Z | 4 |

⇒ 列表止于 v0.3.2，**无 v0.3.3 条目**（与 §1.1 的 404 互证）。前轮 release #13
（run 38198290373 @ b1af371）publish skipped、零 Release 资产的实录见
`m7r2-release.md` §1.2（Actions 工件 AppImage 不是 Release 附件）。

### 1.3 tag v0.3.3 当前落点 = b1af371（未动）

```bash
git ls-remote --tags origin
```

- `refs/tags/v0.3.3` = tag 对象 `cb181ee81b533717f1ac020b57b520f27e2a008a`，`^{}` 解引用 =
  `b1af3716c13b8b85d884a0de276515e483b8a8f6`（b1af371，gate-fix2）——与
  `M7-ADJ-tag-move-2` 预检记录逐字一致，自 2026-10-12 首次移动后未再动。

### 1.4 移动前前提判定（M7-ADJ-tag-move-2 条件）

| 前提 | 判定 | 依据 |
|---|---|---|
| 零 Release | ✓ | §1.1 404 + §1.2 列表无 v0.3.3 |
| 零资产 | ✓ | Release 不存在 ⇒ 无附件（run 13 仅有 Actions 工件，非 Release 资产） |
| tag 落点 b1af371 | ✓ | §1.3 `ls-remote` 逐字 |
| 授权 | ✓ | 用户指令「修复后发布 0.3.3」+ 任务书明文「本包允许 git push、授权二次移动」 |
| 前置「main-full 绿」 | **✗（本轮 §3 判定）** | R2REL 的 8 条 Linux ctest 失败确由 F1 修复（#47 linux 绿），但 `0555947..e1017fe` 又引入 `ui_smoke` 一条失败（#48），前提未达成 |

---

## 2. 推送 main（M7-R2.1 F1–F6 + GATE 全部提交）

- 2026-10-13 05:02 本地（2026-10-12T21:02Z）`git push origin main` → `0555947..e1017fe  main -> main`
  （RC=0）。
- **HEAD = `e1017fe601c8bb342699189bafce7e7772b05e51`**（M7-R2.1-GATE: 终门禁收口 + 证据）。
- 推送面：27 个提交（`60742b0` R2.1-F3 首单 → `e1017fe` GATE）；工作树 tracked 面零未提交改动
  （untracked 的 `AGENTS.md` 非本包归属，不入提交）。触发 **main-full #48**（run 38308098290），
  同轮播种 ccache-Linux / vcpkgwin / Qt(msvc2022_64) 暖度（Windows 侧无 ccache，runbook §1.1）。
- 前情二分（推送前读数，供 #48 判读对照）：main-full **#47 @ 0555947**（F1 收口）——
  **linux（全 ctest+金样）success**（R2REL 的 8 条 Linux ctest 失败已由 F1 修复），红点在
  **tidy 棘轮 step 12 failure**（总闸随红）；该红点由 F4 的 tidy 基线重播种收口
  （`81d0b34`，exif_editor 2→3、总量 264→263，GATE 本机读数 `TIDY total=263 baseline=263
  violations=0`，见 `m7r21-gate.md` §5）。#46 @ 3339492c 为 F1 波次中间提交，红型同源。

---

## 3. main-full #48（push 触发）：**红**——发行在 tag 移动前停止

### 3.1 轮询实录（run 38308098290，head_sha = e1017fe6，push 2026-10-12T21:02:24Z）

- 轮询命令（公共 API 免鉴权，间隔 ≥5min，见 §8）：`workflows/main-full.yml/runs?per_page=3`
  → `actions/runs/38308098290` → `actions/runs/38308098290/jobs?per_page=20`；
  21:02 首拍 → 21:07 / 21:12 / 21:17（run 级 in_progress）→ 21:26 拍到 job 级全完成 + run 级
  终态 **`completed / failure`**（21:02:24 → 21:23:01Z，整轮 20m37s，40min 上限内到达终态，
  无需继续等待）。

### 3.2 job 级读数（`actions/runs/38308098290/jobs`）

| job | 结论 | 耗时 |
|---|---|---|
| lint（clang-format 全源 0 diff） | success | 8s |
| appimage | success | 7m00s |
| **linux（全 ctest+金样）** | **failure** | **11m20s** |
| tidy（clang-tidy 棘轮） | success | 18m03s |
| audit（许可/SPDX） | success | 72s |
| winzip | success | 19m59s |
| windows（全 ctest+金样） | success | 17m03s |
| cache-gc | success | 6s |
| gate（总闸） | failure（随 linux） | 7s |

### 3.3 失败定位（匿名 API 可达的最深处）

- linux job 失败步 = **step 15「configure + build + ctest release-dev（含 ui_smoke）」**
  （21:09:21 → 21:13:52Z）；**step 13「configure + build + ctest release（全量）」success**
  （21:03:41 → 21:09:20Z）⇒ release 树 ctest 全过、红在 release-dev 树（35 条中的 1 条）。
  其后 step 17（UI-SMOKE 冻结行取证）/ 18（金样 20 对）/ 19（冻结行断言）全 skipped。
- step 16（F1 失败注解遥测二版，M7R21F1-1b）success，check-run 注解
  （`check-runs/115007008009/annotations`）逐字：

  ```
  CTEST-FAIL ui_smoke (无 FAIL 断言行；明细见 LastTest.log)
  CTEST-FAIL-SUMMARY 1 tests: ui_smoke
  Process completed with exit code 8.
  ```

  ⇒ **失败测试恰 1 条 = `ui_smoke`**（exit 8 = ctest 失败测试数，口径同 `m7r2-release.md` §4.3）。
- **「无 FAIL 断言行」的语义（查证，勿过度解读）**：遥测提取式为 `FAIL (\S+): (.*)`
  （`.github/workflows/main-full.yml:472` 一带），而 `tests/ui_smoke.py` 的失败行形态是
  `ui_smoke: FAIL (…)`（`tests/ui_smoke.py:510/514/519/…`）——**反序不匹配**该式 ⇒ 任何
  ui_smoke 失败都会落进「无 FAIL 断言行」兜底，**不能**据此推断崩溃/超时/断言失败形态。
  断言级明细在 `build/release-dev/Testing/Temporary/LastTest.log`（job 日志
  `actions/jobs/115007008009/logs` 匿名 **403** `Must have admin rights`，2026-10-13 实测；
  Step Summary 亦只含上述兜底行）⇒ **本机（Windows、无 WSL、无鉴权）不可达**，如实登记。
- **二分归因**：main-full **#47 @ 0555947** 同 job「linux（全 ctest+金样）」**success**
  （15:05–15:14Z，含 release-dev ctest + ui_smoke）⇒ 该失败由 **`0555947..e1017fe`
  （R2.1 F2–F4 + GATE）引入**。变化面（本地 `git diff --stat 0555947..e1017fe`）：
  `tests/ui_smoke.py` 与 `.github/` **零改动**；`src/ui/` 大改（`smoke_probes.cpp` +889、
  `page_meta`/`page_output` 拆 TU、`meta_*` 新 TU、`mainwindow.cpp` 25±）。候选根因
  （**推断，未实证**）：F4 第 4 波常驻探针（R-148/149 · R-143 · R-152/153 · UX-12 像素档）的
  平台敏感判据（Linux offscreen 渲染/字体与 Windows 不同），或 F2/F3 几何改动（R-218 RTL 续 /
  页面拆分）在 Linux 下的自绘几何值漂移。Windows 侧同树全绿（GATE ctest 35/35 + ui_smoke
  冻结行，`m7r21-gate.md` §3）。逐条断言定位需 Linux 宿主或鉴权日志（边界同 R2REL §4.3）。

### 3.4 缓存播种面（流程 3 的第二个目的）：达成（一处如实注记）

- winzip 19m59s / windows 17m03s / tidy 18m03s 全绿且 vcpkg save 步完整（vcpkgwin- /
  Qt(msvc2022_64) 播种）；linux 侧 vcpkg save（step 22）与 ccache save（step 23）success。
- **Qt(gcc_64) save（step 21）skipped**（linux job 失败所致，形态同 R2REL §4.4）——只影响
  appimage 侧暖度，不影响 winzip。
- ⇒ ui_smoke 修复后重走发行链时，winzip 的 20min 硬顶有暖度支撑（#48 winzip 19m59s 亦为
  暖度实证，余量 ~0min 见 runbook §3 的预算口径）。

---

## 4. 发行前本机复核（2026-10-13 05:02–05:1x；与 §3 轮询并行执行，两判据都绿才可进 tag 移动）

构建面（如实）：打包面 `build/release` **增量重建 226 步至 HEAD**（此前止于 R2REL 期
`2c50bfc`；`build/m7r21-f7/build-release-1.log`，exit 0）；E-2 的 dev exe 取
**`build/release-dev/photopipeline.exe` 只读**（GATE 门禁树，2026-10-13 03:55 构建；GATE 复跑
`ninja: no work to do.` 已核对与 HEAD 树一致）。make_winzip 的 E-2 是 `shutil.copy2` 拷入
staging 后运行（`tools/make_winzip.py:688`），**对 build/release-dev 零写入**（纪律 4「不动
build/release-dev」成立；与 R2REL 自建 dev 树做法殊途同归，本轮取 runbook §5-4 的标准读法）。

```bash
bash tools/ci-format.sh
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
```

| # | 判据 | 读数 |
|---|---|---|
| 1 | `ci-format.sh` | **`violations=0`**（钉版 clang-format 23.1.1，**187 文件**全源 0 diff，RC=0）——`build/m7r21-f7/ci-format.log` |
| 2 | 门禁 A（闭包） | `68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37`；VC 运行时 app-local 10 个 |
| 3 | 门禁 A′（非传递闭包点） | `1 个已随包（0 个为本次显式补拷）`（jxl_threads.dll） |
| 4 | 门禁 B（来源） | `5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致`（Qt 6.11.2 msvc2022_64） |
| 5 | 断言 C（结构） | `photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备`；Qt 链接面 Core/Gui/Widgets/Network 全随包 |
| 6 | 断言 D（许可） | 写盘前 `35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓`；产物内复核 35（≥30 ✓） |
| 7 | 烟测 E-1 | `PhotoPipeline 0.3.3 (exit 0)`；产物内解压即用 `--version` 同（exit 0） |
| 8 | 烟测 E-2 | **`UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)`**（冻结行逐字） |
| 9 | 烟测状态隔离 | staging 快照还原断言过（无残留路径） |
| 10 | 指纹 F（本机） | `PhotoPipeline-0.3.3-win64.zip` **34,797,611 B** / sha256 `ce16b57fe3f384a329e7e09a4ed11d708a737b27a04e7e57766076c926bb1405` / 105 条目（zip 不承诺字节可复现，发行指纹以 Release 附件为准——本轮未发行，留待真发布后 `d049d22` 先例收口） |

- **rc 读数**：`make_winzip.py` 退出码 **0**（bash `echo $?` 捕获，`build/m7r21-f7/winzip-gates1.log`）。
- 流程 4 只要求 ci-format + make_winzip 两项，本轮实跑即此两项（ci-versions-check / 金样 /
  回归等由 F6 GATE 在同一棵树上全量背书，见 `m7r21-gate.md`；本包未复跑、不重复记过）。

---

## 5. tag v0.3.3 二次移动（M7-ADJ-tag-move-2）—— **未执行（授权保持未消耗）**

- 流程 5 的前置（流程 3「main-full 绿、linux job 必须绿」）**未满足**：#48 终态 failure
  （linux `ui_smoke`，§3）⇒ 按纪律 9「判据不可达成如实上报，不绕过、不造假、不放宽判据」
  与 §3.9 先例，**停止在 tag 移动之前**。修复须改 `src/ui/`（探针/几何）或 `tests/ui_smoke.py`
  = 本包独占文件面外（任务书「不改 src/ 与 tools/」+ 纪律 3「绝不顺手改」）。
- 收口复核（2026-10-13 05:3x 本地，停止决策后）：`git ls-remote --tags origin` 逐字

  ```
  cb181ee81b533717f1ac020b57b520f27e2a008a  refs/tags/v0.3.3
  b1af3716c13b8b85d884a0de276515e483b8a8f6  refs/tags/v0.3.3^{}
  ```

  ⇒ tag **未动**（同 §1.3）。`M7-ADJ-tag-move-2` 授权**未消耗**，留待 ui_smoke 修复、
  main-full 全绿后由后续发行包按同一条件执行（预检零 Release 零资产 / 原子 +
  `ls-remote` 核对 / 此后不得再动）。
- 未采用的备选（如实登记，供主对话裁决）：① 空提交重触发 main-full 复核是否瞬态——不采用：
  取证更支持确定性回归（#47 绿 → #48 红与 `0555947..e1017fe` 的 src/ui 大改同步），且空提交
  污染历史；② 本包跨界修 ui_smoke——纪律 3 明文禁止。

## 6. release.yml 实况（tag 推送后）—— **未触发**

- 无 tag 推送 ⇒ 无 release.yml 新轮次（`actions/workflows/release.yml/runs?per_page=3` 复核：
  最新仍为 **#13 run 38198290373 @ b1af3716 completed/cancelled**，其后无新 run）。
- GitHub Release v0.3.3 **仍未创建**：收口复检 `/releases/tags/v0.3.3` = **404**（05:3x 本地）。

## 7. 处置与跨界需求

- **跨界需求（交修复包，不在本包文件面）**：Linux 侧 `ui_smoke` 一条失败的定位与修复——
  排查入口：Linux 宿主（或 CI 日志权限）跑 `QT_QPA_PLATFORM=offscreen python3
  tests/ui_smoke.py build/release-dev` 取失败探针段 / `LastTest.log`；候选面见 §3.3（F4 第 4 波
  探针、F2/F3 几何改动，推断未实证）；二分锚点 = main-full #47 @ `0555947` 绿 / #48 @
  `e1017fe` 红。修复合入且 main-full 全绿后，发行包按 `M7-ADJ-tag-move-2` 条件重走本流程
  （步骤 1–7），指纹/「已发行」终态照 `d049d22` 先例在真发布后收口。
- 本包已交付（不受阻部分）：预检取证（§1）、main 推送与 HEAD 记录（§2）、main-full 轮询与
  失败定位（§3）、发行前本机复核全绿（§4）、停止决策与终态核对（§5–§6）。

## 8. 复现命令索引（逐字）

```bash
curl -s -o build/m7r21-f7/rel_check.json -w "%{http_code}" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases?per_page=5"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/main-full.yml/runs?per_page=3"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38308098290"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38308098290/jobs?per_page=20"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/commits/e1017fe601c8bb342699189bafce7e7772b05e51/check-runs?per_page=30"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/check-runs/115007008009/annotations"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/release.yml/runs?per_page=3"
git ls-remote --tags origin
git push origin main
bash tools/ci-format.sh
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
```

原始 JSON / 日志（gitignored）：`build/m7r21-f7/`（`rel_check.json`、`rel_check.headers`、
`releases-list.json`、`mainfull-runs.json`、`mainfull47-jobs.json`、`mainfull48-run.json`、
`mainfull48-jobs.json`、`checkruns48.json`、`linux48-annotations.json`、`release-runs.json`、
`build-release-1.log`、`ci-format.log`、`winzip-gates1.log`）。

---

*本文件收口于发行停止决策（2026-10-13）；每节命令逐字可复跑。*
