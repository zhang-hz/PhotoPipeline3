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
                 ├ winzip  （15min 预算）：restore Qt(msvc2022_64)/vcpkgwin- → 构建 → make_winzip 五门禁
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
- `release.yml` 的 winzip job **无** ccache restore/save 步（与设计一致，非缺失）；
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

*手册随实况维护；新教训按 §3 表追加行，不删旧行。*
