# m7r22-gate —— R2.2 终门禁全量判据取证（M7-R2.2-GATE / G5）

> 包：**G5 终门禁复核**（2026-10-13）｜门禁树：`build/release-dev`（release-dev preset，
> `-DVCPKG_MANIFEST_INSTALL=OFF`）｜原始日志：`build/m7r22-g5-*.log`（gitignored，本目录为
> 摘录层，两层互补见 [README](README.md)）。本文件按三段式：① 命令行 ② 读数/结果摘录
> ③ 结论与判据。**判据零改动**（只记录既有判据实跑读数 + 修 fallout + 授权范围内的
> tidy 棘轮重播种）。对照先例：[m7r21-gate.md](m7r21-gate.md)（R2.1-F6，2026-10-13）、
> [m7r2-gate.md](m7r2-gate.md)（R2G，2026-10-12）。

## ① 命令行（逐字可复跑，全部实跑于 2026-10-13）

```bash
# 1) 全源格式（钉版 clang-format 23.1.1）
bash tools/ci-format.sh

# 2) 门禁树构建（configure + 全量编译；复跑幂等核对）
python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- cmake --build build/release-dev      # 复跑：期望 ninja: no work to do.

# 3) tidy 棘轮 + NOLINT 零新增（收口重播种一节见 ③-3）
python tools/env.py run -- python tools/ci-tidy.py build/release-dev
python tools/env.py run -- python tools/ci-tidy.py build/release-dev --update   # 收口重播种（授权，见 ③-3）
grep -rEn "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/                     # 期望 0 命中

# 4) ctest 全量 + ui_smoke 冻结行
python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure
python tools/env.py run -- python tests/ui_smoke.py build/release-dev

# 5) 金样 20 对
python tools/env.py run -- python tests/golden/smoke.py build/release-dev

# 6) 全语料回归（zero diff；本轮未 --update——见 ③-6 归因）
python tools/env.py run -- python tools/regression.py

# 7) 密钥扫描（docs/m6-report.md §6 口径：git 全历史 + 工作树两面，值形态模式）
python build/m7r22-g5/leak_scan.py       # git log --all -p 全历史（脚本 = R2G/F6 先例原样拷贝）
python build/m7r22-g5/worktree_scan.py   # git grep 值形态模式（tracked 面）

# 8) 冻结行附加核对（--version 单行 / pp_verify 行式）
python tools/env.py run -- build/release-dev/photopipeline.exe --version
python tools/env.py run -- build/release-dev/pp_verify.exe --selftest
```

## ② 读数/结果摘录（关键行逐字；长输出截断注明 `…`）

### 1. ci-format（`build/m7r22-g5-format4.log`，终态复跑）

```
ci-format: check 196 files with clang-format version 23.1.1 (tool: C:/Data/Code/PhotoPipeline3/tools/bin/clang-format-win64.exe) → violations=0
ci-format: OK —— 全源 0 diff（clang-format version 23.1.1，style=file:C:/Data/Code/PhotoPipeline3/.clang-format）
```

（R2.1 终门禁 187 文件 → R2.2 拆 TU/新测试后 **196 文件**；判据口径不变 = 全源 0 diff。）

### 2. 构建（`build/m7r22-g5-configure.log`、`build/m7r22-g5-build1.log`、`…-build4.log`）

- configure：`-- Build files have been written to: C:/Data/Code/PhotoPipeline3/build/release-dev`（0.9s+0.7s）
- 全量编译 exit 0；复跑幂等：`[0/2] Re-checking globbed directories...` / `ninja: no work to do.`
- 门禁树与 HEAD 源态一致（ninja 无补编）；G5 的 fallout 修复 4 文件（③-3）随 build3/build4 增量重编后
  `photopipeline.exe` 重新链接，测试三判据（ctest/金样/回归/ui_smoke）全部重跑于**修复后终态树**
  （`…-ctest-final2.log`、`…-golden2.log`、`…-regression-final.log`、`…-uis-final2.log`）。

### 3. tidy 棘轮（收口全程四跑，③-3 归因）

```
# 首跑（fallout 面，build/m7r22-g5-tidy1.log）
ci-tidy: 棘轮违规 5 条（前 40 条）:
ci-tidy:   总数 267 > 基线总数 263（+4）
ci-tidy:   tests/unit/test_meta_format.cpp: 基线外文件（新增代码）3 条告警（须 0）
ci-tidy:   src/mapwidget/geocode_client.cpp: 基线外文件（新增代码）1 条告警（须 0）
ci-tidy:   src/mapwidget/tile_fetcher.cpp: 基线外文件（新增代码）1 条告警（须 0）
ci-tidy:   tests/unit/test_gcj02.cpp: 基线外文件（新增代码）1 条告警（须 0）
TIDY total=267 baseline=263 files=79 violations=5 seed=0

# 修 5 条后复跑（build/m7r22-g5-tidy2.log）
TIDY total=262 baseline=263 files=76 violations=1 seed=0     # 余 1 = test_meta_format 接线型（③-3 登记）

# 收口重播种（build/m7r22-g5-tidy-update.log；M7-ADJ-tidy-reseed 口径）
TIDY-UPDATE total=262 files=76 baseline=…tools/baseline/clang-tidy.tsv（基线已重写；diff 必须随本次提交评审）

# 终验复跑（build/m7r22-g5-tidy-final.log / …-tidy-final2.log）
ci-tidy: 棘轮通过（76 文件，146s，基线 …tools/baseline/clang-tidy.tsv）
TIDY total=262 baseline=262 files=76 violations=0 seed=0
```

NOLINT 零新增：`grep -rEn "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/` → **0 命中**（exit 1）。

### 4. ctest 全量 + ui_smoke 冻结行（`build/m7r22-g5-ctest-final2.log`、`…-uis-final2.log`）

```
100% tests passed out of 36

Total Test time (real) =  48.02 sec
```

```
UI-SMOKE OK shots=12 pages=3           # 冻结行逐字不动
UI-SMOKE pass
```

G1 探针行（终态全绿）：`rtl-mirror checked=18 bad=0 stars=0 hud=0 align=0 geom=0`、
`check-all checked=13 bad=0`、`context-menu cards=12 checked=60 bad=0`、
`settings-tabs-light checked=4 bad=0 pp-tab-general mode=#1b1c20 est=#1b1c20 t=1.00 resid=0 …`、
`format-roundtrip checked=15 bad=0 x=webp y=jxl off-active=webp front=jpeg`、
`rtl-deep checked=4 bad=0 cols=96`、`color-options checked=6 bad=0`、
`zero-byte-redo checked=7 bad=0 outputs=26 a=598`、`failure-list checked=7 bad=0 lines=1`。
（36 = R2.1 期 35 + G2 新增 `test_meta_format`；ui_smoke 以 ctest 条目与
`tests/ui_smoke.py` 双通道各跑一遍均绿。）

### 5. 金样 20 对（`build/m7r22-g5-golden2.log`）

```
SMOKE total=20 pass=20 fail=0 (out: C:\Data\Code\PhotoPipeline3\.cache\out-smoke)
```

### 6. 全语料回归（`build/m7r22-g5-regression-final.log`）

```
regression: corpus 27 files (base 16 + edge 4 + meta 7)
…
regression: normalized 2448 lines vs baseline 2448 lines
regression: zero diff (baseline reproduced)
```

**归因（任务书注「R-197 行为变化 diff 预期存在」，实测未出现）**：R-197（M7R22G2-0）的换锚面 =
`exif_editor` 的 `TriMode::Clear`（GUI 例外编辑，TagEdit remove），`--dev` 语料回归不经过
exif_editor 路径——G2 ④ 已给同结论（实测 zero diff、无需重播种），本包 HEAD 终态复跑再次印证
（2448 vs 2448）。故**无 diff 可归因、未动回归基线**（`tools/baseline/golden.windows.log` 零改动）。
meta-* 槽位 `failed=4`/`avif failed=8` 与基线一致（Exiv2 只读面既有失败，槽位未变）。

### 7. 密钥扫描（`build/m7r22-g5-leak-history.log` 全历史 / `…-leak-worktree.log` 工作树）

```
total lines: 297861
sk-key: 0                      # sk-[A-Za-z0-9]{16,}
amap-value: 0                  # amap_key\s*[=:]\s*["']?[A-Za-z0-9]{16,}
hex32: 0                       # \b[0-9a-fA-F]{32}\b
amap-key-name: 280             # 仅键名/字段名/文档（样例：auto *amap_key = new QLineEdit(page_map);）

git grep worktree (tracked) hits:
  pattern [sk-key] files=0 lines=0
  pattern [amap-value] files=0 lines=0
  pattern [hex32] files=0 lines=0
  pattern [amap-key-name] files=29 lines=122    # .gitignore 规则名 / CHANGELOG / settings 字段名 / 文档
```

值形态模式两面 **0 命中**；`amap_key` 名字面命中全为键名/字段名/文档（口径同
[m7r21-gate.md](m7r21-gate.md) ⑦-2）。

### 8. 冻结行附加核对（`build/m7r22-g5-version.log`、`…-ppverify.log`）

```
PhotoPipeline 0.3.3            # --version 单行输出格式逐字不动
…
VERIFY selftest OK (39 probes, 0 unexpected)   # pp_verify 的 VERIFY 行式逐字不动
```

## ③ 结论与判据

### 3.1 八项判据对照（全过）

| # | 判据 | 终值 | 过/不过 |
|---|---|---|---|
| 1 | ci-format 全源 0 diff | 196 文件 violations=0 | **过** |
| 2 | release-dev 全量编译 + 幂等 | exit 0 / `ninja: no work to do.` | **过** |
| 3 | ci-tidy 棘轮只减不增 + NOLINT 零新增 | `TIDY total=262 baseline=262 files=76 violations=0 seed=0`；NOLINT 0 命中 | **过** |
| 4 | ctest 全绿 + ui_smoke 冻结行 | 36/36；`UI-SMOKE OK shots=12 pages=3` 逐字 | **过** |
| 5 | 金样 20 对 | `SMOKE total=20 pass=20 fail=0` 逐字 | **过** |
| 6 | 全语料回归 | `regression: zero diff (baseline reproduced)`（2448/2448，未 --update） | **过** |
| 7 | 密钥扫描 0 命中 | 值形态 sk/amap-value/hex32 两面 = 0 | **过** |
| 8 | 冻结行附加（--version / pp_verify） | 单行 `PhotoPipeline 0.3.3`；`VERIFY selftest OK (39 probes, 0 unexpected)` | **过** |

### 3.2 fallout 修复（R2.2 新增代码 6 条告警 → 修 5 / 登记 1）

| 文件:行 | 告警 | 处置 | 零行为依据 |
|---|---|---|---|
| `tests/unit/test_meta_format.cpp:126` | bugprone-implicit-widening-of-multiplication-result | **修**：`1024*1024` → `1024LL*1024`（同 idiom 连带 :128） | 值逐位相同（1048576/1048575） |
| `tests/unit/test_meta_format.cpp:55` | performance-enum-size | **修**：测试侧复述枚举 `InfoSlotCopy : int → std::uint8_t` | 纯测试侧词表复述；值集 0…8 不变；产品侧 `meta_state.h` `InfoSlot : int` **不动** |
| `tests/unit/test_meta_format.cpp:19` | bugprone-suspicious-include | **登记**（逐文件放宽登记，见 3.3） | 与 `tests/unit/test_icons.cpp` 基线内 1 条**同型同因**（`#include "ui/….cpp"` 单实现接线，M3 铁律六） |
| `src/mapwidget/geocode_client.cpp:199` | performance-unnecessary-copy-initialization | **修**：`fallback_query` 副本 → const 引用别名 | lambda 仍按值捕获（拷贝一份），语义不变 |
| `src/mapwidget/tile_fetcher.cpp:121` | performance-unnecessary-copy-initialization | **修**：`entry` 拷贝 → `std::move(*it)` 后 erase | 被搬元素随即 erase 析构，可观测同拷贝 |
| `tests/unit/test_gcj02.cpp:69` | bugprone-exception-escape（main） | **修**：主体改 `run_cases` + main 包 try/catch | test_icons / WP12-B2 先例：异常路径返回 1，断言与成功路径逐字不动 |

### 3.3 tidy 棘轮收口（授权 M7R22G2-0 衍生；M7-ADJ-tidy-reseed 口径，逐条归因）

基线 `tools/baseline/clang-tidy.tsv`：**263/75 → 262/76**（`TIDY-UPDATE total=262 files=76`）。

| 逐文件变动 | 基线 → 终值 | 归因 |
|---|---|---|
| `tests/unit/test_meta_format.cpp` | （无）→ **1（+1，逐文件放宽登记）** | bugprone-suspicious-include = G2 M7R22G2-2 单实现接线（`#define PP_META_FORMAT_TEST_NO_WIDGETS` + `#include "ui/meta_format.cpp"`）的结构性告警；与基线内 `tests/unit/test_icons.cpp=1` **同型同因**（test_icons 同款接线先例）。**替代修法在案**：CMake 目标级接线（`target_sources` + `target_compile_definitions`）可两处同清零；发行门禁期不动测试接线（登记交后续，见 handoff ⑥） |
| `src/mapwidget/mapwidget.cpp` | 4 → **3（−1，收紧）** | R2.2 G2 B.5③⑤（`847a83d`/`4c81bd0`）拆分随代码搬出一条；搬入新 TU 的 unnecessary-copy ×2 已随 3.2 修掉归零 |
| `src/ui/classify_panel.cpp` | 3 → **2（−1，收紧）** | R2.2 G2 R-183 后半（`7541683`）行部件复用级解耦消除一条 |
| `tests/unit/test_meta_format.cpp` 其余 2 条 | 3 → 1（−2，修） | 见 3.2 |
| `src/mapwidget/geocode_client.cpp`/`tile_fetcher.cpp`/`tests/unit/test_gcj02.cpp` | 各 1 → **0（修）** | 见 3.2；基线外文件回零 = 「新增代码 0 告警」恢复成立 |

**F4 登记待回收三组（M7R21F4-0-1/-10）维持登记，逐条归因**：

| 组 | 明细（终值） | 归因 / 不回收理由 |
|---|---|---|
| page_run +1 | `src/ui/page_run.cpp=6`（5+1） | +1 = `:1127` `refresh_overview_progress` bugprone-easily-swappable-parameters（R2.1-F3 `0a6655da` 行控件束单源新增）；修法 = 参数组结构化/签名重排，沿 WP12「发行期不做签名重构」口径维持登记 |
| exif_editor +1 | `src/ui/exif_editor.cpp=3`（2+1） | +1 = `:1931` QtConcurrent 任务 lambda bugprone-exception-escape（R-081 异步化，M7R21F4-10 登记）；修法 = try/catch 吞异常 = 行为变化，沿 WP12-B2/M7R21F4-0 口径不采 |
| F2/F3 拆 TU 机械迁移 19 条 | meta_state 7 / meta_format 4 / meta_card_privacy 3 / filegroup_proxy 2 / map_view 1 / output_page_build 1 / output_template_ui 1 | 内容 = easily-swappable ×8 + narrowing ×4 + no-automatic-move ×3 + empty-catch ×2 + misplaced-widening ×2（本包 dump 核对）；empty-catch 无零行为修法、swappable 需签名重构，余为机械修法面——登记待后续批量收敛（同 M7R21F4-0-1 口径） |

**M7-ADJ-tidy-reseed 五条件逐条核对**：① 总量只减（263 → **262**，−1）✓；② 放宽逐条理由 + 登记
（唯一放宽 = test_meta_format 0→1，上表）✓；③ 归因随提交正文（`M7-R2.2-GATE` 提交正文）✓；
④ 复跑 exit 0 + 读数入报告（`TIDY total=262 baseline=262 files=76 violations=0 seed=0`）✓；
⑤ 不动 CI 判据与检查集、不加抑制（fingerprint `windows-msvc|clang-tidy 21.1.0|-*,bugprone-*,
performance-*,portability-*` 不变；NOLINT 0 命中）✓。

### 3.4 结论

R2.2 轮门禁终态：**八项判据全绿**（③-1）。冻结行逐字不动（`SMOKE total=20 pass=20 fail=0`、
`UI-SMOKE OK shots=12 pages=3`、`--version` 单行、`pp_verify` VERIFY 行式）；金样语料与夹具
零改动；回归基线零改动（zero diff）；NOLINT 零新增。tidy 棘轮经授权收口（③-3）：总量 263→262
只减不增，唯一放宽 `test_meta_format=1` 已登记（同型同因 + 替代修法在案）。
