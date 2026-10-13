# m7r22-g3-gates —— R2.2 G3「判据小项与文档补录」门禁读数

> 包：M7-R2.2-G3（提交 `3c35c00` / `39942bf` / `55b13a1`）｜日期：2026-10-13｜
> 原始日志：`build/m7r22-g3-ctest1.log`、`build/m7r22-g3-build1.log`、
> `build/m7r22-g3-schema-{head,now}.txt`（gitignored）。摘录人：G4（2026-10-13，自 G3
> handoff ④ 逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r22-g3 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r22-g3
python tools/env.py run -- ctest --test-dir build/m7r22-g3 --output-on-failure
python tools/env.py run -- ctest --test-dir build/m7r22-g3 -R ui_smoke -V
# SCHEMA 加性自检
git diff --numstat tests/golden/SCHEMA.md
fc /b <HEAD-state-row> <worktree-state-row>        # 逐字节比对既有值表行
python tools/env.py run -- build/m7r22-g3/pp_verify.exe --selftest
tools/bin/clang-format-win64.exe -i src/ui/page_meta.h   # 仅本包改动文件
```

## ② 读数/结果摘录

- ctest（`build/m7r22-g3-ctest1.log`）：**`100% tests passed out of 35`**（46.98s，全量含
  ui_smoke、verify_selftest）；`ctest -R ui_smoke -V` 冻结行逐字 **`UI-SMOKE OK shots=12
  pages=3`**（末行 `UI-SMOKE pass`）。
- SCHEMA 加性自检：`git diff --numstat tests/golden/SCHEMA.md` = **`1  0  tests/golden/
  SCHEMA.md`**（纯加性，0 删除 0 修改）；HEAD 与工作树的 `state` 值表行 `fc /b` 逐字节比对 =
  **`STATE_ROW_BYTE_IDENTICAL`**（既有值表行逐字不动）。
- `pp_verify --selftest`：**`VERIFY selftest OK (39 probes, 0 unexpected)`**。
- `clang-format -i src/ui/page_meta.h`：无额外格式变化（diff 仍 +5/-3 = 本包两处注释改写）。
- 校读补录自检（`39942bf` 提交正文引用的实跑读数）：`handoff rows=19 doc rows=19 /
  ROW_COMPARE diff=0`（抄录件与原件逐行相等）、`LINK_CHECK missing=0`。

## ③ 结论与判据

- **SCHEMA 加性（M7R22G3-1）走「加性 + 追认」有机械背书**：numstat 1/0 + 逐字节比对双自检；
  断言零放松、金样/夹具零改动（SCHEMA.md 非语料非夹具）。
- **读数归属注（如实）**：上述 build/ctest 为 2026-10-13 当时工作树实跑，树上含并行包 G2
  在途改动（kFieldProperty 收口，其后由 G2 自提交收口）；本包代码面仅 `src/ui/page_meta.h`
  注释两处，读数与本包终态一致。
- **未跑（如实，G3 ④ 原注）**：金样、全语料回归、ci-tidy、ci-format 全源判据未在 G3 跑
  （零引擎改动；归波次收口把关——G4 已于合流 HEAD 补跑金样/回归/ui_smoke，见
  `m7r22-g4-gates.md`；ci-format 全源由 G2 `4dee5c8` 收口 0 diff）。
