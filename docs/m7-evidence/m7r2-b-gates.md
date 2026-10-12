# m7r2-b-gates —— R2B「核心与参数域尾单」关键门禁读数

> 包：R2B（提交 `5d19146`）｜原始日志：`build/m7r2-b-*.log`、`build/m7r2-b/handoff.md`（gitignored）。
> 摘录人：R2F（2026-10-12，自 R2B handoff ④ 节逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r2-b -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r2-b
python tools/env.py run -- ctest --test-dir build/m7r2-b --output-on-failure \
    -R "test_params\|test_presets\|test_fsops\|test_settings\|test_scheduler_contract\|test_metadata\|test_pipeline_contract"
python tools/env.py run -- python tests/golden/smoke.py build/m7r2-b
python tools/regression.py --self-check
```

## ② 读数/结果摘录

| 命令 | 读数（逐字） |
|---|---|
| 首建全量 329 目标 | exit 0 |
| 7 目标子集 ctest | `100% tests passed out of 7`（含全部 m7r2b 新用例） |
| 全量 ctest | `97% tests passed, 1 tests failed out of 35` —— 仅 test_color 失败 |
| 金样（格式化前后各一轮） | `SMOKE total=20 pass=20 fail=0`（冻结行逐字不动） |
| 回归行型自检 | `regression self-check: cases=19 fail=0` |
| clang-format（钉版，本包 17 个 C++ 文件） | exit 0；格式化后重建 + ctest/smoke 复跑全绿 |

## ③ 结论与判据

- 本包 7 测试目标全绿 + 金样 20/20 + 行型表自检 19 例全过。
- **test_color 唯一失败 4 条 `cicp/*-log-verbatim` 属他包在途态**（同树 colormanager 未提交改动，
  R2A 施工中；本包零 colormanager 触点）；test_decode/test_thumbs 当时编译失败亦为他包在途
  （`OIIO::TypeUINT8` 引用）。三者随 R2A 落地后由 R2-gate 全量复验（`m7r2-a-gates.md` 35/35）。
- 回归全量本包未跑（工作树含他包半成品，跑回归会把他人在途改动算进来）——归波次收口
  （R2-gate 重播种 + 复跑 zero diff 在案）。
- 行为变化三条已登记（M7R2B-2/3/4）：跨字段规则双通道共用、259/255 长度硬线解除（`\\?\` 归一）、
  validate_preset 补 cross_validate 通道；默认/常规路径行为字节级不变（金样背书）。
