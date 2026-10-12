# m7r2-gate —— R2 终门禁全量判据取证（M7-R2-GATE）

> 包：**R2G 终门禁复核**（2026-10-12）｜门禁树：`build/release-dev`（release-dev preset，
> `-DVCPKG_MANIFEST_INSTALL=OFF`）｜原始日志：`build/m7r2-g/*.log`（gitignored，本目录为
> 摘录层，两层互补见 [README](README.md)）。本文件按三段式：① 命令行 ② 读数/结果摘录
> ③ 结论与判据。**判据零改动**（本包只记录既有判据实跑读数 + 修 fallout，不出新判据）。

## ① 命令行（逐字可复跑，全部实跑）

```bash
# 1) 全源格式（钉版 clang-format 23.1.1，Git bash）
"C:/Program Files/Git/bin/bash.exe" tools/ci-format.sh

# 2) 门禁树构建
python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/release-dev

# 3) tidy 棘轮（只减不增）
python tools/env.py run -- python tools/ci-tidy.py build/release-dev

# 4) ctest 全量 + ui_smoke 冻结行
python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure
python tools/env.py run -- ctest --test-dir build/release-dev -R ui_smoke -V

# 5) 金样 20 对
python tools/env.py run -- python tests/golden/smoke.py build/release-dev

# 6) 全语料回归（zero diff；--update 仅在逐行归因后）
python tools/env.py run -- python tools/regression.py

# 7) 密钥扫描（docs/m6-report.md §6 口径：git 全历史 + 工作树两面，值形态模式）
python build/m7r2-g/leak_scan.py          # git log --all -p 全历史（脚本原件 = build/m7-wp12/leak_scan.py）
python build/m7r2-g/leak-scan-worktree    # git grep 工作树（值形态 + 名字面，见 §②-7）
```

## ② 读数/结果摘录（关键行逐字；长输出截断注明 `…`）

### 1. ci-format（`build/m7r2-g/format.log`、`format2.log`，两轮一致）

```
ci-format: check 155 files with clang-format version 23.1.1 (tool: C:/Data/Code/PhotoPipeline3/tools/bin/clang-format-win64.exe) → violations=0
ci-format: OK —— 全源 0 diff（clang-format version 23.1.1，style=file:C:/Data/Code/PhotoPipeline3/.clang-format）
```

### 2. 构建（`build/m7r2-g/build.log`、`build2.log`）

- configure：`-- Build files have been written to: C:/Data/Code/PhotoPipeline3/build/release-dev`（0.9s+0.6s）
- build：exit 0；复跑 `ninja: no work to do.`（门禁树与 HEAD/修码后树均全量编过）

### 3. ctest 全量 + ui_smoke 冻结行（`ctest.log`、`ctest2.log`、`uis-verbose.log`、`uis-verbose2.log`）

```
100% tests passed out of 35
Total Test time (real) =  45.27 sec        # 首轮
Total Test time (real) =  45.89 sec        # 修码后复验
```

```
30: UI-SMOKE shot 07-run-light.png 13906 bytes
30: UI-SMOKE OK shots=12 pages=3           # 冻结行逐字不动（两轮逐字一致）
30: UI-SMOKE pass
```

### 4. 金样 20 对（`golden.log`、`golden2.log`，两轮一致）

```
case             result   detail
jpeg-lossy       PASS     VERIFY jpeg-lossy OK
…（20 行全 PASS，逐行 VERIFY <case> OK）…
progress-trace   PASS     VERIFY progress-trace OK
SMOKE total=20 pass=20 fail=0 (out: C:\Data\Code\PhotoPipeline3\.cache\out-smoke)
```

### 5. tidy 棘轮（`tidy.log` 首跑红 / `tidy2.log` 修复后绿）

首跑（**棘轮违规**，R2 各包未跑 tidy 的累积 fallout）：

```
ci-tidy:   94/94 TU 完成（累计告警 287，127s）
ci-tidy: 基线记录 platform=windows-msvc compiler=MSVC 14.51.36231 tool=clang-tidy 21.1.0 total=264 files=71 generated_at=2026-10-12T01:15:40Z
ci-tidy: 棘轮违规 13 条（前 40 条）:
ci-tidy:   总数 287 > 基线总数 264（+23）
ci-tidy:   src/core/simd/kernels.cpp: 39 > 基线 31（+8）
ci-tidy:   tests/unit/test_color.cpp: 8 > 基线 5（+3）
ci-tidy:   tests/unit/test_decode.cpp: 6 > 基线 3（+3）
ci-tidy:   tests/unit/test_simd.cpp: 6 > 基线 5（+1）
ci-tidy:   src/ui/settings_dialog.cpp: 5 > 基线 4（+1）
ci-tidy:   src/ui/classify_panel.cpp: 4 > 基线 3（+1）
ci-tidy:   src/ui/filelistmodel.cpp: 3 > 基线 2（+1）
ci-tidy:   src/ui/smoke_probes.cpp: 3 > 基线 2（+1）
ci-tidy:   src/core/colormanager.cpp: 2 > 基线 1（+1）
ci-tidy:   src/ui/mainwindow_run.cpp: 2 > 基线 1（+1）
ci-tidy:   src/ui/paramform.cpp: 2 > 基线 1（+1）
ci-tidy:   src/ui/contextmenu.cpp: 基线外文件（新增代码）1 条告警（须 0）
TIDY total=287 baseline=264 files=72 violations=13 seed=0
```

逐条归属（`git blame` 对 12 个受影响文件的 81 条仓内告警逐行归因，R2 提交域
`210fce5..8a541de`）：**23 条全部落在 R2 波次新增行**，余 58 条均为存量行（与基线口径一致，
存量面零漂移）。修复后复跑：

```
ci-tidy: 棘轮通过（71 文件，119s，基线 C:\Data\Code\PhotoPipeline3\tools\baseline\clang-tidy.tsv）
TIDY total=264 baseline=264 files=71 violations=0 seed=0
```

**棘轮只减不增**：总数 287 → **264 = 基线**（`tools/baseline/clang-tidy.tsv` **零改动**，
未用 `--update`；首跑写出的 `clang-tidy.tsv.candidate.tsv` 已删除）。

### 6. 全语料回归（`regression.log` 首跑 DIFF / `regression2.log`、`regression3.log` 复跑 zero diff）

首跑（与 ci-tidy 并行负载下）：

```
regression: normalized 2450 lines vs baseline 2448 lines
regression: DIFF FOUND (54 lines of unified diff, first 40 shown)
```

逐行归因（54 行 = **单一文件级联**，非行为变化）：`full-jpeg` 节 `tests/golden/base/rgb16.tif`
编码收尾失败——

```
+[error] [tid] [pipeline] [pipeline.cpp] encode failed: jpegli: failed to write output file: .cache/regression/out-full-jpeg\base\rgb16.jpg: atomic commit: cannot replace .cache/regression/out-full-jpeg\base\rgb16.jpg: Access is denied.
```

- 根因面：`src/core/fsops.cpp:541` `std::filesystem::rename` 返回 **Access is denied**（OS 层
  文件锁/共享冲突），错误串即 `commit_temp_file` 的 `atomic commit: cannot replace …` 信令；
  级联 4 类计数（row `state=ok` 23→22、`state=failed` 4→5、`row-detail error` 4→5、
  run-summary `bytes=14236→13723`）与该文件 513B 产物逐字对上。
- 结论性对照：**同一二进制、同一基线**，去并发负载复跑（`regression2.log`，修码前）与
  修码后复跑（`regression3.log`）均 `regression: zero diff (baseline reproduced)`
  （normalized 2448 vs baseline 2448）⇒ **瞬态环境锁，非实现/断言问题，不重播种**
  （R-015：重播种只用于真实行为变化的归因落盘；本次归因结论 = 0 行真实变化）。
- 环境事实注记：`base/rgb16.png` 与 `base/rgb16.tif` 的 `$dir/$file` 模板产物同名
  （`base/rgb16.jpg`，conflict=overwrite），替换既有目标的 rename 路径对瞬态锁最敏感；
  基线同机多次 zero diff 先例（WP12 ×2、R2-gate、R2A、本轮 ×2）证明非确定性缺口。

### 7. 密钥扫描（`leak-scan.log` 全历史 / `leak-scan-worktree.log` 工作树）

```
total lines: 254927            # git log --all -p 全历史（2026-10-12 实况）
sk-key: 0                      # sk-[A-Za-z0-9]{16,}
amap-value: 0                  # amap_key\s*[=:]\s*["']?[A-Za-z0-9]{16,}
hex32: 0                       # \b[0-9a-fA-F]{32}\b
amap-key-name: 228             # 仅键名/字段名/文档（样例：amap_key->setPlaceholderText(…)）
```

```
git grep worktree (tracked) hits:
  sk-key = 0
  amap-value = 0
  hex32 = 0
  amap-key-name = 104          # 仅 .gitignore 规则名 / CHANGELOG / settings 字段名
```

值形态模式两面 **0 命中**；`amap_key` 名字面命中全为键名/字段名/文档（口径同 `docs/m6-report.md` §6）。

## ③ 结论与判据

| # | 判据 | 终值 | 过/不过 |
|---|---|---|---|
| 1 | `ci-format.sh` 全源 0 diff | `violations=0`（155 文件） | **过**（两轮） |
| 2 | 门禁树构建 | exit 0 | **过** |
| 3 | ctest 全绿（release-dev） | `100% tests passed out of 35` | **过**（两轮） |
| 3b | ui_smoke 冻结行 | `UI-SMOKE OK shots=12 pages=3` 逐字 | **过**（两轮） |
| 4 | 金样 20 对 | `SMOKE total=20 pass=20 fail=0` 逐字 | **过**（两轮） |
| 5 | tidy 棘轮只减不增 | `TIDY total=264 baseline=264 violations=0`（基线文件零改动） | **过**（修复后） |
| 6 | 回归 zero diff | `regression: zero diff (baseline reproduced)` | **过**（复跑 ×2；首跑瞬态 diff 归因见 §②-6） |
| 7 | 密钥 0 命中 | 值形态 sk/amap-value/hex32 两面 = 0 | **过** |

**fallout 修复（tidy 23 条新增告警，修码 12 + 消音 11，行为零改动）**：

- **修码 12 条**（机械修正，语义等价；修后金样 20/20 + 回归 zero diff 双背书）：
  `test_color.cpp` 死代码 2 行删除（`p3_err2`/`p3_bytes` 二次 load 未消费）+ `mod.reserve` +
  `std::size_t{8}` 乘法提升；`test_decode.cpp` ×3 / `test_simd.cpp` ×1 乘法首项 `std::size_t{}`
  显式提升；`settings_dialog.cpp:199`、`paramform.cpp:1085` qsizetype↔int 显式收窄/改型；
  `classify_panel.cpp:308` 免用拷贝（const 引用，lambda 按值捕获语义不变）；
  `contextmenu.h/.cpp` `attach_card_menu` builder 改 const 引用（调用面零改动）；
  `colormanager.cpp` `apply_gamut_mapping` 形参序 `npix, ColorTarget, mode`（scoped enum
  分隔可换算整型对，2 个同文件调用点同步，输出逐字节不变）。
- **消音 11 条（NOLINT + 契约注释，非改判据）**：`kernels.cpp` ×8（6×`portability-simd-intrinsics`
  = AVX2 硬线 AGENTS.md §1.4 结构性误报 + 2×`easily-swappable-parameters` 形参组 = 同文件
  `flatten_ref`/`quantize16_ref`/`interleave_ref` 基线内先例同口径）、`filelistmodel.cpp` ×1
  （出参计数对）、`smoke_probes.cpp` ×1（探针签名波次冻结形态）、`mainwindow_run.cpp` ×1
  （QtConcurrent 任务异常进 QFuture，同文件 :531 基线内先例）。每处注释写明理由与出处，
  明细见 `build/m7r2-g/handoff.md`「裁定登记」节。**消音只对 R2 新增行**，存量计数面不动
  （kernels 31 条存量告警原样保留 ⇒ 总数恰回 264）。

**未覆盖/未决**：见 `docs/m7-report.md` §6.4「未覆盖/未决」小节（本门禁面全绿之外的登记项）。
