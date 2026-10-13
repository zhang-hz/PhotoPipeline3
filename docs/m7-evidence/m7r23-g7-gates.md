# m7r23-g7-gates.md — R2.3-G7「winzip 预算工程解」证据摘录

> 归属：M7-R2.3-G7（2026-10-13）。原始日志 = `build/m7r23-g7/`（gitignored：`local-winzip.log`、
> `run57-jobs.json`、`run58-jobs2.json`、`runs.json` 等）；本文件只留**命令行 + 读数摘录 + 结论**
> （三段式，`docs/m7-evidence/README.md` 规矩）。CI 读数来源两处：① G7 会话按
> `docs/m7-evidence/release-runbook.md` §4 公共 API 命令抓取的 JSON；② G9 收口时（2026-10-13）
> 公开 API 复取（`api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs`，免鉴权）。

## ① 本地验证（G7 会话实跑，摘自 `build/m7r23-g7/handoff.md` ④）

| 判据 | 命令 | 结果 |
|---|---|---|
| YAML 结构 | `python` + `yaml.safe_load` 双 workflow + job/needs/timeout 图 | 绿：main-full 10 jobs（winzip needs=[windows, windows-dev]）、release 7 jobs（winzip-build∥winzip-build-dev→winzip-pack→probe→publish）；timeout-minutes 全 20 |
| 不变量脚本 | needs 图 / 双 artifact 链 / 打包 job 无 cmake / 判据步骤盘点 / windows 无 dev 残留 / budget 键覆盖 | 无 FAIL（五门禁 + E-2 + SMOKE 冻结行 + ui_smoke + 暖度断言 + 语料断言一个不少） |
| PowerShell 语法 | `Parser::ParseFile` 全部 61 个 run 块（UTF-8 BOM） | parse OK ×61 |
| 五门禁 + E-2 等效验证 | `python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe` | **rc=0**，逐字读数见下 |
| 格式 | `bash tools/ci-format.sh`（HEAD 独立 worktree） | 196 files violations=0（G7 零 C/C++ 改动） |

make_winzip 逐步读数（`build/m7r23-g7/local-winzip.log` 逐字）：

```
make_winzip: 闭包门禁 A: 68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37
make_winzip: 来源门禁 B: 5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致（…\.toolchain\Qt\6.11.2\msvc2022_64\bin）
make_winzip: 结构断言 C: photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备
make_winzip: 烟测 E-1: PhotoPipeline 0.3.3 (exit 0)
make_winzip: 烟测 E-2: UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)
make_winzip: 产物: dist\PhotoPipeline-0.3.3-win64.zip (34801005 bytes, sha256=e4ff16f4…, 条目 105)
```

（断言 D 许可 ≥30 port 与产物内复核 35 copyright 在同日志前段/后段，门禁 rc=0 已含；`tools/
make_winzip.py` 零改动——`PP_BUILD_DIR`/`--smoke-exe` 输入面与 artifact 布局天然兼容。）

## ② CI 实证（main-full 三轮，run 号与逐 job 读数）

| run | 触发提交 | run 级 | 要点 |
|---|---|---|---|
| **#57**（id 38346237124） | `83a9cb5`（一轮：产物复用） | **cancelled** | windows job 20m28s「exceeded 20m0s」；取消点 step 13「configure release-dev + build 金样子集」（step 级：prep 57s + step12 build+ctest 872s + step13 272s 在建撞顶）；winzip skipped ⇒ 二轮拆段归因依据 |
| **#58**（id 38350019547） | `fa871c8`（二轮：release-dev 段拆独立 job） | **success（10/10 job 全绿）** | 逐 job：lint 9s / tidy 18m34s / linux 11m01s / windows 11m39s / **windows-dev 6m14s** / appimage 4m39s / **winzip 1m37s**（make_winzip 步 48s 含五门禁+E-2）/ audit 1m15s / cache-gc 5s / gate 8s；job 级 wall **19m03s**（05:10:10→05:29:13）≤ 20min 硬顶 |
| **#59**（id 38352494135） | `fe3b43f`（runbook 增补） | **success（10/10 job 全绿）** | G9 于 2026-10-13 公开 API 复取：lint 9s / appimage 4m25s / linux 9m46s / tidy 18m33s / windows-dev 7m39s / windows 14m38s / audit 1m15s / **winzip 1m29s** / cache-gc 7s / gate 6s；job 级 wall **19m01s**（05:39:15→05:58:16）——**全树（含 G8 四提交）复证 per-job ≤20min** |

轮询命令（runbook §4，公开仓库免鉴权）：

```
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs?per_page=6"
curl -s "https://api.github.com/repos/zhang-hz/PhotoPipeline3/actions/runs/38352494135/jobs"
```

## ③ 结论与判据

- **M7R23G7-0 执行口径**：产物复用/拆 job，**CI 判据零改动**——timeout-minutes 全部维持 20、
  五门禁 + E-2 `--smoke-exe` + SMOKE 冻结行 + ui_smoke + vcpkg 暖度断言逐步逐字保留，
  `tools/make_winzip.py` 零改动（「仅参数/输入面小改」授权未消耗）。
- **per-job ≤20min 达成**：#58/#59 双轮实证，最紧 job = windows 14m38s（#59）/11m39s（#58），
  winzip 侧 89–97s。**整轮 wall 余量是 runner 波动的函数**（19m01s/19m03s；#57 同代码段
  windows step 12 曾慢 43%）——如实登记，不承诺整轮余量。
- **release.yml 三段（winzip-build / winzip-build-dev / winzip-pack）无 tag 轮实测**：只有
  预算值（15/7/8），发行轮首跑按 runbook §4 补读数（不编数）。
