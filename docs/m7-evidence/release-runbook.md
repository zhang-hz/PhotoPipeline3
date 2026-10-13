# 发布链操作手册（release-runbook）

> **范围**：tag 推送 → `release.yml`（appimage / winzip / probe-* / publish）→ GitHub Release 的
> 全链路操作与判读。配套证据：`docs/m7-evidence/m7r2-c-ci-budget-timings.md`（线上实况取证）、
> `docs/m7-evidence/m7r2-c-winzip-gates.md`（五门禁本地读数）。设计判据真源：
> `docs/v0.3.0-design.md` §12.1–§12.7；20min 硬顶口径 §12.6（**job/整轮双 ≤20min，timeout-minutes
> 不许改大**）。
>
> 本手册由 M7-R2-C 于 2026-10-12 编写；所有「实测」条目均在当日经 GitHub 公共 API 或本地实跑取证
> （命令逐字可复跑，见各节）。

---

## 1. 发布链拓扑与缓存链（事实地图）

```
push main ──→ main-full（lint/tidy/linux/windows/appimage/winzip/audit/cache-gc/gate）
                 │  ① linux job = ccache 唯一常规写者（键 ccache-<os>-…-<run_id>）
                 │  ② windows job = Qt(msvc2022_64) 唯一常规写者；winzip/tidy 只 restore
                 │  ③ 各 Windows job save vcpkgwin-*（滚动键，末段 run_id）
tag v* 推送 ──→ release.yml
                 ├ appimage（12min 预算）：restore Qt(gcc_64)/vcpkg-/ccache-Linux- → 构建 → 4 项烟测
                 ├ winzip-build（15min 预算）：构建两树 → 上传 winzip-build-input（§7）
                 ├ winzip-pack（8min 预算）：下载 winzip-build-input → make_winzip 五门禁（§7）
                 ├ probe-appimage / probe-winzip：下载产物独立 --version 断言（§12.7 ②）
                 └ publish：查重（§12.7 ①）→ tag↔版本 → gh release create（§12.7 ③）+ CHANGELOG 说明 + SHA256SUMS
```

### 1.1 四类缓存键（全仓逐字一致；来源 = 各 workflow 的 restore/save 步）

| 前缀 | key | restore-keys | 写者 | 读者 |
|---|---|---|---|---|
| `ccache-` | `ccache-<os>-${hashFiles('CMakeLists.txt','src/**','CMakePresets.json')}-${hashFiles('cmake/**','triplets/**')}-<run_id>` | `ccache-<os>-` | **仅 Linux**：main-full `linux` job、warm-cache `warm` job | main-full `linux`/`appimage`、nightly、pr-fast、release `appimage` |
| `vcpkg-`（Linux）/ `vcpkgwin-`（Windows） | `…-${hashFiles('vcpkg.json','vcpkg-overlay/**')}-${hashFiles('triplets/**')}-<job>-<run_id>-<attempt>` | 两级：同哈希前缀 → 宽前缀 | 各 Linux/Windows 构建 job 与 warm-cache（滚动键） | 同平台全部 job |
| `qt-` | `qt-<QT_VERSION>-gcc_64\|win64_msvc2022_64-<os>-<arch>` | 同前缀 | main-full `linux`(gcc_64)/`windows`(msvc2022_64)、warm-cache、release `appimage`(gcc_64) | 其余全部 |

**Windows 侧没有 ccache（查证事实，不要按 Linux 习惯推理）**：`CMakeLists.txt:126` 与
`CMakeLists.txt:136-144`（M7-WP3 **R-124** 裁定）——compiler launcher 只在
`CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"` 时挂接，MSVC（cl.exe）**不挂**、回退无缓存构建
（理由：ccache 探针是 GCC 风格参数，MSVC 挂 launcher = 坏缓存直接全构建红）。因此：
- `main-full` 的 windows/winzip/tidy job **不写** `CCACHE_DIR`（实为空目录）；
- `release.yml` 的 winzip-build/winzip-pack job **无** ccache restore/save 步（与设计一致，非缺失）；
- 任何「给 Windows job 补 ccache save/restore」的改动都只能存/取**空缓存**——`restore` 会
  「命中」一个空条目但编译速度零收益，是伪命中。**本包裁定不补**（见 handoff「裁定登记」）。
  Windows 构建提速的正道在引擎面（R-124 解冻重议），不在 workflow 对称性。

### 1.2 vcpkg 暖度断言（PP_VCPKG_WARM_MIN=20）

- 位置：`release.yml:77`（常量）+ `release.yml:139-147`（appimage）/ `release.yml:350-358`
  （winzip）；main-full 3 处、nightly 5 处同口径。
- 判据：`find … -name '*.zip' | wc -l ≥ 20`，早失败并指路 warm-cache。
- **已知缺口（实测，见 §3）**：二进制缓存文件名是纯 sha256（如
  `.cache/vcpkg-binary/03/033e44….zip`），**无法**按名断言「哪个端口在缓存里」、更无法断言
  ABI 新旧。计数 ≥20 只证明「有 20 个以上档」，不证明「这 20 个档能命中当前安装计划」——
  ABI 失配时 vcpkg 会**源码重建**而断言照样绿（假绿）。故暖度的最终判读是
  **configure 步骤实际耗时**（暖 = 秒级；冷 = 分钟级），见 §2。

---

## 2. 暖度验证方法（推 tag 前必做）

1. **warm-cache 播种轮绿**：Actions → warm-cache → Run workflow（dependency/Qt/triplet 有变化时
   必跑；平时可复用）。判读 = 整轮 `success` 且 `warm`、`warm-windows` 两个 job 均过，
   summary 里 Qt/vcpkg/ccache save 均执行（`if: always()` 步骤看 summary，不要只看绿勾）。
   - **解析期秒败的识别**（2026-10-12 实况，warm-cache #9–#13）：run 的
     `created_at == updated_at` 且 jobs 列表为空 ⇒ workflow YAML 非法被拒，一行没跑。
     此时**先修 workflow**，播种通道是断的，任何「先推 tag 再说」都是赌冷构建。
2. **首轮 main-full 核对 configure 耗时**（见 §4 命令）：windows/winzip/tidy 三个 Windows job
   的第一个 configure/build 步骤。秒级 = vcpkg 全命中；**分钟级 = 在源码重建端口**，
   该轮 20min 硬顶必然危险。
3. **release.yml 开跑后再核对一次**：暖度断言（≥20 档）绿**不等于**暖，以 winzip job
   「configure + build release」步骤的实跑耗时为准（对照基线：10-10 轮 9m14s=正常）。

## 3. 实测教训（2026-10-10 … 10-12，取证文件 m7r2-c-ci-budget-timings.md）

| 轮次 | 结论 | 关键读数 |
|---|---|---|
| main-full #37（10-10，d049d22） | 全绿 | winzip 14m53s、windows 17m11s、tidy 8m40s；tidy 的 **configure-only 43s** |
| release #10/#11（0.3.1/0.3.2） | 绿 | winzip job 17m43s / 17m39s（**距 20min 硬顶仅 ~2min**） |
| main-full #38/#39（10-11，f661a1fe） | 红（tidy 棘轮） | Windows 侧仍绿但已紧：winzip 18m16s / 17m58s |
| warm-cache #9–#13（10-12，97cd9da…a4cf0fc） | **全部解析期秒败** | 0 job；根因 = M7-WP3（18fb355）R-063 删值行留下空 `env:` 键（Line 247 Unexpected value）；M7-R2-C 已修 |
| main-full #41/#42（10-12） | 红 | windows/winzip/tidy **三 job 全部 20m10s 撞顶取消**；tidy 的 configure-only 从 43s → **>19min 未完**（vcpkg 期膨胀；暖度断言全绿 = 假绿实证） |
| release #13（10-12，b1af371） | **取消** | winzip 20m16s 撞顶；step 12「configure+build」19m11s 未完；probe/publish 全 skipped，**Release 未创建**（m7-report §3.8） |

**结论**：发布链当前的主要风险不是「缓存键不对」，而是 ① warm-cache 解析期红 ⇒ 播种断供
（已修，本手册 §2 就是它的操作面）；② Windows 侧 vcpkg 缓存 ABI 假绿（计数断言管不到）；
③ winzip job 预算余量只有 ~2min。Linux 链（ccache/vcpkg）对称完整、同轮全绿，无需改动。

## 4. 轮询公共 API 命令（2026-10-12 实测可用；公开仓库免鉴权，限速 60 req/h）

```bash
# ① 最近几轮 release.yml / warm-cache.yml（识别：created==updated ⇒ 解析期秒败）
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/workflows/warm-cache.yml/runs?per_page=6" \
  | python -c "import json,sys;[print(r['run_number'],r['id'],r['conclusion'],r['created_at'],'->',r['updated_at']) for r in json.load(sys.stdin)['workflow_runs']]"

# ② 一轮 run 的 job 级耗时（发布链预算校准的最小取证单位）
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/<RUN_ID>/jobs?per_page=20" \
  | python -c "import json,sys;[print(j['name'],'|',j['conclusion'],'|',j['started_at'],'->',j['completed_at']) for j in json.load(sys.stdin)['jobs']]"

# ③ step 级耗时（判 configure 是 43s 暖还是 >19min 冷，就看这一步）
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/<RUN_ID>/jobs?per_page=20" \
  | python -c "
import json,sys
for j in json.load(sys.stdin)['jobs']:
    print('==',j['name'],j['conclusion'])
    for s in j.get('steps',[]):
        print('  ',s['number'],s['name'][:50],'|',s['conclusion'],'|',s.get('started_at'),'->',s.get('completed_at'))"
```

日志本体（logs API）需要鉴权，公开匿名只到 job/step 级耗时——判读就以耗时形态为准。

## 5. tag 前置条件（checklist，逐条打勾再推）

1. [ ] warm-cache 近期一轮**绿**（且其后 `vcpkg.json`/`vcpkg-overlay/`/`triplets/`/Qt 版本无变化）；
2. [ ] 目标提交上 main-full **全绿**，且 gate 预算校准表里 windows/winzip/tidy 均在预算内；
3. [ ] 版本单源一致：`python tools/env.py run -- python tools/ci-versions-check.py`
      → `OK`（CMakeLists ↔ vcpkg.json ↔ CHANGELOG ↔ README；R-122 教训：vcpkg.json version 漏同改
      会在 appimage job 6s 处红，见 m7-report §3.8）；
4. [ ] 本地门禁读数在手：`python tools/env.py run -- python tests/golden/smoke.py build/release-dev`
      （SMOKE 20/20 冻结行）、`python tools/env.py run -- python tools/regression.py`（zero diff）、
      `python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe`
      （五门禁 + E-2，读数见 m7r2-c-winzip-gates.md）；
5. [ ] tag 推送后**人在场**盯 §4 ③ 的 winzip job 步骤耗时（见 §6 即停条件）。

## 6. 失败即停纪律（触发即停止发布动作，不得自行越过）

| 触发 | 动作 |
|---|---|
| warm-cache 解析期秒败（created==updated，0 job） | **不推 tag**；先修 workflow 并重跑播种 |
| 暖度断言红（archive < 20） | 跑 warm-cache 播种后复核；**禁止**硬跑冷构建「赌它来得及」 |
| winzip/windows job 的 configure 步骤 **>5 min** 无完成迹象 | 判定 vcpkg 源码重建：取消该轮、重新播种、不消耗任何 tag 动作 |
| 任一 job 撞 `timeout-minutes: 20` 被取消 | 停止发布链；归因（暖度/预算/代码量）后再处置；**不许改大 timeout**（§12.6 硬约束） |
| tag 轮次出现任何**新原因**红 | 按 M7-ADJ-tag-move 裁决条件：停止、上报、**禁止二次移动 tag**（已发布的 tag 不可移动） |
| publish 前 `gh release view` 显示该 tag 已有 Release | 显式失败即本意（§12.7 ①）；人工处置，不得 `--force` |

---

## 7. job 预算工程解（M7-R2.3-G7，2026-10-13）

> **解决的问题**：winzip「构建+打包」单 job 越过 20min 硬顶（`timeout-minutes: 20`，§12.6 不许改大）。
> **手段**（主对话裁定 M7R23G7-0）：**产物复用 / 拆 job**——不改 CI 判据、不放宽任何 timeout-minutes、
> 不减五门禁与 E-2 `--smoke-exe`、打包对象必须是被验证过的构建产物。

### 7.1 拆分理由（实测依据，两轮归因）

| 证据 | 读数 | 出处 |
|---|---|---|
| main-full #54 winzip job | 「configure+build release」13m05s→14m20s + release-dev 子集段 282s 未完即撞 20min 硬顶；五门禁/E-2 一步未跑 | 任务书 M7R23G7 背景；#54 jobs API step 级取证（2026-10-13） |
| main-full #57（一轮修复 83a9cb5）windows job | step 12（configure+build+ctest release）**872s** + step 13（release-dev 子集）272s 未完即被取消（20m01s）；同段 #54 全绿时 612s+275s（合计 16m23s）——83a9cb5 零 C++ 改动 ⇒ **runner 速度波动（612s vs 872s）+ lane 工作量超载**，不是代码增长 | #57 jobs API step 级取证 |
| release 链 winzip | 同命令同 runner 同硬顶，实测推算 ≈21-22min（17m43s/17m39s 时代余量仅 ~2min） | 任务书；§3 表 release #10/#11/#13 |
| 历史余量 | #37 winzip 14m53s → #39 17m58s → #54/#57 撞顶（单调恶化） | §3 表（m7r2-c 取证） |
| 处置条款 | 单 job 超 20min → **拆 job 或砍步骤（禁止放宽时限）** | design §12.3 末行 |

「砍步骤」不可行——五门禁 A/B/C/D + E-1/E-2 是硬判据（本手册 §1.2、m7r2-c-winzip-gates.md），不许减
⇒ 只剩**拆段/产物复用**。打包段实测本机 <4min（§7.4 本地行）；膨胀源 = 构建段（release 构建
860-872s + release-dev 子集 275-282s，且随 runner 波动 ±40%），故两轮拆出三个段：

1. **一轮**：winzip 的「构建+打包」拆段，打包吃同 run 已构建树（winzip 不再 configure+build）；
2. **二轮**（#57 归因）：windows/winzip-build 里的 **release-dev 子集段**再拆成独立 job
   （windows-dev / winzip-build-dev，与主构建并行），每个 job 的工作量 = 一段构建 + 最小外围，
   对 runner 波动留出 ≥3min 余量。

### 7.2 复用链路图（终稿）

**main-full（打包吃「已验证」产物，双背书）**：

```
lint ─┬→ windows（release 构建 + ctest release 全量，绿）
      │      └─ 上传 winzip-build-input（release 树 + 许可面）
      ├→ windows-dev（release-dev 子集构建 + ctest -R ui_smoke + 金样 20 对 + 冻结行断言，绿）
      │      └─ 上传 winzip-smoke-exe（build/release-dev/photopipeline.exe）
      └→ winzip（needs: [windows, windows-dev]——两侧测试全绿才打包）
             ├─ 下载 winzip-build-input + winzip-smoke-exe → .（还原原布局）
             ├─ 输入面断言（缺项列名早失败）
             └─ make_winzip 五门禁 + E-2（命令逐字不变）→ PhotoPipeline-win64.zip → gate
```

**release.yml（同 run 内无既有 Windows 构建 job ⇒ 自拆三段）**：

```
winzip-build（release 构建 + 上传 winzip-build-input）─┐
winzip-build-dev（release-dev 子集 + 上传 winzip-smoke-exe）─┴→ winzip-pack
        （下载双 artifact + 输入面断言 + make_winzip 五门禁 + E-2）
                └→ probe-winzip（§12.7 ② 独立 --version）──→ publish
```

### 7.3 artifact 传递的内容与命名

| 项 | `winzip-build-input` | `winzip-smoke-exe` |
|---|---|---|
| 生产者 | main-full `windows`（测试绿后）/ release `winzip-build` | main-full `windows-dev`（ui_smoke+金样绿后）/ release `winzip-build-dev` |
| 消费者 | `winzip` / `winzip-pack`（`needs` 硬依赖，双件齐才打包） | 同左 |
| 保留期 | main-full 14 天 / release 30 天 | 同左 |
| 布局（artifact 根 = 下载到 workspace 根，`PP_BUILD_DIR`/`--smoke-exe` 零适配） | `build/release/photopipeline.exe`＋`build/release/*.dll`（40 个，vcpkg app-local 运行时依赖，**含非传递闭包点 `jxl_threads.dll`**）＋`vcpkg_installed/x64-windows-avx2/share/**`（许可面，门禁 D 输入，317 文件/3.5MB；triplet 单源 = `CMakePresets.json:18`） | `build/release-dev/photopipeline.exe`（E-2 `--smoke-exe`） |
| 裸量 | ≈57MB（本地实测：release exe 3.0MB + 40 dll 50.4MB + share 3.5MB） | ≈3.9MB |

- **不**传：`vcpkg_installed/bin`（281MB——运行时 DLL 已 app-local 在 `build/release/*.dll`）、
  `lib/include/debug`、Qt（Qt 缓存链照旧 restore，windeployqt/门禁 B 输入）、
  CRT（vcvars → `Microsoft.VC*.CRT` app-local，`resolve_crt_dir` 照旧）。
- 消费侧输入面断言：`build/release/photopipeline.exe`、`build/release-dev/photopipeline.exe`、
  `build/release/jxl_threads.dll`、`vcpkg_installed/x64-windows-avx2/share` 与 dll≥1；缺项列名早失败。
- `tools/make_winzip.py` **零改动**（`PP_BUILD_DIR` 默认 `build/release`、`--smoke-exe` 显式路径与
  布局天然兼容）。

**缓存纪律保留（裁定要求「保留现有 vcpkg 暖度断言与 ccache 链」）**：

- 打包侧（`winzip` / `winzip-pack`）「缓存目录预建 + vcpkg binary cache(restore) +
  vcpkg 缓存暖度断言」三步**逐字保留**（判据 ≥20 档、指路 warm-cache 的文案不变）；
- vcpkg 的 clone/bootstrap（R-091 钉版 SHA 断言）与 binary cache **save**（安装侧写者）跟随
  `vcpkg install` 留在构建段（windows / windows-dev / winzip-build / winzip-build-dev）——
  打包段不再调用 vcpkg 二进制；
- ccache 链零改动（Linux 写者/读者照旧；Windows 无 ccache = R-124 查证事实，§1.1）；
- Qt 单写者不变（windows job；windows-dev / winzip-* 只 restore）。

### 7.4 每 job 预算读数

**main-full #58（fa871c8，2026-10-13 05:10Z，全绿——G7 终稿拓扑首跑）**：

| job | 预算 | #58 实测 | 读数 |
|---|---|---|---|
| lint | 1 min | 9s | ✓ |
| tidy | 12 min | 1114s = 18m34s | ⚠ 超预算 +6.5min（硬顶内）；预算漂移属既有事实（#57 同段 16m16s），与 G7 无关，登记不修 |
| linux | 12 min | 661s = 11m01s | ✓ |
| windows（release 构建 + ctest 全量） | 15 min | 699s = 11m39s | ✓（#57 慢 runner 同段 872s ⇒ 慢端推算 ≈16-17min，仍在硬顶内） |
| windows-dev（release-dev 子集 + ui_smoke + 金样） | 7 min | 374s = 6m14s | ✓ |
| appimage | 12 min | 279s = 4m39s | ✓ |
| winzip（打包段：双 artifact + 五门禁 + E-2） | 15 min | **97s = 1m37s**（make_winzip 步 48s） | ✓（原「构建+打包」20min 撞顶 → 打包段 1.6min） |
| audit | 3 min | 75s = 1m15s | ✓ |
| cache-gc / gate | 1 / 1 min | 5s / 8s | ✓ |
| **整轮 wall** | 20 min 硬顶 | **19m03s**（05:10:10→05:29:13） | ✓（本轮关键路径 = tidy 18m34s；windows+winzip 链 = 796s = 13m16s） |

**本地五门禁等效读数（2026-10-13，build/m7r23-g7/local-winzip.log，`make_winzip.py` 零改动）**：
`python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe`
→ rc=0：闭包门禁 A 68 PE/105 导入名（staging 51·apiset 17·system 37）、来源门禁 B 5 个 Qt6*.dll
sha256 逐一一致、结构断言 C 齐备（imageformats=4/tls=2）、许可断言 D 35 copyright（≥30）、
烟测 E-1 `PhotoPipeline 0.3.3`、烟测 E-2 `UI-SMOKE OK shots=12 pages=3`、产物内复核 35 copyright
+ 解压即用 --version；zip 34,801,005 B / 105 条目。

**如实说明**：① release 侧三段（winzip-build / winzip-build-dev / winzip-pack）只有预算值，
**无 tag 轮实测**（不编数——发行轮首跑后按 §4 命令补读数）；② 一轮修复（83a9cb5）在 main-full #57
失败：windows job 撞 20min 硬顶（归因见 §7.1 行 2），二轮拆段（fa871c8）后 #58 全绿；③ 整轮 wall
的余量是 runner 波动的函数（#58 windows 699s vs #57 同段 872s），慢 runner 轮次 wall 可能再逼近
或越过 20min 口径——per-job 硬顶在两种波动下均守住。

---

*手册随实况维护；新教训按 §3 表追加行，不删旧行。*
