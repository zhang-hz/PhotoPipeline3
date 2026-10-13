# m7r22-g1-gates —— R2.2 G1「探针总集与 Linux ui_smoke 定点修复」门禁读数

> 包：M7-R2.2-G1（提交 `bbe5101` / `fd5b838` / `07cc8c5` / `297d22b` / `4addc51`）｜
> 日期：2026-10-13｜原始日志：`build/m7r22-g1*.log`、`build/m7r22-g1/{telemetry-step.yml,
> extractor_v3.py,probes.diff}`（gitignored）。摘录人：G4（2026-10-13，自 G1 handoff ④ 逐字
> 摘录；G4 另于合流 HEAD 复跑 ui_smoke/金样，见 `m7r22-g4-gates.md`）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r22-g1 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r22-g1
python tools/env.py run -- ctest --test-dir build/m7r22-g1 --output-on-failure
python tools/env.py run -- python tests/ui_smoke.py build/m7r22-g1
python tools/env.py run -- python tests/golden/smoke.py build/m7r22-g1
tools/bin/clang-format-win64.exe --dry-run --Werror src/ui/smoke_probes.cpp   # 钉版 23.1.1
python tools/env.py run -- python tools/ci-tidy.py build/m7r22-g1
# CI 判据：main-full linux job（run 38314701500 @ bbe51017 等）
```

## ② 读数/结果摘录

- ui_smoke（`build/m7r22-g1-uis10.log` 终态）：`UI-SMOKE OK shots=12 pages=3`（冻结行逐字）+
  `UI-SMOKE pass`；G1 新探针行：rtl-mirror `checked=18 bad=0`（stars=0 hud=0 align=0
  geom=0）/ format-roundtrip `15/0`（`off-active=webp front=jpeg` 取证）/ rtl-deep `4/0
  cols=96` / color-options `6/0` / edit-target `4/0` / context-menu `cards=12 checked=60
  bad=0` / check-all `13/0` / **zero-byte-redo `7/0`（outputs=26 a=598）** / **failure-list
  `7/0`（lines=1）**。
- 金样（`build/m7r22-g1-golden.log`）：`SMOKE total=20 pass=20 fail=0`（冻结行逐字）。
- ctest：`100% tests passed out of 36`（48.3s；ui_smoke ctest 条目单独复跑 34s 1/1，双跑
  增量远低于 240s TIMEOUT）。
- 格式：`clang-format --dry-run --Werror src/ui/smoke_probes.cpp` 0 diff；tidy 单查
  smoke_probes.cpp 0 告警；`ci-tidy` 全树 +3 全部来自 G3 当时未入库 `test_meta_format.cpp`
  （跨界面），本包文件 0 新增。
- 遥测提取器 v3 自测（从 YAML 抽真实 heredoc 跑 4 路假数据）：ALL CASES PASS——assert-fail
  案首条注解带 `UI-SMOKE FAIL …` + `[tail-probe]` + `file=src/ui/smoke_probes.cpp line=366`
  断言源行。
- **CI 实况**（M7R22G1-1 的判据可见性证明）：run 38314701500 @ `bbe51017` **linux failure，
  注解逐字点名** `UI-SMOKE FAIL settings-tabs-light：未选中标题像素 == 亮 text2（got #222638
  want #5d5f68）` + `选中标题像素 == 亮 text（got #1e2028 want #1b1c20）`（#48「无 FAIL 断言
  行」不可定位形态就此闭环）。此后 run 38318033552 @ `fd5b838` / 38322201145 @ `297d22b` /
  38324958075 @ `4addc51` 均 **lint 红**（G2 当时未过钉版格式的已提交文件，末轮余
  settings_dialog.cpp:1341 一行）→ linux job 被跳过，容差修复/探针批的 Linux 侧验证未取得。

## ③ 结论与判据

- **本机判据全绿**（Windows 侧）：ui_smoke 冻结行 + 9 节新探针、金样 20/20、ctest 36/36、
  本包文件格式/tidy 0、遥测提取器自测通过。
- **容差机制（M7R22G1-2）达成设计目的**：run 38314701500 的注解即「修复前」读数（Linux
  灰阶 AA 漂移 #222638/#1e2028 vs 想要 #5d5f68/#1b1c20——同色 AA 漂移非语义差异）；修复后
  判据①众数 ±6 / ②字形内核 t≥0.5 + 残差≤12 / ③令牌互斥三条件并行，断言只增不减。
- **终判据（main-full linux 绿）未达成**：G1 时点被 G2 在途文件的 lint 红阻断；G2 已于
  `4dee5c8` 收口格式，但 `4addc51..4dee5c8` 5 提交截至 G4 时点未推送（`origin/main` =
  `4addc51`），linux 读数待 G6 发行包推送后取得（见 m7-report §8.4 残余①）。
