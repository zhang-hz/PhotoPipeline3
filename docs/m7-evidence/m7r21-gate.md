# m7r21-gate —— R2.1 终门禁全量判据取证（M7-R2.1-GATE / F6）

> 包：**F6 终门禁复核**（2026-10-13）｜门禁树：`build/release-dev`（release-dev preset，
> `-DVCPKG_MANIFEST_INSTALL=OFF`）｜原始日志：`build/m7r21-f6-*.log`（gitignored，本目录为
> 摘录层，两层互补见 [README](README.md)）。本文件按三段式：① 命令行 ② 读数/结果摘录
> ③ 结论与判据。**判据零改动**（只记录既有判据实跑读数 + 修 fallout；本轮 fallout = 0）。
> 对照先例：[m7r2-gate.md](m7r2-gate.md)（R2G，2026-10-12）。

## ① 命令行（逐字可复跑，全部实跑于 2026-10-13）

```bash
# 1) 全源格式（钉版 clang-format 23.1.1，Git bash）
"C:/Program Files/Git/bin/bash.exe" tools/ci-format.sh

# 2) 门禁树构建（configure + 全量编译；复跑幂等核对）
python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- cmake --build build/release-dev      # 复跑：期望 ninja: no work to do.

# 3) tidy 棘轮（只减不增）+ NOLINT 零新增
python tools/env.py run -- python tools/ci-tidy.py build/release-dev
grep -rE "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/      # 期望 0 命中

# 4) ctest 全量 + ui_smoke 冻结行（-V 取逐字输出）
python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure
python tools/env.py run -- ctest --test-dir build/release-dev -R ui_smoke -V

# 5) 金样 20 对
python tools/env.py run -- python tests/golden/smoke.py build/release-dev

# 6) 全语料回归（zero diff；--update 仅在逐行归因后——本轮 zero diff，未动基线）
python tools/env.py run -- python tools/regression.py

# 7) 密钥扫描（docs/m6-report.md §6 口径：git 全历史 + 工作树两面，值形态模式）
python build/m7r21-f6/leak_scan.py        # git log --all -p 全历史（脚本 = R2G build/m7r2-g/leak_scan.py 原样拷贝）
git grep -E "<值形态模式>" -- .           # 工作树 tracked 面（模式同上）

# 8) 冻结行附加核对（--version 单行 / pp_verify 行式）
python tools/env.py run -- build/release-dev/photopipeline.exe --version
python tools/env.py run -- build/release-dev/pp_verify.exe --selftest
```

## ② 读数/结果摘录（关键行逐字；长输出截断注明 `…`）

### 1. ci-format（`build/m7r21-f6-format1.log`）

```
ci-format: check 187 files with clang-format version 23.1.1 (tool: C:/Data/Code/PhotoPipeline3/tools/bin/clang-format-win64.exe) → violations=0
ci-format: OK —— 全源 0 diff（clang-format version 23.1.1，style=file:C:/Data/Code/PhotoPipeline3/.clang-format）
```

（R2G 时点 155 文件；R2/R2.1 拆分新增 TU 后 187 文件，判据口径不变 = 全源 0 diff。）

### 2. 构建（`build/m7r21-f6-configure.log`、`build/m7r21-f6-build1.log`）

- configure：`-- Build files have been written to: C:/Data/Code/PhotoPipeline3/build/release-dev`（0.9s+0.6s）
- build：exit 0（门禁树与 HEAD 树一致，dyndep/链接面全过）；复跑 `ninja: no work to do.`

### 3. ctest 全量 + ui_smoke 冻结行（`build/m7r21-f6-ctest1.log`、`build/m7r21-f6-uis1.log`）

```
100% tests passed out of 35

Total Test time (real) =  46.10 sec
```

```
30: UI-SMOKE OK shots=12 pages=3           # 冻结行逐字不动
30: UI-SMOKE pass
```

探针面（`-V` 全量，坏数全 0）：`shell-geom checked=19 bad=0`、`icons … bad-path=0 bad-bounds=0
bad-svg=0 bad-svg-attr=0`、`theme-tokens-v6 checked=52 bad=0`、`status-selection checked=16/16`、
`pill-tones tones=5 bad=0` 等；第 4 波四节（intake / classify-filter / edit-target /
settings-tabs-light）随 `ui_smoke` 全量通过（ctest 计 35/35 内含）。

### 4. 金样 20 对（`build/m7r21-f6-golden1.log`）

```
case             result   detail
…（20 行全 PASS，逐行 VERIFY <case> OK）…
progress-trace   PASS     VERIFY progress-trace OK
SMOKE total=20 pass=20 fail=0 (out: C:\Data\Code\PhotoPipeline3\.cache\out-smoke)
```

（冻结行逐字：`SMOKE total=20 pass=20 fail=0`。金样/夹具零改动：`git status tests/golden` 空。）

### 5. tidy 棘轮 + NOLINT（`build/m7r21-f6-tidy1.log`）

```
ci-tidy: tool=clang-tidy 21.1.0 @ C:\Data\Code\PhotoPipeline3\.toolchain\venv\Scripts\clang-tidy.EXE
ci-tidy: build_dir=C:\Data\Code\PhotoPipeline3\build\release-dev  TU=112  jobs=16  checks=-*,bugprone-*,performance-*,portability-*
…
ci-tidy:   112/112 TU 完成（累计告警 263，138s）
ci-tidy: 基线记录 platform=windows-msvc compiler=MSVC 14.51.36231 tool=clang-tidy 21.1.0 total=263 files=75 generated_at=2026-10-12T19:48:06Z
ci-tidy: 棘轮通过（75 文件，138s，基线 C:\Data\Code\PhotoPipeline3\tools\baseline\clang-tidy.tsv）
TIDY total=263 baseline=263 files=75 violations=0 seed=0
```

- **棘轮只减不增**：总数 263 = 基线 263（F4 重播种终值），**基线文件零改动**、未用 `--update`。
- **NOLINT 零新增**：`grep -rE "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/` = **0 命中**
  （F4「NOLINT 处置翻案」后仓库零抑制口径保持）。
- 首跑违规才写出的 `tools/baseline/clang-tidy.tsv.candidate.tsv` 本轮未产生；F4 遗留的过期
  同名残件（内容 = 现基线）按 R2G 先例删除，不入库。

### 6. 全语料回归（`build/m7r21-f6-regression1.log`，**首轮即 zero diff**）

```
regression: corpus 27 files (base 16 + edge 4 + meta 7)
regression: out    .cache/regression (wiped)
regression: full-jpeg      rc=4   files=27 ok=23 failed=4 skipped=0 cancelled=0 bytes=14236 …
…（full-jxl/png/tiff/webp/bmp/heif 同型 ok=23 failed=4；full-avif rc=8 ok=19 failed=8；
   meta-jpeg/png/tiff/webp rc=4 files=16 ok=12 failed=4）…
regression: normalized 2448 lines vs baseline 2448 lines
regression: zero diff (baseline reproduced)
```

**失败槽位归因（基线内既有、逐字复现，非本轮新增）**——与 R2G/R2 各轮读数逐项相同：

- 每个 full-* 槽 `failed=4` = `edge/cmyk.tif` + `meta/cmyk.tif`（CMYK 输入，负面清单拒绝解码）
  + `edge/corrupt_trunc.jpg` + `edge/corrupt_zero.png`（损坏夹具，按设计拒绝）；四文件
  `file start` 有、`file done` 无，逐槽与基线一致。
- `full-avif` 另加 `failed=4`：`anim.gif`/`graya8.png`/`rgba16.png`/`rgba8.png` →
  `encode failed: svt-av1 does not support 10-bit with alpha; use backend libaom`（C2 四家
  限定的已知编码器边界，基线内记录）。
- 每个 meta-* 槽 `failed=4` = `metadata-only rewrite failed: exiv2: Writing to GIF/BMP/TGA
  images is not supported` + `Setting Exif metadata in BMFF images is not supported`
  （Exiv2 只读面的已知矩阵，基线内记录）。

结论：**`regression: zero diff (baseline reproduced)`，未触发 `--update`**，`tools/baseline/
golden.windows.log` 零改动。三前提未放宽（workers=1 / 输出目录先清空 / 整体幂等）。
R2.1 窗口（F1–F4）欠跑的全语料回归由本轮补跑闭合（此前各包 handoff ⑥ 与报告 §4.3 的
「未跑」注记随本轮销账）。

### 7. 密钥扫描（`build/m7r21-f6-leak-history.log` 全历史 / `build/m7r21-f6-leak-worktree.log` 工作树）

```
total lines: 287990            # git log --all -p 全历史（2026-10-13 实况）
sk-key: 0                      # sk-[A-Za-z0-9]{16,}
amap-value: 0                  # amap_key\s*[=:]\s*["']?[A-Za-z0-9]{16,}
hex32: 0                       # \b[0-9a-fA-F]{32}\b
amap-key-name: 242             # 仅键名/字段名/文档（样例：st->gps.map->set_amap_key(amap_key);）
```

```
git grep worktree (tracked) hits:
  pattern [sk-[A-Za-z0-9]{16,}] files=0 lines=0
  pattern [amap_key\s*[=:]\s*["']?[A-Za-z0-9]{16,}] files=0 lines=0
  pattern [\b[0-9a-fA-F]{32}\b] files=0 lines=0
  amap-key-name: 25 files / 110 lines    # .gitignore 规则名 / CHANGELOG / settings 字段名 / 文档
```

值形态模式两面 **0 命中**；`amap_key` 名字面命中全为键名/字段名/文档（口径同
`docs/m6-report.md` §6；样例逐条抽查过：`.gitignore:20`、`CHANGELOG.md:24`、
`docs/m1b-tasks.md:95` 等）。

### 8. 冻结行附加核对（`build/m7r21-f6-version.txt`、pp_verify 自检输出）

```
PhotoPipeline 0.3.3                # --version 单行输出，格式逐字不动
VERIFY selftest OK (39 probes, 0 unexpected)   # pp_verify 行式逐字不动（ctest verify_selftest 同过）
```

## ③ 结论与判据

| # | 判据 | 终值 | 过/不过 |
|---|---|---|---|
| 1 | `ci-format.sh` 全源 0 diff | `violations=0`（187 文件） | **过** |
| 2 | 门禁树构建（release-dev 全量编译） | exit 0；复跑 `ninja: no work to do.` | **过** |
| 3 | ctest 全绿（release-dev） | `100% tests passed out of 35`（46.10s） | **过** |
| 3b | ui_smoke 冻结行 | `UI-SMOKE OK shots=12 pages=3` 逐字 | **过** |
| 4 | 金样 20 对 | `SMOKE total=20 pass=20 fail=0` 逐字 | **过** |
| 5 | tidy 棘轮只减不增 + 零 NOLINT 新增 | `TIDY total=263 baseline=263 violations=0 seed=0`；NOLINT 指令 grep 0 命中（基线文件零改动） | **过** |
| 6 | 回归 zero diff | `regression: zero diff (baseline reproduced)`（2448 vs 2448，首轮） | **过** |
| 7 | 密钥 0 命中 | 值形态 sk/amap-value/hex32 两面 = 0 | **过** |

**fallout = 0**：八项判据首轮全绿，无修复、无换锚、无 `--update` 重播种、无裁定新增。
冻结面（`tests/golden/`、`tools/baseline/*.log`、`SMOKE`/`UI-SMOKE`/`--version`/`VERIFY` 冻结行）
零改动（`git status` 核对：除本包 docs 与 handoff 外工作树净）。

**未覆盖/未决**：本门禁面全绿之外的登记项不在本文件范围——见 `docs/m7-report.md` §4 与
§7.5（R2.1 验收结论与残余）。
