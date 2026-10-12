# m7r2-release —— 0.3.3 发行实况取证（R2REL）

- **包**：M7-R2REL（发布 0.3.3）｜**日期**：2026-10-12｜**机器**：开发机 Windows（win32 10.0.26100 x64）
- **授权依据**：用户明确指令「修复后发布 0.3.3」（本包允许 git push；tag 二次移动记 `M7-ADJ-tag-move-2`）
- **配套**：`docs/m7-evidence/release-runbook.md`（发布链操作手册，M7-R2-C）；本轮为该手册的执行实录。

---

## 1. 预检取证（2026-10-12 21:20–21:30，tag 移动前）

### 1.1 GitHub Release v0.3.3 不存在（零 Release）

```bash
curl -s -o /dev/null -w "%{http_code}" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
```

- 输出：`404` ⇒ 该 tag **无 GitHub Release**（发布说明/资产挂载面为空）。

### 1.2 release #13（run 38198290373 @ b1af371）无 publish 产物

```bash
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/release.yml/runs?per_page=10" …
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38198290373/jobs" …
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38198290373/artifacts" …
```

（原始 JSON 留存 `build/m7r2-rel/m7r2-rel_release_runs.json` / `m7r2-rel_run13_jobs.json` /
`m7r2-rel_run13_artifacts.json`，gitignored。）

| 读数 | 值 |
|---|---|
| run 13 | `completed / cancelled`，head_sha `b1af3716`，push 事件，2026-10-12T03:04:07Z |
| winzip | `cancelled`（03:05:53 → 03:26:09，≈20m16s 撞 `timeout-minutes: 20` 硬顶） |
| appimage | `success`（6m56s）；probe-appimage `success` |
| probe-winzip | `skipped`；**publish `skipped`** |
| run 13 artifacts | 仅 `PhotoPipeline-AppImage`（51,382,515 B）——**无 winzip 产物、无 Release 资产** |

⇒ 与 m7-report §3.8 末行记载一致：publish 未执行、GitHub Release 未创建、**tag v0.3.3 零 Release 零资产**
（run 13 的 AppImage 是 Actions 工件（90 天过期），不是 Release 附件）。

### 1.3 tag v0.3.3 当前落点 = b1af371

```bash
git ls-remote --tags origin
```

- `refs/tags/v0.3.3` = tag 对象 `cb181ee81b533717f1ac020b57b520f27e2a008a`，`^{}` 解引用 =
  `b1af3716c13b8b85d884a0de276515e483b8a8f6`（b1af371，上轮 gate-fix2）——与任务书背景逐字一致。

### 1.4 移动前前提判定

| 前提（M7-ADJ-tag-move-2 条件） | 判定 | 依据 |
|---|---|---|
| 零 Release | ✓ | §1.1 404 |
| 零资产 | ✓ | §1.2 publish skipped，Release 不存在 ⇒ 无附件；run 13 仅有 Actions 工件 AppImage（非 Release 资产，如实记录） |
| 授权 | ✓ | 用户指令「修复后发布 0.3.3」（本包任务书明文：允许 git push、授权二次移动 tag） |

### 1.5 轮询配额说明（如实）

公共 API 匿名限速 60 req/h；预检时 `X-RateLimit-Remaining: 7`、reset 21:37:24（本地）。
轮询节流取「间隔 60–180s」（任务书口径 60s 上限），既守 60 req/h 配额又不漏完成事件；
每次轮询把 job 级 JSON 落 `build/m7r2-rel/`（gitignored）留证。

---

## 2. 推送 main（M7-R2 全部提交）

- 2026-10-12 21:26（本地）`git push origin main` → `a4cf0fc..2c50bfc  main -> main`
- **HEAD = `2c50bfc86fa6b53d0c6629e75aa60d40ab2e6ecc`**（M7-R2-F2 独立校读第二轮修订）
- 推送面：11 个 M7-R2 提交（210fce5 R2C → 2c50bfc R2F2）；工作树 tracked 面零未提交改动
  （untracked 的 AGENTS.md / docs 评审件 / temp_bug.png 非本包归属，不入本次提交）。
- 触发 main-full（缓存播种：ccache-Linux- / vcpkgwin- / Qt(msvc2022_64)——Windows 侧无 ccache，
  见 runbook §1.1 查证事实；winzip job 依赖的是 Qt + vcpkgwin 暖度）。

---

## 3. 发行前本机门禁（2026-10-12 21:26–21:42，build/release 树增量重建至 HEAD 后）

构建面（如实）：`build/release`（打包面，PP_BUILD_DEV=OFF）**增量重建 202 步至 HEAD**
（此前 13:36 构建止于 R2C 期；`build/m7r2-rel/build-release-1.log`）；E-2 用 dev exe 出自本包
专属树 `build/m7r2-rel`（`release-dev` 预设，PP_BUILD_DEV=ON，`--target photopipeline`）——
**不触碰 `build/release-dev`**（波次收口门禁专用，纪律 4；与 M7-R2-C 的 release-dev 只读先例
不同路径，语义同：E-2 = dev exe 拷入 staging 后 offscreen 走查）。命令逐字：

```bash
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-rel -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-rel --target photopipeline
bash tools/ci-format.sh
python tools/env.py run -- python tools/ci-versions-check.py
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/m7r2-rel/photopipeline.exe
```

| # | 判据 | 读数 |
|---|---|---|
| 1 | `ci-format.sh` | **`violations=0`**（钉版 clang-format 23.1.1，155 文件全源 0 diff）——`build/m7r2-rel/ci-format.log` |
| 2 | `ci-versions-check.py`（R-122 单源） | **`OK (10 keys, expect=0)`** |
| 3 | `make_winzip.py` 门禁 A 闭包 | `68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37`；VC 运行时 app-local 10 个；非传递闭包点 `1 个已随包（0 个为本次显式补拷）`（jxl_threads.dll） |
| 4 | 门禁 B 来源 | `5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致`（Qt 6.11.2 msvc2022_64） |
| 5 | 断言 C 结构 | `photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 / tls/=2 齐备`；链接面 Qt6Core/Gui/Widgets/Network 全部随包 |
| 6 | 断言 D 许可 | 写盘前 `35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓`；产物内复核 35（≥30 ✓）+ NOTICE ✓ |
| 7 | 烟测 E-1 | `PhotoPipeline 0.3.3 (exit 0)`；产物内解压即用 `--version = PhotoPipeline 0.3.3 (exit 0)` |
| 8 | 烟测 E-2 | **`UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)`**（冻结行逐字） |
| 9 | 烟测状态隔离 | 走查运行态 3 项（classes.json/logs/presets）已清除、快照回原状 |
| 10 | 指纹 F（本地） | 首轮 `PhotoPipeline-0.3.3-win64.zip` 34,785,460 B / sha256 `1baf1481…` / 105 条目；复跑（rc 捕获）同字节数、sha256 `69331ab6…` / 105 条目——zip 不字节可复现为已知事实（release.yml §12.7 口径：以「tag → Release 附件」为锚） |

- **rc 读数（如实）**：首轮 `%ERRORLEVEL%` 为 cmd 语法、bash 下未展开（日志尾行失真）；
  **复跑一轮**以 `echo $?` 正确捕获 **`MAKE_WINZIP_RC=0`**（`build/m7r2-rel/winzip-gates2.log`）。
  两轮门禁读数逐条一致。

---

## 4. main-full #44（push 触发）：**红**——发行在 tag 移动前停止

### 4.1 轮询实录（run 38255712342，head_sha = 2c50bfc，push 2026-10-12T13:26:30Z）

- 轮询命令 = `build/m7r2-rel/poll-mainfull.sh`（`actions/workflows/main-full.yml/runs?head_sha=…`）；
  21:33–21:58 本地共 16 拍（其中 21:33–21:57 多拍撞共享 IP 匿名配额 60 req/h——
  `rate_limit` 读数 `remaining 0`、reset 21:57:21，为本机/共享出口他人消耗，非本包单方；
  21:58:43 配额恢复后即读到终态）。
- 终态：**`completed / failure`**（13:26:30 → 13:47:02Z，整轮 20m32s）。

### 4.2 job 级读数（`actions/runs/38255712342/jobs`）

| job | 结论 | 耗时 |
|---|---|---|
| lint（clang-format 全源 0 diff） | success | 9s |
| appimage | success | 6m39s |
| **linux（全 ctest+金样）** | **failure** | 6m38s |
| tidy（clang-tidy 棘轮） | success | 14m49s |
| audit（许可/SPDX） | success | 68s |
| **winzip** | **success** | **19m45s** |
| **windows（全 ctest+金样）** | **success** | **19m50s** |
| cache-gc | success | 9s |
| gate（总闸） | failure（随 linux） | 6s |

### 4.3 失败定位（匿名 API 可达范围内）

- linux job 失败步 = **step 13「configure + build + ctest release（全量）」**
  （13:27:48 → 13:33:11Z）；其后步（覆盖自检 / release-dev / ui-smoke / 金样 20 对）全 skipped。
- check-run 注解（`check-runs/114833200480/annotations`）：`failure | Process completed with exit code 8`；
  日志本体需鉴权（runbook §4 口径），匿名不可取。
- **退出码 8 = ctest 的失败测试数**（本机反证：`ctest -R no_such_test_zzz` ⇒ `No tests were found!!!`
  且 **rc=0**，排除「无测试」假说；cmake/ninja 失败均为 rc=1）⇒ **Linux 侧构建成功，`ctest --preset
  release`（34 条）中 8 条失败**。
- **二分归因（同日同 runner）**：main-full **#41 @ b1af371**（M7 全树、R2 波次之前）
  「linux（全 ctest+金样）」**success**（9m37s，含金样 20 对）；**#44 @ 2c50bfc 红** ⇒
  8 条失败为 **M7-R2 波次（b1af371..2c50bfc）引入的 Linux 侧回归**，非既有已登记债
  （R2F-C3 登记的是 Linux **回归基线**未移植，管不到 ctest）。
- 候选面（未能实证，如实标注为推断）：R2 测试增量 +1840 行 / 11 文件
  （`test_color`+777、`test_decode`+274、`test_params`+196、`test_presets`+185、`test_fsops`+87…）；
  Windows 侧同套件全绿（#44 windows job）⇒ 属平台敏感断言或平台敏感行为。**逐条测试名需
  Linux 宿主或鉴权日志**，本机（Windows、无 WSL）无法复现与定位。

### 4.4 缓存播种面（ask 的步骤 3 目的）：已达成

winzip job 19m45s / windows job 19m50s / tidy 14m49s 全部 **success 且完整走完 save 步**
（`vcpkg binary cache（save）success`；windows 侧 Qt(msvc2022_64)/vcpkgwin- 播种）——
release.yml winzip job 所需的暖度来源已就绪（Windows 侧无 ccache，见 runbook §1.1）。
linux job 的 Qt(ccache-Linux) save 因 step 13 失败被 skip（影响 appimage 侧，不影响 winzip）。

### 4.5 处置：停止发布链（ask 流程 3 的明文分支）

ask 流程 3：「若 main-full 红：修复推送（允许）或停止上报」。本包评估：

- **修复推送不可行**：8 条 Linux 测试失败的修复须改 `src/**`/`tests/**`——**独占文件面外**
  （本包任务书：「不得改 src/ 与 tools/（门禁已绿才走到这里）」+ 纪律 3「绝不顺手改」）；
- ⇒ **停止上报**：tag `v0.3.3` **未移动**（仍指 b1af371，零 Release 零资产状态保持不变，
  与 §1 前提取证一致）；release.yml 未触发；GitHub Release 未创建。
- 备选（未执行，留主对话裁决）：① 先派修复包定位/修复 8 条 Linux 失败 → main-full 绿后重跑本包；
  ② 空提交重触发 main-full 复核是否瞬态（本包判定 8 条测试失败非瞬态形态，未采用）。

---

## 5. tag v0.3.3 二次移动（M7-ADJ-tag-move-2）—— **未执行**

- 授权在案（用户指令「修复后发布 0.3.3」+ 任务书明文），但**前提未满足**（流程 3 的 main-full 绿）。
- 截至本文件收口：`git ls-remote --tags origin` 复核 `refs/tags/v0.3.3` = `cb181ee…`，`^{}` =
  `b1af371…`（未动，同 §1.3）。裁定登记见 `docs/m7-adjudications.md` 发行裁定节（M7-ADJ-tag-move-2）。

---

## 6. release.yml 实况（tag 推送后）—— **未触发**

无 tag 推送 ⇒ 无 release.yml 轮次；无 Release 资产可核对（预检 404 状态保持）。

---

## 7. 复现命令索引（逐字）

```bash
curl -s -o /dev/null -w "%{http_code}" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38198290373/jobs"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38255712342/jobs?per_page=30"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38197935670/jobs?per_page=30"   # #41 b1af371 二分对照
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/check-runs/114833200480/annotations"
git ls-remote --tags origin
bash tools/ci-format.sh
python tools/env.py run -- python tools/ci-versions-check.py
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-rel -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-rel --target photopipeline
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/m7r2-rel/photopipeline.exe
python tools/env.py run -- ctest --test-dir build/m7r2-rel -R no_such_test_zzz   # ctest rc 语义反证
```

原始 JSON / 日志（gitignored）：`build/m7r2-rel/`（`m7r2-rel_release_runs.json`、`m7r2-rel_run13_jobs.json`、
`m7r2-rel_run13_artifacts.json`、`mainfull-run.json`、`mainfull44-jobs.json`、`mainfull41-jobs.json`、
`mainfull40-jobs.json`、`mainfull-history.json`、`warmcache-runs.json`、`checkruns.json`、
`build-release-1.log`、`build-dev.log`、`ci-format.log`、`winzip-gates.log`、`winzip-gates2.log`、
`poll-mainfull.log`、`ctest-probe.log`）。

---

*本文件收口于发行停止决策（2026-10-12）；每节命令逐字可复跑。*
