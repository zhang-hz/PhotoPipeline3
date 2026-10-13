# m7r22-release —— 0.3.3 发行实况取证（M7-R2.2-G6）

- **包**：M7-R2.2-G6（发布 0.3.3）｜**日期**：2026-10-13｜**机器**：开发机 Windows（win32 10.0.26100 x64）
- **授权依据**：用户明确指令「修复后发布 0.3.3」（本包允许 git push；tag 二次移动 =
  `M7-ADJ-tag-move-2` 授权的**正式消耗**，条件同首次：预检零 Release 零资产 / 原子操作 +
  `ls-remote` 核对 / 移动后不得再动）。
- **配套**：`docs/m7-evidence/release-runbook.md`（发布链操作手册，M7-R2-C）；前情实录
  `docs/m7-evidence/m7r2-release.md`（R2REL 第一班，停于 main-full #44 linux ctest 8 红）、
  `docs/m7-evidence/m7r21-release.md`（R2.1-F7 第二班，停于 main-full #48 linux `ui_smoke` 1 红）。
  本轮为发行链执行实录**第三班**：任务书载明 G1 已修 Linux 侧 `ui_smoke`（以本轮 main-full
  实跑为准，见 §3），R2.2 G1–G5 全部门禁绿（`m7r22-gate.md` 八项判据）后重走发行链。
- **本文件为过程取证**，随流程逐步落盘；每节命令逐字可复跑。

---

## 1. 预检取证（2026-10-13，tag 移动前）

### 1.1 GitHub Release v0.3.3 不存在（零 Release）

```bash
curl -s -o build/m7r22-g6/rel_check.json -D build/m7r22-g6/rel_check.headers \
     -w "%{http_code}" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
```

- 输出：`404`（2026-10-13 本地）⇒ 该 tag **无 GitHub Release**（发布说明/资产挂载面为空 ⇒
  **零资产**）。原始 JSON/响应头留 `build/m7r22-g6/`（gitignored）。
- 配额读数（响应头逐字）：`X-RateLimit-Limit: 60`、`X-RateLimit-Remaining: 59`、
  `X-RateLimit-Used: 1`、`X-RateLimit-Resource: core`。

### 1.2 releases 列表核对（零 Release 的旁证）

```bash
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases?per_page=5"
```

| tag | published_at | assets |
|---|---|---|
| v0.3.2 | 2026-10-11T02:29:36Z | 4 |
| v0.3.1 | 2026-10-10T11:46:49Z | 4 |
| v0.3.0 | 2026-10-10T04:58:22Z | 4 |
| ci-seed-qt-6.11.2 | 2026-10-10T00:47:33Z | 1 |
| v0.2.0 | 2026-09-23T11:24:10Z | 3 |

⇒ 列表止于 v0.3.2，**无 v0.3.3 条目**（与 §1.1 的 404 互证）。

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
| 零资产 | ✓ | Release 不存在 ⇒ 无附件 |
| tag 落点 b1af371 | ✓ | §1.3 `ls-remote` 逐字 |
| 授权 | ✓ | 用户指令「修复后发布 0.3.3」+ 任务书明文「本包允许 git push、授权二次移动」 |
| 前置「main-full 绿（linux 含 ui_smoke）」 | 待 §3 判定 | G1 已修 Linux `ui_smoke`，以本轮 main-full 实跑为准 |

### 1.5 版本单源一致性（附加预检，runbook §5-3 口径）

```bash
python tools/env.py run -- python tools/ci-versions-check.py
```

- 输出：`ci-versions-check: OK (10 keys, expect=0)`；`CMakeLists.txt:19` =
  `project(PhotoPipeline VERSION 0.3.3 LANGUAGES C CXX)`。

---

## 2. 推送 main（R2.2 G1–G5 全部提交）

- 2026-10-13 02:44Z 本地（10:44 UTC+8）`git push origin 35c4410d704e181f29e25aafb7eb7cfb204eeee2:main`
  → `4addc51..35c4410  main -> main`（RC=0，精确 SHA 推送，共享树并发纪律）。
- **HEAD = `35c4410d704e181f29e25aafb7eb7cfb204eeee2`**（M7-R2.2-GATE: 终门禁收口 + 证据）。
- 推送面：9 个提交（`4c81bd0` G2 B.5⑤ 首单 → `35c4410` GATE），推送前核对
  `git rev-list --left-right --count HEAD...origin/main` = `9  0`（纯快进，origin/main = `4addc51`
  为 HEAD 祖先）；工作树 tracked 面零未提交改动（untracked 的 `AGENTS.md`、
  `tools/baseline/clang-tidy.tsv.candidate.tsv` 非本包归属，不入提交）。
- 触发 **main-full #54**（run 38338314518，head_sha `35c4410d704e`，created
  2026-10-13T02:44:18Z）。

---

## 4. 发行前本机复核（2026-10-13 02:45–02:5x；与 §3 轮询并行执行，两判据都绿才可进 tag 移动）

```bash
bash tools/ci-format.sh
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
```

| # | 判据 | 读数 |
|---|---|---|
| 1 | `ci-format.sh` | **`violations=0`**（钉版 clang-format 23.1.1，**196 文件**全源 0 diff，RC=0）——`build/m7r22-g6/ci-format.log` |
| 2 | build/release 增量重建 | exit 0（114 编译步至 `photopipeline.exe` 链接）；复跑幂等 `ninja: no work to do.`——`build/m7r22-g6/build-release-1.log` |
| 3 | 门禁 A（闭包） | `68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37`；VC 运行时 app-local 10 个 |
| 4 | 门禁 A′（非传递闭包点） | `1 个已随包（0 个为本次显式补拷）`（jxl_threads.dll） |
| 5 | 门禁 B（来源） | `5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致`（Qt 6.11.2 msvc2022_64） |
| 6 | 断言 C（结构） | `photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备`；Qt 链接面 Core/Gui/Widgets/Network 全随包 |
| 7 | 断言 D（许可） | 写盘前 `35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓`；产物内复核 35（≥30 ✓） |
| 8 | 烟测 E-1 | `PhotoPipeline 0.3.3 (exit 0)`；产物内解压即用 `--version` 同（exit 0） |
| 9 | 烟测 E-2 | **`UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)`**（冻结行逐字） |
| 10 | 烟测状态隔离 | staging 快照还原断言过（清走查运行态 4 项：classes.json / logs / logs/failed-*.txt / presets，无残留） |
| 11 | 指纹 F（本机） | `PhotoPipeline-0.3.3-win64.zip` **34,801,005 B** / sha256 `6bc3c2b1823ad588ca8ebdd88c794a7cf4ddb093636904802b494ed8068494d2` / 105 条目（zip 不承诺字节可复现，发行指纹以 Release 附件为准，待 §6 后照 `d049d22` 先例收口） |

- **rc 读数**：`make_winzip.py` 退出码 **0**（后台任务 exit code，`build/m7r22-g6/winzip-gates1.log`）。
- E-2 的 dev exe 取 **`build/release-dev/photopipeline.exe` 只读**（GATE/G5 门禁树：
  `m7r22-gate.md` §②-2 记「复跑幂等 `ninja: no work to do.`」，其后仅 docs 类提交 `35c4410` +
  工作树 tracked 面零改动 ⇒ 与 HEAD 源态一致；`ninja -n` 干跑法因 CONFIGURE_DEPENDS 恒显
  「Re-running CMake」不可判读，如实登记）。make_winzip 的 E-2 是 `shutil.copy2` 拷入 staging
  后运行（`tools/make_winzip.py:686`），**对 build/release-dev 零写入**（纪律 4 成立，F7 同口径）。
- 流程 4 只要求 ci-format + make_winzip 两项，本轮实跑即此两项（八项门禁由 G5 GATE 在同一
  HEAD 源态全量背书，见 `m7r22-gate.md`；附加跑了 `ci-versions-check` 见 §1.5；本包未重复
  金样/回归全套，不重复记过）。

---

## 3. main-full #54（push 触发）：**红**——唯一失败 = winzip job 撞 20min 硬顶被取消

### 3.1 轮询实录（run 38338314518，head_sha = `35c4410d704e`，push 2026-10-13T02:44:18Z）

- 轮询命令（公共 API 免鉴权，间隔 ≥5min，见 §8）：`workflows/main-full.yml/runs?per_page=3`
  → `actions/runs/38338314518/jobs?per_page=20`；02:44 首拍（定位）→ 02:47 / 02:48 / 02:54 /
  02:59 / 03:04 / 03:11（终态）。02:44:18 → 03:05:17Z 全轮到达终态 **21m**（40min 上限内，
  无需继续等待）。03:11 拍到 job 级全完成；03:1x 复取 run 级结论时撞**匿名限流 403**
  （`X-RateLimit-Used: 60` / `Remaining: 0`，reset 03:40:07Z），限流复位后 03:40Z 复取：
  **run 级 conclusion = `completed / cancelled`**（winzip 被 timeout 取消所致的 run 级取消态；
  job 级 gate = failure）——**非绿**，流程 3「至绿」判据未达成的结论不变。
  （登记勘误：限流期间曾以 job 级读数推定「run failure」，实测 run 级为 `cancelled`，
  推定措辞以此更正；两者同为非绿，不影响停止决策。）

### 3.2 job 级读数（`actions/runs/38338314518/jobs`，03:11Z 拍）

| job | 结论 | 耗时 |
|---|---|---|
| lint（clang-format 全源 0 diff） | success | 9s |
| **linux（全 ctest+金样，含 release-dev ui_smoke）** | **success** | **11m40s** |
| audit（许可/SPDX） | success | 86s |
| appimage | success | 6m46s |
| windows（全 ctest+金样） | success | 16m23s |
| tidy（clang-tidy 棘轮） | success | 11m18s |
| **winzip** | **cancelled（timeout-minutes: 20 撞顶）** | **20m26s** |
| cache-gc | success | 8s |
| gate（总闸） | failure（随 winzip） | 7s |

### 3.3 linux job 步级实录（流程 3 的明文前提 = **达成**）

step 13「configure + build + ctest release（全量）」success（5m14s）→ step 14 系统依赖覆盖
自检 success → **step 15「configure + build + ctest release-dev（含 ui_smoke）」success（4m34s）**
→ step 16「UI-SMOKE 冻结行取证」success → step 17「金样 20 对」success → step 18「金样冻结行
断言」success → step 19（失败注解遥测）**skipped（无失败可注解）**。⇒ **Linux 侧 ui_smoke
一条已由 G1 修复并经 main-full 实证**（R2REL 的 8 条 ctest 失败 + F7 的 ui_smoke 1 条均不再现）。

### 3.4 winzip 撞顶归因（步级；runbook §6「暖度/预算/代码量」三分）

winzip job 步级（02:44:31 起算，cap = 20min）：

| step | 名称 | 读数 |
|---|---|---|
| 1–11 | checkout / Qt restore / vcpkg pinned / 二进制缓存 restore / 暖度断言 / 语料断言 | 全 success，合计 **1m03s**（**暖度假绿排除**：暖度断言 1s 过，vcpkg 非重建形态） |
| 12 | configure + build release | success **14m20s**（02:45:34 → 02:59:54） |
| 13 | configure + build release-dev 子集（photopipeline，供 E-2） | **cancelled** 4m42s（02:59:54 → 03:04:36，撞顶在建） |
| 14 | 打包 win64 zip（make_winzip 五门禁/烟测） | **skipped**（未起跑） |
| 15–18 | 指纹 / 上传 / vcpkg save / 汇总 | skipped×2 + save success（缓存已播） |

- **归因 = 代码量（编译时长增长），非暖度、非预算漂移**：对照 main-full **#48**（R2.1 树，
  2026-10-12，同 job **success 19m59s**）步级读数（run 38308098290/jobs）：setup 1m10s /
  step 12 build release **13m05s** / step 13 dev 子集 **4m30s**（完）/ step 14 make_winzip
  **49s** / 指纹+上传+save 21s。#54 相对 #48：build release **+1m15s**（13m05→14m20）、
  dev 子集同量级（4m30 完 vs 4m42 未完）——增量与 `e1017fe..35c4410`（R2.2 G1 探针批 +
  G2 mapwidget 拆 TU + G3）的编译面增长同步。Windows runner 无 ccache（R-124 查证事实，
  runbook §1.1），重跑同型确定性撞顶，空提交/复跑不可解。
- **对 release 链的推算（决定是否动 tag 的关键证据）**：`release.yml` winzip job 与 main-full
  winzip job 的两个构建步**逐字同命令**（`cmake --build --preset release` +
  `cmake --build build/release-dev --target photopipeline`，两 workflow 对照实读），
  同 runner（windows-2025）、同缓存键、同 20min 硬顶（`release.yml:293`「timeout-minutes: 20
  # 硬顶（预算 15 min）」）。⇒ 本树上 release 链 winzip 预计 **≈21–22min（1m03+14m20+~4m50+
  0m49+0m30）> 20min**，大概率在 dev 子集步重演撞顶 ⇒ make_winzip 不起跑、probe/publish
  skipped、**Release 不创建**。历史旁证：release #13（b1af371，vcpkg 冷重建形态）winzip
  20m16s 撞顶 ⇒ publish skipped 零 Release；release #10/#11（0.3.1/0.3.2）winzip 17m43s/
  17m39s 绿——彼时树更小、编译面更轻。

---

## 5. tag v0.3.3 二次移动（M7-ADJ-tag-move-2）—— **未执行（授权保持未消耗）**

- 流程 5 的前置 = 流程 3「轮询 main-full **至绿**」：#54 终态 **completed/cancelled**（run 级
  取消态随 winzip；job 级 gate = failure，§3.1–3.2）⇒ 前置**未达成**。明文前提「linux job
  全绿（含 ui_smoke）」确已达成（§3.3），但判据是「main-full 至绿」，不是「linux 绿」——
  **不放宽判据**（纪律 9）。
- 叠加不可逆性论证（即使按宽口径放行也应停）：tag 移动**此后不得再动** + **禁止第三次移动**
  （M7-ADJ-tag-move-2 条件 + 任务书步骤 8）。§3.4 推算 release 链 winzip **≈21–22min >
  20min 硬顶**，撞顶 ⇒ publish skipped ⇒ **Release 不创建**；移动即把一次性授权烧在
  大概率发不出的树上，0.3.3 将卡死在「tag 已动、零 Release、无第三次机会」的死角。
  两班先例（R2REL §3.9、F7 §3.10）同一裁决口径：**停在坏状态之前，保留授权**。
- runbook §6 失败即停触发项命中：「**任一 job 撞 `timeout-minutes: 20` 被取消 ⇒ 停止发布链；
  归因（暖度/预算/代码量）后再处置；不许改大 timeout**」——归因 = 代码量（§3.4），
  处置权在预算/设计口径（跨界，见 §7），本包不越权。
- 收口复核（停止决策后）：`git ls-remote --tags origin` 逐字

  ```
  cb181ee81b533717f1ac020b57b520f27e2a008a  refs/tags/v0.3.3
  b1af3716c13b8b85d884a0de276515e483b8a8f6  refs/tags/v0.3.3^{}
  ```

  ⇒ tag **未动**（同 §1.3）。`M7-ADJ-tag-move-2` 授权**未消耗**，留待预算/编译面处置后由
  后续发行包按同一条件执行（预检零 Release 零资产 / 原子 + `ls-remote` 核对 / 此后不得再动）。

## 6. release.yml 实况（tag 推送后）—— **未触发**

- 无 tag 推送 ⇒ 无 release.yml 新轮次（前情最新仍为 **#13 run 38198290373 @ b1af3716
  completed/cancelled**，其后无新 run）。
- GitHub Release v0.3.3 **仍未创建**：预检 §1.1 = 404，**收口复检 = 404**（2026-10-13
  03:40Z，`curl …/releases/tags/v0.3.3`，限流复位后实测）。
- 收口文档提交（`e0a1c2c`）推送触发 **main-full #55**（run 38342162697 @ `e0a1c2c81b63`，
  created 03:32:04Z，03:40Z 拍时 in_progress）——docs-only 提交不改编译面，预期同型红于
  winzip 预算；本包不跟踪该轮（发行判读以 #54 为准）。

## 7. 处置与跨界需求

- **跨界需求（本包文件面外，交主对话/预算裁定）**：release 链 winzip job 在当前树的
  实测/推算时长 **≈21–22min > 20min 硬顶**（§3.4），0.3.1/0.3.2 时代的 17.7min 已被代码
  增长吃穿。可选处置（均需裁定，本包不擅自改）：① 预算重标定（设计 `docs/v0.3.0-design.md`
  §12.3 的 15min job 预算 vs §12.6「timeout-minutes 不许改大」硬顶的口径修订——runbook §3
  与 d049d22 均已挂账「重标预算属设计文档口径，待裁定」）；② winzip job 减负（如 dev 子集
  构建/ E-2 与 main-full 的重复验证取舍——动 `.github/`，非本包面）；③ 编译面瘦身（引擎面，
  更远）。**在处置前推 tag = 赌 15% 成功率烧掉唯一一次移动授权**，不做。
- 本包已交付（不受阻部分）：预检取证（§1）、main 推送与 HEAD 记录（§2）、main-full 轮询 +
  撞顶归因与 release 链推算（§3）、发行前本机复核全绿（§4）、停止决策与终态核对（§5–§6）。
- 流程 3 的第二个目的（缓存播种）达成：#54 winzip/windows 两 job 的 vcpkg save 步 **success**
  （winzip 被取消但 step 17 save 已跑完 15s）；Qt(msvc2022_64) save（windows job step 17）与
  Qt(gcc_64) save（linux job step 21）均 **skipped**（键已存在/条件所致——Qt 缓存由既往轮
  播种且本轮 restore 全部命中，暖度断言 1s 过，非断供形态；appimage job 6m46s 绿亦为暖度实证）。
- 取证缺口（如实，已补）：03:1x 复取 run 级 conclusion 时撞匿名限流 403，03:40Z 复位后复取
  为 `completed/cancelled`（勘误见 §3.1）；缺口期间的 job 级读数（§3.2–3.4）均实测无误。

## 8. 复现命令索引（逐字）

```bash
curl -s -o build/m7r22-g6/rel_check.json -D build/m7r22-g6/rel_check.headers -w "%{http_code}" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases?per_page=5"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/main-full.yml/runs?per_page=3"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38338314518/jobs?per_page=20"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38308098290/jobs?per_page=20"
git ls-remote --tags origin
git push origin 35c4410d704e181f29e25aafb7eb7cfb204eeee2:main
bash tools/ci-format.sh
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
python tools/env.py run -- python tools/ci-versions-check.py
```

原始 JSON / 日志（gitignored）：`build/m7r22-g6/`（`rel_check.json`、`rel_check.headers`、
`releases-list.json`、`mainfull-runs.json`、`mainfull54-run.json`、`mainfull54-jobs*.json`、
`mainfull48-jobs.json`、`ci-format.log`、`build-release-1.log`、`winzip-gates1.log`）。

---

*本文件收口于发行停止决策（2026-10-13）；每节命令逐字可复跑。*
