# m7r21-f1-gates —— R2.1 F1「Linux 回归定位与修复」门禁读数

> 包：M7-R2.1-F1（提交 `3339492` / `b673ee4` / `d4099bd` / `0555947`，本包授权 git push）｜
> 日期：2026-10-13｜原始日志：`build/m7r21-f1*.log`、`build/m7r21-f1/{evidence-run47-jobs.txt,
> cr-0555947.json,annot-selftest.log}`（gitignored）。摘录人：F5（2026-10-13，自 F1 handoff
> ④ 与原始日志逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r21-f1 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r21-f1
python tools/env.py run -- ctest --test-dir build/m7r21-f1 --output-on-failure
python tools/env.py run -- python tests/golden/smoke.py build/m7r21-f1
python tools/env.py run -- tools/bin/clang-format-win64.exe -i src/core/fsops.cpp tests/unit/test_fsops.cpp
# 钉版 clang-tidy 单查本包两文件（与 tools/baseline/clang-tidy.tsv 逐项对比）
python tools/env.py run -- clang-tidy -p build/m7r21-f1 --checks=-*,bugprone-*,performance-*,portability-* <file>
# CI 最终判据（run #47 @ 0555947）：main-full linux（全 ctest+金样）job conclusion
```

## ② 读数/结果摘录

- 本机 ctest（`build/m7r21-f1-ctest1.log`）：`100% tests passed out of 35`（含 test_fsops
  断言样例按平台拼写修改后）。
- 金样（`build/m7r21-f1-golden1.log`）：`SMOKE total=20 pass=20 fail=0`（冻结行逐字）。
- tidy 单查本包两文件：`src/core/fsops.cpp` 2 条、`tests/unit/test_fsops.cpp` 1 条，
  与基线（`tools/baseline/clang-tidy.tsv`）逐项相等 = **零新增告警**。
- 提取器 v2 自测（fakeroot 三路假数据，`build/m7r21-f1/annot-selftest.log`）：
  2 失败测试 / 3 断言行 → 摘要注解 1 + 明细 3 + `$GITHUB_STEP_SUMMARY` 全量成文；
  样例行 `::error file=tests/unit/test_fsops.cpp,line=665::CTEST-FAIL test_fsops FAIL m7r2b/vof-reserved: …`。
- **CI 最终判据**（`build/m7r21-f1/evidence-run47-jobs.txt`，run #47 @ `0555947`）：
  `linux（全 ctest+金样）: success`；同轮 `lint/appimage/winzip/windows/cache-gc: success`、
  `tidy: failure`（F2/F3 在飞 src/ui 工作面，非本包面——见 F1 handoff ③-1）、`gate: failure`（随 tidy）。
  前一轮 run #46 @ `3339492`：`linux` step 13 `ctest --preset release` exit 8（8 条失败测试，
  匿名日志 403 + 遥测一版截断 ⇒ 失败名不可见）⇒ 二版遥测 + test_fsops 修复后 #47 全绿。

## ③ 结论与判据

- **最终判据达成**：main-full linux job 绿（#47，check-run `linux（全 ctest+金样）` id 114873710214）。
- 8 条 Linux 失败中可代码级归因的 2 条 = test_fsops 长路径断言样例（`C:\out\…` 在 POSIX
  fs::path 语义下是单组件，被非法字符防线误拒）——#ifdef 按平台拼写样例，两侧断言逐条相同、
  严格度不变（M7R21F1-3）；其余 ≤7 条成因无法从现有证据判定（如实登记，遥测二版 summary
  通道下次红灯可直接点名）。
- `commit_temp_file` rename 有界重试（M7R21F1-2）收口 D-1 瞬态锁（仅 Windows 共享违规类
  GetLastError 5/32/33、≤3 次、退避 25/50/75ms；非瞬态与 POSIX 一次失败即出，失败消息/返回值
  口径逐字不变）。失败注解遥测二版**永久保留**（M7R21F1-1/-1b，判据可见性改进非临时探针）。
