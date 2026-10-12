# m7r21-f3-gates —— R2.1 F3「R2E2 结构余量」门禁读数

> 包：M7-R2.1-F3（提交 `6250b3b`/`5cf7ddd`/`69c08e1`/`60742b0`/`0a6655d`/`c900f39`/`334c8c1`/
> `355e1f2`/`edfc6e4`/`0ca8c62`）｜日期：2026-10-12～13｜原始日志：`build/m7r21-f3-*.log`
> （gitignored）。摘录人：F5（2026-10-13，自 F3 handoff ④ 与原始日志逐字摘录）。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m7r21-f3 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m7r21-f3
python tools/env.py run -- ctest --test-dir build/m7r21-f3 --output-on-failure
python tools/env.py run -- python tests/ui_smoke.py build/m7r21-f3
build/m7r21-f3/pp_test_smoke.exe                       # = ctest test_smoke（第 7/8/9 节新增）
python tools/env.py run -- python tests/golden/smoke.py build/m7r21-f3
```

## ② 读数/结果摘录

- 全量 ctest：**100% tests passed out of 35**（每单元后各一轮；终态 `build/m7r21-f3-ctest19.log`）。
- ui_smoke：`UI-SMOKE OK shots=12 pages=3`（冻结行逐字不变；终态 `build/m7r21-f3-uis17.log`）；
  分项：`filelist-geom checked=17 bad=0`、`settings-geom checked=39 bad=0`、
  `output-geom checked=29 bad=0`、`folds checked=6 bad=0`、`context-menu cards=12 checked=36
  bad=0`；meta-* 全行与拆分前逐字一致（`meta-geom checked=22 bad=0`、`meta-loop checked=11
  bad=0`、`meta-map-expand checked=14 bad=0`）。
- `pp_test_smoke`（test_smoke 第 7/8/9 节新增断言）：`test_smoke: OK`——第 7 节 byte_text
  真值表 10 断言、第 8 节时间键真值表（9/5 计数、清除/命中角色、与引擎写路径同集）、
  第 9 节 FormatSelection 真值表 11 断言。
- 金样（`build/m7r21-f3-golden.log`）：`SMOKE total=20 pass=20 fail=0`（冻结行逐字不变）。

## ③ 结论与判据

- 结构拆分/收敛全部为**逐字搬运 + 单点化**，行为零变化（R-259 join 反馈为唯一明文授权的
  行为变化——关闭路径带反馈同步 join，阻塞期从析构挪到关闭路径，M7R21F3-10）：
  R-254（byte_text 单源 + remove_sel 判据单点）、R-256（apply_map_style 三职拆名）、
  R-185（page_run 注册表 unique_ptr RAII，page_run.h 加性 `~PageRun()`）、R-187/188
  （RowBundle 控件束 + out_kind_of 单点 + refresh_widgets 7 段）、R-196/197/198/199/205
  （exif_editor 孪生收敛 + time_tag_keys 单表 + 三态去魔法索引 + 状态写入权成文）、
  R-061/R-201（SettingsDialog ctor 拆 5 个 build_*_page）、R-200（filelistmodel 一 TU 6 类
  按所有权拆 TU）、B.3 余量（format_selection_model R-189/192 + output_page_build/
  output_template_ui TU 拆分）、R-255（smoke_probe_meta 1675 行/深 10 → 12 分节，节体零
  文本改动、12 个 meta-* 探针输出行逐字一致）。
- 未跑（按分工归波次收口）：ci-tidy 棘轮、tools/regression.py 全语料回归——工作树有他包
  在途文件，跑回归会把他人半成品算进来（与 R2E2 同口径，F3 handoff ④ 注）。
