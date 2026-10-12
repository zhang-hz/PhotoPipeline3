# m7r21-f4-gates —— R2.1 F4「跨面包收口」门禁读数（NOLINT 翻案 / 色彩 GUI / 探针批 / 跨界小项）

> 包：M7-R2.1-F4（提交 `dcedf4c`/`7b89cc3`/`93001a9`/`6ff43f9`/`d737cb4`/`ca96896`/`81d0b34`）｜
> 日期：2026-10-13｜原始日志：`build/m7r21-f4-*.log`（gitignored）。摘录人：F5
> （2026-10-13，自 F4 handoff ④ 与原始日志逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r21-f4 -DVCPKG_MANIFEST_INSTALL=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python tools/env.py run -- cmake --build build/m7r21-f4
python tools/env.py run -- ctest --test-dir build/m7r21-f4 --output-on-failure
QT_QPA_PLATFORM=offscreen python tools/env.py run -- build/m7r21-f4/photopipeline.exe --ui-smoke --inputs tests/golden/base --shots build/m7r21-f4/shots
python tools/env.py run -- python tests/golden/smoke.py build/m7r21-f4
python tools/env.py run -- python tools/ci-tidy.py build/m7r21-f4 --update     # 重播种（M7R21F4-0/-0-1/-10）
python tools/env.py run -- python tools/ci-tidy.py build/m7r21-f4              # 复跑棘轮
tools/bin/clang-format-win64.exe --dry-run --Werror <本包改动文件>
grep -rE "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/                    # 期望 0 命中
```

## ② 读数/结果摘录

- 全量 ctest（`build/m7r21-f4-ctest-final.log`）：`100% tests passed out of 35`（89.93s）。
- ui_smoke（`build/m7r21-f4-uis-final.log`）：冻结行 **`UI-SMOKE OK shots=12 pages=3`** 逐字；
  **第 4 波新探针 4 节**：`intake: checked=3 bad=0 rows=16 unread=1 hint=1`（R-148/149）、
  `classify-filter: checked=5 bad=0 filter="pick" rows=16`（R-143）、
  `edit-target: checked=3 bad=0`（R-152/153）、`settings-tabs-light: checked=4 bad=0`（UX-12
  像素档）；存量探针全绿：`meta-geom checked=22` / `meta-loop 11` / `meta-map-expand 14` /
  `rtl-mirror 11` / `check-all 11` / `context-menu cards=12 checked=36` / `run-reason 4` /
  `folds 6` / `settings-geom 39` / `output-geom 29`（均 bad=0）。
- 金样（`build/m7r21-f4-golden2.log`）：`SMOKE total=20 pass=20 fail=0`（冻结行逐字）。
- **tidy 棘轮**（两次重播种 + 复跑，`build/m7r21-f4-tidy*.log`）：
  消音翻案后 `--update` 播种 264/71→**264/76**（总量零增长）；R-081 异步化补记后再播种
  264→**263**（exif_editor 2→3 的连带放宽由他文件收紧抵消，总量只减）；终读数
  **`TIDY total=263 baseline=263 files=75 violations=0 seed=0`**（`build/m7r21-f4-tidy-final2.log`）。
- **NOLINT 零残留**：`grep -rE "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/` = **0 命中**
  （代码内注释提及「NOLINT 处置翻案」的是契约说明文字，非抑制指令）。
- 格式：`clang-format-win64.exe --dry-run --Werror` 本包改动面 exit 0（收口提交 `ca96896`
  = 8 文件纯格式重排、零语义）。

## ③ 结论与判据

- **NOLINT 处置翻案（M7R21F4-0）**：R2G 的 11 处行级消音全部撤销，替代处置 = 零行为结构
  修复 14 处（同型形参对→参数组 struct 9 + Win32 int↔ptr 显式转换封装 5）+ 无零行为修法 7 处
  （AVX2 内核 intrinsics ×6、QtConcurrent 任务 lambda ×1）按 M7-ADJ-tidy-reseed 口径重播种
  + 逐条归因（adjudications 十一节）。「仓库零抑制」恢复成立。
- 色彩四选项 GUI 接线（M7R21F4-4）：pp-intent/pp-gamut/pp-dither8/pp-linear 四控件全部
  **opt-in 默认 0/false = 现行为**（金样 20/20 背书）；预览通道 linear_resample 加性透传。
- 探针批抓出并修复两个真实缺陷：R-153 尾单（restore_selection 塌缩一行）与 R-183 前半
  （rebuild_rows 旧代 teardown 与 deleteLater 解耦）——行为面 = 评审意图兑现（M7R21F4-6）。
- 跨界小项 14 条全清（R-195/007/145/239/319/137/133/065/269/264/279/081/204/260 + UX-11
  余量 + enc_heif group 显式声明 + dev_cli state_text），逐条见 F4 handoff ①-C。
- 未跑：`tools/regression.py` 全语料回归（引擎写盘点 R-007 归一有金样 20/20 背书；重播种
  归波次收口——F4 handoff ⑥-6）。
