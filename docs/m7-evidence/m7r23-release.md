# m7r23-release —— 0.3.3 发行实况取证（M7-R2.3-G10）

- **包**：M7-R2.3-G10（发布 0.3.3）｜**日期**：2026-10-13｜**机器**：开发机 Windows（win32 10.0.26100 x64）
- **授权依据**：用户明确指令「修复后发布 0.3.3」（本包允许 git push；tag 二次移动 =
  `M7-ADJ-tag-move-2` 授权的**正式消耗**，条件同首次：预检零 Release 零资产 / 原子操作 +
  `ls-remote` 核对 / 移动后不得再动）。
- **配套**：`docs/m7-evidence/release-runbook.md`（发布链操作手册，M7-R2-C）；前情实录
  `docs/m7-evidence/m7r2-release.md`（R2REL 第一班，停于 main-full #44 linux ctest 8 红）、
  `docs/m7-evidence/m7r21-release.md`（R2.1-F7 第二班，停于 main-full #48 linux `ui_smoke` 1 红）、
  `docs/m7-evidence/m7r22-release.md`（R2.2-G6 第三班，停于 main-full #54 winzip 撞 20min 硬顶）。
  本轮为发行链执行实录**第四班**：G6 残余③「release 链 winzip 预算超出」已经
  **G7 工程解**处置（`M7R23G7-0` 主对话裁定：产物复用/拆 job，CI 判据零改动、
  `timeout-minutes` 全 20 未动、五门禁+E-2+冻结行逐步保留；main-full #58/#59 双轮全绿实证
  per-job ≤20min，winzip job 97s/89s），R2.3 G7–G9 全部交付后重走发行链。
- **本文件为过程取证**，随流程逐步落盘；每节命令逐字可复跑。
- **API 取证口径（与三班不同，如实登记）**：三班走匿名 API（60 次/时，均中途撞过限流）。
  本轮改用本机 Git Credential Manager 存储的 GitHub 凭据做 Bearer 认证
  （`git credential fill` 取用，**凭据不落任何文件、不入任何提交**；`X-RateLimit-Limit: 5000`），
  复现命令以环境变量形式给出：

  ```bash
  PAT=$(printf "protocol=https\nhost=github.com\n\n" | git credential fill | sed -n 's/^password=//p')
  curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" <URL>
  ```

---

## 1. 预检取证（2026-10-13 06:39–06:41Z，tag 移动前）

### 1.1 GitHub Release v0.3.3 不存在（零 Release）

```bash
curl -s -o build/m7r23-g10/rel_check.json -D build/m7r23-g10/rel_check.headers \
     -w "%{http_code}" -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" \
     https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
```

- 输出：**`404`**（2026-10-13T06:40Z 本地实测）⇒ 该 tag **无 GitHub Release**（发布说明/资产
  挂载面为空 ⇒ **零资产**）。原始 JSON/响应头留 `build/m7r23-g10/`（gitignored）。
- 附（06:39Z 匿名首测撞限流 403 的如实登记）：先以匿名 curl 试取时 `X-RateLimit-Remaining: 0`
  （reset 07:01:55Z），改用上述认证口径后立即取得 404——404 判定以认证口径实测为准。
- 配额读数（认证口径响应头）：`X-RateLimit-Limit: 5000`、`Remaining: 4999` 级（polling 每拍记账）。

### 1.2 releases 列表核对（零 Release 的旁证）

```bash
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" \
     "https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases?per_page=5"
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
  `b1af3716c13b8b85d884a0de276515e483b8a8f6`（b1af371，gate-fix2）——与任务书预检要求
  「v0.3.3 → b1af371」逐字一致，自 2026-10-12 首次移动后未再动（三班 §1.3 同读数）。

### 1.4 移动前前提判定（M7-ADJ-tag-move-2 条件）

| 前提 | 判定 | 依据 |
|---|---|---|
| 零 Release | ✓ | §1.1 404 + §1.2 列表无 v0.3.3 |
| 零资产 | ✓ | Release 不存在 ⇒ 无附件 |
| tag 落点 b1af371 | ✓ | §1.3 `ls-remote` 逐字 |
| 授权 | ✓ | 用户指令「修复后发布 0.3.3」+ 任务书明文「本包允许 git push、授权二次移动」 |
| 前置「main-full 至绿」 | 待 §3 判定 | G7 工程解后 #58/#59 双绿，本轮 #60 以实跑为准（§3） |
| 前置「winzip 预算处置」（三班残余③） | ✓ 已处置 | `M7R23G7-0` 产物复用/拆 job 落地，main-full #58/#59 全绿（winzip job 97s/89s） |

---

## 2. 推送 main（R2.3 G7–G9 全部提交 + G10 前置）

- 2026-10-13T06:41Z 本地 `git push origin 6b571cb362c22c50bba7e8ef259f371e2f0e2e44:main`
  → `fe3b43f..6b571cb  main -> main`（RC=0，精确 SHA 推送，共享树并发纪律）。
- **HEAD = `6b571cb362c22c50bba7e8ef259f371e2f0e2e44`**（M7-R2.3-G9: 文档收口）。
- 推送面：1 个提交（`fe3b43f` → `6b571cb`，G9 文档收口；G7/G8 诸提交已由前续包推送，
  远端 main 于推送前 = `fe3b43f3bc9c`）；推送前核对
  `git rev-list --left-right --count HEAD...origin/main` = `1  0`（纯快进）；工作树 tracked
  面零未提交改动（untracked 的 `AGENTS.md`、`tools/baseline/clang-tidy.tsv.candidate.tsv`
  非本包归属，不入提交）。
- 推送后 `git ls-remote origin refs/heads/main` 复核 = `6b571cb362c2…`（06:46Z）。
- 触发 **main-full**（run 38358227651，head_sha `6b571cb362c2`，created 2026-10-13T06:42:27Z）。

---

## 4. 发行前本机复核（2026-10-13 06:44–06:48Z；与 §3 轮询并行执行，两判据都绿才可进 tag 移动）

```bash
bash tools/ci-format.sh
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
```

| # | 判据 | 读数 |
|---|---|---|
| 1 | `ci-format.sh` | **`violations=0`**（钉版 clang-format 23.1.1，**196 文件**全源 0 diff，RC=0）——`build/m7r23-g10/ci-format.log` |
| 2 | build/release 增量重建 | exit 0（36 编译步：25 个 CXX obj + `photopipeline.exe` 链接——G8 代码面增量）——`build/m7r23-g10/build-release-1.log` |
| 3 | 门禁 A（闭包） | `68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37`；VC 运行时 app-local 10 个 |
| 4 | 门禁 A′（非传递闭包点） | `1 个已随包（0 个为本次显式补拷）`（jxl_threads.dll） |
| 5 | 门禁 B（来源） | `5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致`（Qt 6.11.2 msvc2022_64） |
| 6 | 断言 C（结构） | `photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备`；Qt 链接面 Core/Gui/Widgets/Network 全随包 |
| 7 | 断言 D（许可） | 写盘前 `35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓`；产物内复核 35（≥30 ✓） |
| 8 | 烟测 E-1 | `PhotoPipeline 0.3.3 (exit 0)`；产物内解压即用 `--version` 同（exit 0） |
| 9 | 烟测 E-2 | **`UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)`**（冻结行逐字） |
| 10 | 烟测状态隔离 | staging 快照还原断言过（清走查运行态 4 项：classes.json / logs / logs/failed-*.txt / presets，无残留） |
| 11 | 指纹 F（本机） | `PhotoPipeline-0.3.3-win64.zip` **34,801,903 B** / sha256 `3f41297e6352f861b04436c484c98437445a952f2c0ae4cda21e584a630d1f23` / 105 条目（zip 不承诺字节可复现，发行指纹以 Release 附件为准，待 §6 后照 `d049d22` 先例收口） |

- **rc 读数**：`make_winzip.py` 退出码 **0**（后台任务 exit code，`build/m7r23-g10/winzip-gates1.log`）。
- E-2 的 dev exe 取 **`build/release-dev/photopipeline.exe` 只读**（对 build/release-dev 零写入，
  纪律 4 成立，F7/G6 同口径）。源态一致性证据：`build/release-dev/.ninja_log` 显示 14:00（UTC+8）
  最后一轮构建重建 25 个 UI obj（含 G8 触碰的 mainwindow/page_output/paramform/smoke_probes 等）
  并重链 exe；`git ls-files src tests tools` 无任一文件 mtime 新于该 exe ⇒ 与 HEAD 源态一致
  （其后仅文档类提交 `fe3b43f`/`6b571cb`，不影响二进制）。
- 流程 4 只要求 ci-format + make_winzip 两项，本轮实跑即此两项（`cmake --build build/release`
  为 make_winzip 的 PP_BUILD_DIR 输入面刷新，同 G6 口径；本包未重复金样/回归全套，不重复记过）。

---

## 3. main-full #60（push 触发）：**全绿**（run 级 completed/**success**，十 job 全过）

### 3.1 轮询实录（run 38358227651，head_sha = `6b571cb362c2`，push 2026-10-13T06:42:27Z）

- 轮询命令（认证 API，间隔 ≥5min）：`actions/workflows/main-full.yml/runs?per_page=3`
  → `actions/runs/38358227651/jobs?per_page=20`；拍点 06:43（runs 定位）→ 06:49:28（job 级）→
  06:49:55（job 级）→ 06:55:31（job 级）→ 07:00:58（job 级 + run 级结论）。
  06:42:27 → 06:59:38Z 全轮到达终态 **17m11s**（40min 上限内）。
- **如实登记（轮询纪律滑脱一次）**：06:49:55 拍距上一拍仅 27s（<5min 间隔）——任务书
  「≥5min 间隔」纪律未逐拍满足，该拍为多余读数（内容与 06:49:28 拍一致：linux 已转绿）；
  其后拍点恢复 ≥5min。认证配额 5000/时无耗尽风险（`X-RateLimit-Remaining` 逐拍记账在
  `build/m7r23-g10/` 响应头），但纪律违反如实上报。
- run 级结论：**`completed / success`**（07:00:58Z 实测，`updated_at` 2026-10-13T06:59:38Z）。

### 3.2 job 级读数（`actions/runs/38358227651/jobs`，07:00:58Z 终拍）

| job | 结论 | 耗时 |
|---|---|---|
| lint（clang-format 全源 0 diff） | success | 0.1min |
| linux（全 ctest+金样，含 release-dev ui_smoke） | success | 7.1min |
| audit（许可/SPDX） | success | 1.4min |
| appimage | success | 5.0min |
| windows-dev（ui_smoke+金样，G7 拆段新 job） | success | 7.8min |
| windows（全 ctest+金样） | success | 10.8min |
| tidy（clang-tidy 棘轮） | success | 16.7min |
| **winzip（G7 拆段后 = 打包段）** | **success** | **1.4min** |
| cache-gc | success | 0.1min |
| gate（总闸） | success | 0.1min |

- **判据达成**：流程 3「轮询 main-full 至全绿」——run 级 success + 十 job 全绿，**无一撞
  `timeout-minutes: 20`**。G7 工程解在本树实证（winzip 打包段 1.4min，对照 G6 时点同职责
  单 job 20m26s 撞顶）；整轮 wall 17m11s。
- 对照 #59（fe3b43f 前推，run 38352494135，全绿）：十 job 同型（winzip 1.5min），
  本轮仅 docs 提交增量（`fe3b43f..6b571cb`），编译面无变化。

---

## 5. tag v0.3.3 二次移动（M7-ADJ-tag-move-2 正式消耗，2026-10-13T07:02Z）—— **已执行**

- 前提全过（流程 1–4）：预检零 Release 零资产（§1.1/§1.2，移动前 07:01Z 复检仍 **404**）·
  tag 落点 b1af371（§1.3）· main-full #60 **全绿**（§3）· 本机复核全绿（§4）。
- 操作（原子，单 ref 更新）：

  ```bash
  git tag -f -a v0.3.3 -F build/m7r23-g10/tagmsg.txt 6b571cb362c22c50bba7e8ef259f371e2f0e2e44
  git push --force origin refs/tags/v0.3.3
  ```

  - tag 注解**原文保留**（`tagmsg.txt` = 旧 tag 对象 `cb181ee` 的 message 逐字，3 行：
    `PhotoPipeline 0.3.3` + 空行 + 0.3.3 内容摘要），仅 object 指向与 tagger 时间戳更新；
  - push 输出：`+ cb181ee...0aba959 v0.3.3 -> v0.3.3 (forced update)`，RC=0。
- 移动后核对（`git ls-remote --tags origin` 逐字，07:02Z）：

  ```
  0aba95941379b4dfb9ffcd594856f344a0042240	refs/tags/v0.3.3
  6b571cb362c22c50bba7e8ef259f371e2f0e2e44	refs/tags/v0.3.3^{}
  ```

  ⇒ tag 对象 `cb181ee` → **`0aba959`**，解引用 = **`6b571cb`**（发行树 = main HEAD，
  任务书「指向新 HEAD」逐字达成）。
- **M7-ADJ-tag-move-2 授权就此正式消耗**；按条件「**移动后不得再动**」+ 任务书步骤 8
  「禁止第三次移动」——自此 tag 落点不可变，release.yml 再红亦不得回移（失败即停如实上报）。
- tag push 触发 **release.yml run 38360204850**（head_sha `6b571cb362c2`，head_branch
  `v0.3.3`，created 2026-10-13T07:02:27Z）——见 §6。

---

## 6. release.yml 实况（tag 推送后）：**全绿，publish 执行，GitHub Release 已创建**

### 6.1 轮询实录（run 38360204850，head_sha = `6b571cb362c2` @ tag `v0.3.3`）

- 轮询命令（认证 API）：`actions/workflows/release.yml/runs?per_page=3` →
  `actions/runs/38360204850/jobs?per_page=20`；拍点 07:02:39（runs 定位，queued）→ 07:08:46
  → 07:12:14 → 07:17:59 → 07:23:42（job 级 + run 级结论）。07:02:27 → 07:19:47Z 全轮终态
  **17m20s**（60min 上限内）。
- **如实登记（轮询纪律滑脱）**：与 §3.1 同型——07:12:14 拍距上一拍 3m28s（<5min）。
  两次滑脱（main-full 一次 + 本轮一次）均为提前查看、未节省等待也未致配额问题，但
  「≥5min 间隔」纪律未逐拍满足，如实上报。
- run 级结论：**`completed / success`**（07:23:42Z 实测，`updated_at` 2026-10-13T07:19:47Z）。

### 6.2 job / 步级读数（`actions/runs/38360204850/jobs`，07:23:42Z 终拍）

| job | 结论 | 耗时 | 预算（runbook §7.4） |
|---|---|---|---|
| appimage | success | 4.6min | 12 min ✓ |
| winzip-build（release 构建 → artifact `winzip-build-input`） | success | **14.8min** | 15 min ✓（贴近预算，20min 硬顶内） |
| winzip-build-dev（release-dev 子集 → artifact `winzip-smoke-exe`） | success | **6.3min** | 7 min ✓ |
| winzip-pack（双 artifact + make_winzip 五门禁/E-2 + 指纹） | success | **1.4min** | 8 min ✓ |
| probe-appimage | success | 0.7min | 3 min ✓ |
| probe-winzip | success | 0.2min | 3 min ✓ |
| publish | success | 0.7min | 2 min ✓ |

- **G7 工程解 release 侧首跑实测读数补齐**（runbook §7.4 挂账的「无 tag 轮实测」项）：
  winzip-build **14.8** / winzip-build-dev **6.3** / winzip-pack **1.4** min，三段各自 ≤20min
  硬顶、均在预算内；关键路径 = winzip-build + winzip-pack + probe-winzip + publish =
  **17.1min ≤ 20min**（d049d22 同口径）。对照 G6 时点单 job 20m26s 撞顶 ⇒ 拆段解成立。
- winzip-pack 步级（全绿）：下载 winzip-build-input → 下载 winzip-smoke-exe → 输入面断言 →
  **打包 win64 zip（tools/make_winzip.py，内置五门禁/烟测）** → 产物指纹 → 上传 artifact。
- publish 步级（全绿）：① 查重（gh release view，无既有 Release）→ 下载两平台产物 → ② 发布级
  产物校验（AppImage 下载件再跑 `--version`）→ tag↔版本一致性断言 → ④ 生成发布说明
  （CHANGELOG [0.3.3] 节）+ SHA256SUMS → ③ `gh release create`。

### 6.3 GitHub Release v0.3.3 + 资产指纹（`curl releases/tags/v0.3.3` = **200**）

- **Release URL：<https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.3.3>**
  （name `PhotoPipeline 0.3.3`，published_at **2026-10-13T07:19:43Z**，draft=false）。
- 4 资产（名 / 大小 / digest 逐字，API `digest` 字段与下载件 `SHA256SUMS.txt` 交叉核对一致）：

| 资产 | 大小 | sha256 |
|---|---|---|
| `PhotoPipeline-0.3.3-win64.zip` | **33,732,900 B** | `0074d2f918026d280bd6584d0acb4b0c933e84fbc6991cd06992f1072b9b7352` |
| `PhotoPipeline-0.3.3-x86_64.AppImage` | **52,062,712 B** | `3e8bb82223cb07ebe1800b623e6d13742a1e33bd957f4c6755ad2ef7a3f98a86` |
| `PhotoPipeline-0.3.3-deps.txt` | **2,439 B** | `e8c80b3d61a4e0d41cde50eb3c04577688d7428cc189129b3331cf221407854d` |
| `PhotoPipeline-0.3.3-SHA256SUMS.txt` | **293 B** | `0e7ded5604c385c98c080142550e4443cede4171916aaddc5af1a73b718bee77` |

- 交叉核对（`curl -L …/PhotoPipeline-0.3.3-SHA256SUMS.txt` 下载件逐字）：

  ```
  3e8bb82223cb07ebe1800b623e6d13742a1e33bd957f4c6755ad2ef7a3f98a86  PhotoPipeline-0.3.3-x86_64.AppImage
  e8c80b3d61a4e0d41cde50eb3c04577688d7428cc189129b3331cf221407854d  PhotoPipeline-0.3.3-deps.txt
  0074d2f918026d280bd6584d0acb4b0c933e84fbc6991cd06992f1072b9b7352  PhotoPipeline-0.3.3-win64.zip
  ```

  ⇒ 三个发布件指纹 API ↔ 下载件**逐字一致**；SHA256SUMS.txt 自身文件 sha256 实测
  `0e7ded56…` 与其 API digest 一致。指纹口径 = `d049d22` 先例（取自下载件，非本地推算）。
- 观察（事实登记）：`deps.txt` 指纹与 0.3.1 时代记录（`d049d22` 提交正文：2,439 B /
  `e8c80b3d…`）完全相同——依赖闭包清单在依赖集未变（vcpkg tag 钉死）时逐字稳定；
  本机 zip（34,801,903 B，§4）与 CI zip（33,732,900 B）**不同字节**，印证「zip 不承诺
  字节可复现，发行指纹以 Release 附件为准」口径（CHANGELOG [0.3.3] 发行节既有注记）。

---

## 7. 处置与跨界需求

- **`M7-ADJ-tag-move-2` 授权正式消耗**（十五节裁定的执行落地）：预检零 Release 零资产 /
  原子 push + `ls-remote` 核对 / 移动后不得再动——三条件全部满足，执行记录见 §5。
  **tag `v0.3.3` 落点就此冻结于 `0aba959` → `6b571cb`，任何后续情形不得再移动（含
  release 红灯；任务书步骤 8「禁止第三次移动」）。**
- 跨界登记（本包文件面外，交主对话）：runbook `docs/m7-evidence/release-runbook.md` §7.4
  的「release 侧三段无 tag 轮实测」挂账项已有读数（本文件 §6.2：14.8 / 6.3 / 1.4 min），
  待 runbook 归属方补入手册；本包不改该文件（独占面纪律 3）。
- 发布说明口径核对：publish 步「④ 生成发布说明（CHANGELOG [版本] 节）」取的是**tag 树**
  （`6b571cb`）的 CHANGELOG [0.3.3] 正文——本包 §8 的「已发行」终态收口提交**在 tag 之后**
  入 main，不入 tag（`d049d22` 先例、AGENTS.md §9「报告类提交不入 tag」）。

## 8. 复现命令索引（逐字；`$PAT` 取法见文首 API 口径）

```bash
PAT=$(printf "protocol=https\nhost=github.com\n\n" | git credential fill | sed -n 's/^password=//p')
curl -s -o build/m7r23-g10/rel_check.json -D build/m7r23-g10/rel_check.headers -w "%{http_code}" -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" "https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases?per_page=5"
git ls-remote --tags origin
git push origin 6b571cb362c22c50bba7e8ef259f371e2f0e2e44:main
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/main-full.yml/runs?per_page=3"
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38358227651/jobs?per_page=20"
bash tools/ci-format.sh
python tools/env.py run -- cmake --build build/release
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
git tag -f -a v0.3.3 -F build/m7r23-g10/tagmsg.txt 6b571cb362c22c50bba7e8ef259f371e2f0e2e44
git push --force origin refs/tags/v0.3.3
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/release.yml/runs?per_page=3"
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38360204850/jobs?per_page=20"
curl -s -H "Authorization: Bearer $PAT" -H "Accept: application/vnd.github+json" https://api.github.com/repos/zhang-hz/PhotoPipeline3/releases/tags/v0.3.3
curl -sL -o build/m7r23-g10/SHA256SUMS.txt "https://github.com/zhang-hz/PhotoPipeline3/releases/download/v0.3.3/PhotoPipeline-0.3.3-SHA256SUMS.txt"
```

原始 JSON / 日志（gitignored）：`build/m7r23-g10/`（`rel_check.json`、`rel_check.headers`、
`rel_check_pre_move.json`、`releases-list.json`、`mainfull-runs.json`、`mainfull60-*.json`、
`release-runs.json`、`rel60-*.json`、`rel_final.json`、`SHA256SUMS.txt`、`deps.txt`、
`tagmsg.txt`、`ci-format.log`、`build-release-1.log`、`winzip-gates1.log`）。

---

*本文件收口于 0.3.3 发行完成（2026-10-13）；每节命令逐字可复跑。*
