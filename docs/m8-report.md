# M8 收口报告（进行中）

> 状态：**进行中**（2026-10-28 开工）。按 `docs/m8-workflow.md` 的 K/A/B/C/D 阶段追加；每阶段结束补“阶段验收”。
> 版本：M8 / 0.4.0（版本号届时按 `CMakeLists.txt:19` 单源更新）。
> 关联：设计 `docs/m8-encoder-params-and-ssimulacra2-design.md`（v4）｜参数范围 `docs/m8-param-exposure-review.md`｜控件形态 `docs/m8-param-control-map.md`｜纪律 `docs/m8-discipline.md`。

## 0. 阶段总览

| 阶段 | 状态 | 提交 | 关键证据 |
|---|---|---|---|
| K 启动 | 完成 | 并入 M8-A 提交 | `build/m8/k3-baseline/cmd.log` |
| A 参数事实层 | 完成 | 待提交 | `build/m8/a4-{build2,ctest,smoke,regression,format}.log`、`build/m8/a4/reconcile.txt` |
| B 引擎 + UI | 完成 | 待提交 | `build/m8/b-gate-*.log`、`build/m8/b1/`、`build/m8/b2/` |
| C SSIMULACRA 2 | 完成 | 待提交 | `build/m8/c-gate-*.log`、`build/m8/c1/`、`build/m8/c2/`、`build/m8/c3/` |
| D 收口 | 完成 | `7455edc`（+ dist 证据补记） | `docs/m8-report.md`、`CHANGELOG.md`、`README.md`、`docs/param-catalog.md`、`dist/PhotoPipeline-0.3.5-win64.zip` |

## 1. 交付内容（逐阶段追加）

### K 启动

- K1 `docs/m8-adjudications.md`（冻结面加性逐条登记，M8-A1…A16）
- K2 本报告骨架
- K3 基线门禁留档（Stage A 前状态）
- K4 任务板（task-1/2/3 数据卡 + 写作用域/依赖/验收）+ 构建树约定
- K5 `docs/m8-discipline.md`（任务卡自包含/禁止递归派发）已确认并生效

### A 参数事实层（完成）

- **A0**：新增 `src/codecs/param_catalog/catalog.{h,cpp}`（CatalogSpec/CatalogRow/查询/计数/合并辅助）；`tools/gen_param_catalog.py`（specs → `.inc` + anchors，`--validate-only/--check/--strict-counts`）；`tools/param_catalog_specs/README.md`（schema/字段/形态约束/行清单）。
- **A1**：`tools/param_catalog_specs/jxl.json` 16 行（switch 6 / number 5 / dropdown 1 / mode 2 / text 2；anchor 4 / dependent 9 / independent 3）。
- **A2**：`tools/param_catalog_specs/x265.json` 40 行（36 可设置 + 4 只读）。x265 spec 关键决策：`x265:no-deblock` engine 用规范名 `no-deblock`（通用 `no-` 前缀，B1 直接下发、不取反）；X7 按源码订正为 `tu-inter-depth` 并降级为 tooltip note。
- **A3**：`tools/param_catalog_specs/aom.json` 57 行（51 可设置 + 6 只读）、`svt.json` 3 行只读；aom 形态 switch 29 / dropdown 16 / number 6。
- **A4**：生成 `.inc`（rows=116：x265 40 / aom 57 / svt 3 / jxl 16）+ `anchors.inc`（x265 239 / aom 192）；新增 `tests/unit/test_param_catalog.cpp`（计数/形态/角色/分组/锚点/禁名/唯一性/合并辅助，37 项 ctest 通过）。Stage A 未把目录并入 `introspect_backends`/`static_formats` ⇒ **零用户可见行为变化**。

**A4 对账（`build/m8/a4/reconcile.txt`）**

| 后端 | 行数 | 可设置 | 只读 | anchor / dependent / independent |
|---|---:|---:|---:|---|
| heif/x265 | 40 | 36 | 4 | 4 / 25 / 11 |
| avif/libaom | 57 | 51 | 6 | 3 / 18 / 36 |
| avif/svt-av1 | 3 | 0 | 3 | 0 / 0 / 3 |
| jxl/libjxl | 16 | 16 | 0 | 4 / 9 / 3 |
| 合计 | **116** | **103** | **13** | 11 / 52 / 53 |

> 与评审稿“组行 ≈142”差异来自组行合并（8 帧内开关、tskip 对、qm 对、partition 上下限、tile 对、HDR 组、只读行等），**范围未扩大**；键展开 116 行为目录/测试/门禁真源。

**A 阶段偏差（如实登记）**

1. A3 def 取 pinned 源码 **all-intra** 口径：`enable-cdef=0`（插件 `encoder_init:3091` 覆写；评审稿 1 为普通模式默认）、`qm-min=4`/`qm-max=10`（:3100-3101）、`disable-trellis-quant=3`；`cq-level` def 留空（libheif 总先按 quality 映射并 AOME_SET_CQ_LEVEL，库内 10 不可观察）；tune=iq 的 handle_tuning 改 9 个值已逐行 tooltip 注明。
2. A2 def 口径 = `x265_param_default()`；preset slow/tune ssim 与插件强制项（aq-mode=1、psy-rd/psy-rdoq=1.0、rd=4、rdoq-level=2 等）写在 tooltip/reason，不写 def；crf/qp 动态域（随位深）与 qg-size/max-tu-size（随自适应 CTU）留给 B1 预检。
3. A3 `min-cr` 域 [0,1000] 为实用域（源码无上界校验），标“待 B1 源码核对”；`target-seq-level-idx` 仅列合法 level 索引 + 31 不约束；`set-tier-mask` 按源码位掩码；`tune-content` 含 `film`。
4. A2 只读 4 行 role 按语义取（profile/keyint/input-csp=dependent，total-frames=independent；control-map 未规定，登记待 B1 统一）。
5. 生成器在 Stage A 修复 4 处（登记 M8-A14/A15）：KEY_RE 允许 `__` 只读键；AOM anchors 含 `ARG_DEF_ENUM`；x265 `no-` 别名锚点；JXL text 行 `expected_format` 修正；另修正 anchors 计数字符串。
6. A1 `intrinsic_size` 以 `form=text` 承载 x/y 对（“宽x高”），B1/B2 负责解析/校验；HDR 三值上界 10000 为 UI 约定（libjxl 不校验）。

### B 引擎 + UI（完成）

**B0（Lead，加性接入）**：`ParamType::String` + `ParamContext` + `cross_validate(ctx)` 声明；`static_formats()` 追加 JXL 目录行、`introspect_backends()` 追加 x265/libaom 目录行；`declared_keys` 并入目录键；`param_rules.h` 冻结规则引擎接口；`settings.h` 加性 `quality_score`（GUI 默认 true）。

**B1（引擎）**：
- `params.cpp`：heif/avif runtime 后端用 live introspection 做类型/范围/枚举校验；`cross_validate(ctx)` = 旧通道 + rules::preflight。
- `param_rules.cpp`（新，45KB）：数据驱动规则表 + evaluate/preflight；覆盖 G1/G3/G4、X1–X15、A1–A16、S1–S4/S6、J2–J18；A9 按源码降级 W。
- `enc_heif.cpp`：透传后端作用域（x265 只对 heif、aom 只对 libaom、SVT 无）+ 目录白名单兜底 + bool 值格式 `"1"/"0"`（修复 libaom 只认数字的真实缺陷）。
- `enc_jxl.cpp`：16 个新键三态接线 + codestream_level 顺序修正 + effort 11（format_tables 仅 hi 10→11）。
- `dev_cli.cpp`：`--param` String/运行时内省/清单外键报错；`pp_linkprobe` 加目录计数/SVT 10 键/无 `svt:` 三条 PROBE。
- 测试：`test_params` 语义适配（跳过 has_default=false 行）；新增 `test_param_rules`（规则表不变量 + 锚点闭合 + 60 用例）。

**B2（UI）**：
- String 编辑器修复 ui-smoke 崩溃；目录行三态（N/D/T 启用勾选、新 Bool 三态下拉、13 只读行、禁设行不出现）。
- RC 模式显式下拉（HEIF 质量/CRF/QP；libaom 质量/CQ Level；SVT 质量/QP），默认质量、非激活路径不下发、无损整组隐藏、预设按存在键推断+冲突报错。
- 联动实时态统一走 `pp::rules::evaluate()`；每格式 tab 错误清单/徽标、`ready_to_start()` 拒绝、`focus_first_rule_error()` 聚焦首错。
- 9 组词表 + 搜索（标签/键/引擎名）+ 只看已启用/可设置；预设新键序列化；settings `pp-quality-score` 完整读写。
- 加性探针 `probe_m8_params()` 49 断言；test_smoke 组词表语义适配。

**B 阶段门禁（`build/release-dev`，`build/m8/b-gate-*`）**

| 门禁 | 命令 | 结果 |
|---|---|---|
| 构建 | `cmake --build build/release-dev` | exit 0（141 目标，`b-gate-build.log`） |
| 单测 | `ctest --test-dir build/release-dev` | **38/38 passed**（69.76 s，`b-gate-ctest.log`） |
| 金样 | `python tests/golden/smoke.py build/release-dev` | **`SMOKE total=20 pass=20 fail=0`** |
| UI | `python tests/ui_smoke.py build/release-dev` | **`UI-SMOKE OK shots=12 pages=3`**；`m8-params checked=49 bad=0`；layout-audit bad=0 |
| 回归 | `python tools/regression.py` | 本机 DIFF，与 K3 **逐字同形**（thread_budget 16 vs 4 + buildtree 路径截断）；无 M8 键/新行进入日志（`b-gate-regression.log`） |
| 格式 | `bash tools/ci-format.sh` | `violations=0`（200 files） |

### C SSIMULACRA 2（完成）

**C0（Lead）**：libjxl v0.12 `tools/ssimulacra2.{h,cc}` + `gauss_blur.{h,cc}` 逐字 vendor 到 `third_party/ssimulacra2/`（SHA-256/License/PATENTS 见其 README）；新增 `src/core/metric/metric.h`（RowFn/FrameSource/compute）；`pipeline.h` 加性 `MetricState` + `OutputResult` 评分字段 + `RunConfig::quality_score`；CMake 收编 compat；adjudication A25–A31。

**C1（指标核心）**：
- `third_party/ssimulacra2/compat/` 20 文件兼容层：Status/StatusOr、ImageF/Image3F、ImageBundle、ColorEncoding、ThreadPool/AlignedMemory、ToXYB（公式/常量逐字，`std::fma` 复现 AVX2 舍入）、官方 `matrix_ops.h`/`no_memory_manager` 逐字复制、官方 `gauss_blur.cc` Highway 实现保留；vendored 仅 5 行 include 适配（双哈希+逐行记录）。
- `src/core/metric/metric.cpp`：compute 实现（<8×8/尺寸不等/异常/NaN → Unavailable；自比=100；负分放行；线程安全；ms）。
- `tests/unit/test_quality_metric.cpp`：恒等/单调性/8-16bit/灰度/边界/异常/确定性/并发。
- **数值 oracle**：libjxl v0.12 官方源码 + vcpkg 预编译官方内部件（PyPI 离线不可得）；**6/6 对 |Δ|=0**（8 位小数逐位相同；8-bit/16-bit/灰度/JPEG/JXL 覆盖）。
- 性能：1/4/12MP = 134–136 / 152–158 / 161–163 ms/MP（设计区间 120–245）；峰值内存 ≈142 B/px（约 5.9× C10 的 2×frame 预算，已如实登记 A28/A31）。

**C2（管线/结果面）**：
- `pipeline.cpp`：参考=编码器收到的量化样本（`simd::quantize8/16` + dither8 口径）/失真=OIIO 回读/两侧同一 ColorManager→sRGB；评分点=encode 后 metawrite 前；失败只落 MetricState（不失败产物、无 Warning）；评分运行额外预算 160 B/px。
- `dev_cli.cpp`：`--score`（dev-only）+ sidecar 嵌套 `metric{...}` + 每输出 `log_info("quality metric")`。
- `page_run.cpp`：隐藏子行 `pp-run-score-<i>-<j>`/`-text-`，Ok 分数+色阶+tooltip，Unavailable/Failed `—（原因）`，Off 不显示；`mainwindow_run.cpp` GUI 默认开。
- 8 格式实测：png/tiff/webp-lossless/bmp/jxl-lossless=100；heif/avif 码流无损=98.34（如实<100）；有损分下降/负分合法；P3/keep 非 sRGB 真无损=100；4×4→Unavailable 且产物 ok。
- 默认不评分：与 A/B 树跨树 normalized 回归 diff **0 行**；预算行逐字不变。

**C3（证据面）**：
- `SCHEMA.md` 加性 sidecar metric + `assert.score`（state/min/max，区间不逐值）；`pp_verify` check_score + 17 条 selftest；4 既有金样加 score 断言（不新增 case）；smoke.py 内部 `--score`；`smoke_probes.cpp` 纯加性 run-score 探针；`ui_smoke.py` 新断言。
- `pp_verify --selftest` → `VERIFY selftest OK (56 probes, 0 unexpected)`；smoke 真分 jxl/png16=100、heif=98.92、avif=94.85；ui `run-score checked=3 bad=0`。

**C 阶段门禁（`build/m8/c-gate-*`）**

| 门禁 | 命令 | 结果 |
|---|---|---|
| 构建 | `cmake --build build/release-dev` | exit 0 |
| 单测 | `ctest --test-dir build/release-dev` | **39/39 passed**（70.32 s，含 `test_quality_metric`） |
| 金样 | `python tests/golden/smoke.py build/release-dev`（内部 --score） | **`SMOKE total=20 pass=20 fail=0`** |
| UI | `python tests/ui_smoke.py build/release-dev` | **`UI-SMOKE OK shots=12 pages=3`**；`run-score checked=3 bad=0`；layout-audit 1109/0 |
| pp_verify | `pp_verify.exe --selftest` | `VERIFY selftest OK (56 probes, 0 unexpected)` |
| 回归 | `python tools/regression.py` | 本机 DIFF，与 K3 **逐字同形**（thread_budget 16 vs 4 + buildtree 路径）；无评分/新参数行进入日志（默认不评分） |
| 格式 | `bash tools/ci-format.sh` | `violations=0`（203 files） |
| 静态 | `python tools/ci-tidy.py build/release-dev` | 棘轮通过（total=262=baseline，0 violation） |

**C 阶段偏差/风险（如实）**

1. **内存**：C1 compute 内部物化整帧 ≈142 B/px，超 C10 的 2×frame（24 B/px）约 5.9×；C2 以 A28 额外预算 160 B/px 兜底（3MP 峰值 325.6MB < 预留 552MB）；12MP 评分峰值 ≈1.8GB/输出，并行 workers 叠加为已知风险（P2 流式优化留后续）。
2. 24MP 评分墙钟预估 ≈28–33 s/图（8 输出，C1 实测 161–163 ms/MP + C2 采样 210 ms/MP）；批处理墙钟代价登记 R1/R6，GUI 可关。
3. `dither8` 灰度 1ch 组合的抖动序列与参考 3ch 交织口径可能逐位不同 ⇒ 该组合为近似（默认 dither 关，未实测）。
4. oracle 采用官方内部件而非 PyPI（离线不可得）；只验证评分算法，解码/管线由 C2 证据覆盖。
5. `ui_smoke` 首跑偶发既有 `zero-byte-redo` 探针失败（同构建复跑通过、并发对照未复现；根因未证，判据未改）；固定 `.cache/tmp/ui-smoke-out` 共享目录为门禁卫生理论风险。
6. C12 颜色分档未做像素断言（探针只锁文本/显隐/tooltip）；`assert.score` outputs[] 逐产物路径未入金样（selftest 覆盖单产物）。

### D 收口（完成）

- D1 报告：本文件（K/A/B/C/D 逐阶段交付、验收、偏差、未覆盖、复现命令）。
- D2 文档：`CHANGELOG.md` 新增 `[未发布] M8`；`README.md` 增加 M8 功能段；`docs/param-catalog.md` 追加 M8 更新指针；`tools/collect_licenses.py` 增加 vendored `ssimulacra2` 的 `LICENSE`/`PATENTS` 随包分发（third_party 非 vcpkg port）。关于页无独立 UI（版本徽标由 `core/version.h` 单源），许可文本随安装包 licenses 目录分发。
- D3 门禁：沿用 C 阶段同树最终结果（D 仅改文档/许可脚本，未改产品代码）；补充 `python -m py_compile tools/collect_licenses.py` 通过。
- **D4 Windows 发行打包（已完成）**：`python tools/env.py run -- python tools/make_winzip.py dist --smoke-exe build/release-dev/photopipeline.exe` → exit 0。
  - 产物：`dist/PhotoPipeline-0.3.5-win64.zip`（34,996,250 bytes；sha256 `9e02ce3195bc3f18045f3af1170cc7d7dec5c88c845eb1985551c04583457035`；107 条目；顶层单目录 `PhotoPipeline/`）；`dist/PhotoPipeline/` staging 同源。
  - 门禁：闭包 A 68 PE/105 导入名全解析；Qt 来源 B 5 个 DLL sha256 一致；结构 C 齐备；许可 D `licenses/*/copyright=35` + `PhotoPipeline/LICENSE/NOTICE` + 新增 `licenses/ssimulacra2/{LICENSE,PATENTS}`；烟测 E-1 `--version`、E-2 `UI-SMOKE OK shots=12 pages=3`、E-3 `WIN-LAUNCH OK windows=6 visible=1`；产物内复核 `--version` 与许可计数通过。
  - 版本仍是 `0.3.5`（M8 代码已编入；0.4.0 版本号提升留给发行流程）。
- 提交：A `d7490e1`、B `30391b1`、C `e9cce58`、D `7455edc`（+ dist 打包证据补记提交，见 `git log`；收口提交不入发行 tag）。

**D 阶段未覆盖（如实）**

1. **Linux 构建/CI 未在本机验证**（Windows 本机完成全部门禁；Highway 多目标 TU + GCC LTO 需 CI 首跑确认）。
2. **AppImage 未跑**（Linux 本机不可用），Windows zip 已完成（D4）；`collect_licenses.py` 改动已在 winzip 许可断言 D 中覆盖。
3. `--ui-traverse` 深走查未覆盖新增 UI 面（目录三态/RC/预检清单/评分子行）；`--ui-smoke` 冻结行与 12 截图已通过。
4. 24MP 评分墙钟/峰值 RSS 未实测（C1 12MP 实测 ≈1.8GB；C2 3MP 实测 325.6MB/≈210ms/MP，按比例外推）。
5. `--dev` 批量跨字段预检未接 `pipeline::validate_run_config`（A24）；GUI `ready_to_start()` 已覆盖运行前拦截。
6. 版本号未提升（仍 0.3.5；M8 目标 0.4.0 由发行流程按 `CMakeLists.txt:19` 单源更新）。

## 2. 验收结论（命令 + 结果逐条取证）

### K3 基线（Stage A 前）

命令与日志：`build/m8/k3-baseline/cmd.log`。结果：

- `ctest --test-dir build/release-dev`：**36/36 passed**（60.88 s）。
- 金样：**`SMOKE total=20 pass=20 fail=0`**（20 个 case 全 PASS）。
- UI：**`UI-SMOKE OK shots=12 pages=3`**；`layout-audit checked=1069 bad=0`；`format-card checked=81 bad=0`。
- `regression.py`：本机 **DIFF**，归因为**已知环境形态差异**（非代码回归）：基线 runner 4 vCPU `thread_budget=4` vs 本机 16 核 `thread_budget=16`，以及 libjpeg-turbo buildtree 路径截断差异（`vendor-stdout`）；AGENTS §2 已登记“基线为 runner-shaped，本机直跑出 thread-shaped diff”，CI 为权威。Stage 门禁复核时沿用此归因，不重播种。
- `ci-format.sh`：首次因 pwsh PATH 无 `bash` 未执行；用 `C:\Program Files\Git\bin\bash.exe` 复跑（`build/m8/k3-baseline/format.log`）。

> 说明：本机回归不能 zero diff 是**已知基线形态**，不是 M8 引入；Stage A/B/C 的“零回归”以“新增参数默认不设、既有信息行/预算行不变、CI zero diff”为判据，本地 diff 必须逐条归因为 thread/path 形状。

### A 阶段门禁（Stage A 出口）

| 门禁 | 命令 | 结果 |
|---|---|---|
| 生成器 | `python tools/gen_param_catalog.py --strict-counts` → `--check` | 均 exit 0：`rows=116 x265_anchors=239 aom_anchors=192`；`catalog check OK` |
| 构建 | `python tools/env.py run -- cmake --build build/release-dev` | exit 0（`build/m8/a4-build2.log`） |
| 单测 | `ctest --test-dir build/release-dev` | **37/37 passed**（60.30 s；新增 `test_param_catalog`，`build/m8/a4-ctest.log`） |
| 金样 | `python tests/golden/smoke.py build/release-dev` | **`SMOKE total=20 pass=20 fail=0`**（`build/m8/a4-smoke.log`） |
| 回归 | `python tools/regression.py` | 本机 DIFF，归因同 K3（thread_budget 16 vs baseline 4 + buildtree 路径截断）；**无任何 M8 键/目录行进入日志**（目录尚未接入），`build/m8/a4-regression.log` |
| 格式 | `bash tools/ci-format.sh`（Git Bash 路径） | `violations=0`（197 files） |
| 零行为变化 | 目录 API 未被 `introspect_backends`/`static_formats`/UI 调用 | 代码检索 + 金样/回归一致 |

## 3. 偏差与追认

（逐条）

## 4. 未覆盖 / 未决项（如实）

（逐条）

## 5. 复现命令与证据路径

```bash
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- ctest --test-dir build/release-dev
python tools/env.py run -- python tests/golden/smoke.py build/release-dev
python tools/env.py run -- python tools/regression.py
python tools/env.py run -- python tests/ui_smoke.py build/release-dev
bash tools/ci-format.sh
```

证据：`build/m8/<task-id>/`（gitignored）；报告引用路径 + 关键输出行。
