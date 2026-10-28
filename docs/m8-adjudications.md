# M8 裁定记录（冻结面加性追认）

> 状态：**登记中**（2026-10-28 开工）。格式：`#｜裁定｜理由/先例｜实施归属`。
> 规则：冻结面只走「加性 + 追认」；既有签名/行为零改动；新字段追加在结构体末尾；实施后在本表回填提交号/证据。

| # | 裁定 | 理由/先例 | 实施归属 |
|---|---|---|---|
| M8-A1 | `core/pipeline.h`：`OutputResult` 末尾加性 `MetricState metric_state`、`double metric_ssimulacra2 = -1.0`、`double metric_ms = 0.0`、`std::string metric_note`；`RunConfig` 末尾加性 `bool quality_score = false`；新枚举 `MetricState` 声明在 `OutputResult` 前 | 冻结面加性先例 m5 A28/A29、m6 T11；评分结果必须随输出行返回；既有聚合初始化/位置语义不变 | C2（Lead 集成） |
| M8-A2 | `core/params.h`：`ParamType` 末尾加性 `String`；`ParamDef` 末尾加性 `bool requires_enable = false`；加性 `struct ParamContext` 与 `cross_validate(ctx, values)` 重载；`kGroup*` 词表加性扩展 | 新增文本参数（`frame_name`）与三态/规则引擎需要 backend/位深上下文；旧签名转发、旧行为零改动；先例 m7 R-208/R-211 加性 | B0/B1 |
| M8-A3 | `codecs/encoder.h` 零改动；`core/types.h` 零改动（评分字段不进 Timing/WarningKind） | 评分失败不产生 Warning、不新增 FileState/FileEvent；C10 | — |
| M8-A4 | `core/settings.h` 末尾加性 `bool quality_score`（GUI 默认 on；`--dev` 默认 off + `--score`） | 先例 A28/M6-T11；设置项不进预设 | C2 |
| M8-A5 | `tests/golden/SCHEMA.md` **加性**追加 sidecar `metric` 段与 `assert.score`（区间断言）；金样不新增 case | 证据面只增不减；`SMOKE total=20` 冻结 | C3 |
| M8-A6 | `--dev` CLI 面加性新增 `--score`（`PP_BUILD_DEV` 保护） | `--dev` 冻结面加性先例；GUI 默认开、dev 默认关保证回归零 diff | C2 |
| M8-A7 | `third_party/ssimulacra2/` 纳入 libjxl 0.12 `tools/ssimulacra2.{h,cc}` + `gauss_blur.cc`（BSD-3，算法/权重逐字保留）+ `PATENTS` 说明 + 兼容层；**不新增 vcpkg 依赖/不改 overlay/不升库** | C14；官方源码 + 兼容层（方案 A）；许可清单/关于页登记 | C0/C1 |
| M8-A8 | UI 冻结 `pp-*` objectName 只加不删：`pp-run-score-<i>-<j>`/`pp-run-score-text-<i>-<j>`、`pp-param-enable-<key>`、`pp-param-status-<i>`、`pp-rule-error-*`、`pp-param-rcmode-*` | 冻结口径允许加性（`pp-run-warn-*`/`pp-run-reason-*` 先例）；不改表头/冻结行 | B2/C2 |
| M8-A9 | 新增 `src/codecs/param_catalog/`（目录 API/数据）与 `src/core/param_rules.*`（规则引擎）；`params.h` 仅加性转发；不重排既有表 | C1/C5；新能力尽量放新文件承载 | A0/A4/B1 |
| M8-A10 | 回归基线**不重播种**；Stage A/B/C 预期 zero diff；如出现 diff 先归因并单独裁定 | AGENTS §5.3；新参数默认 Unset、评分默认不在 `--dev` | 全程 |
| M8-A11 | 本阶段**不新增/不升级任何 vcpkg port**；只允许 vendored BSD-3 源码 | C14；用户裁定 SVT 不打补丁 | 全程 |
| M8-A12 | 开发纪律：任务卡自包含/约束清晰；**子任务不得再次派发子任务**；未“开工”不写实现 | 用户 2026-10-28 指令；`docs/m8-discipline.md` | K5/全程 |
| M8-A13 | RC 模式用**显式模式下拉/单选**；新增布尔行用**三态下拉**；计数双口径（报告组行 ≈142；目录/测试/门禁键展开 170） | 用户 2026-10-28 裁定（D9/D10） | B2/C3 |
| M8-A14 | 目录只读键允许 `__` 前缀（KEY_RE 首字符含 `_`）；AOM 锚点抽取含 `ARG_DEF_ENUM`；x265 锚点校验接受通用 `no-` 别名 | Stage A 实测：只读键 `__x265:*`/`__aom:*`/`__svt:*` 被旧正则拒绝；libaom 4 个 enum 选项（dist-metric/sb-size/timing-info/tune-content）用 ARG_DEF_ENUM；x265 `no-deblock` 由通用前缀归一 | A4 已修生成器/测试 |
| M8-A15 | x265 `no-deblock` 目录 engine=`no-deblock`（不是 `deblock`）；B1 直接下发 `x265:no-deblock=1`，不取反 | x265_param_parse 通用 `no-` 前缀；避免 GUI 语义反转 | A2 数据/B1 接线 |
| M8-A16 | control-map X7 订正：`limit-tu` 依赖 x265 **tu-inter-depth**（非暴露键 `tu-intra-depth`）；tu-inter-depth 不暴露 ⇒ X7 降级为 tooltip 说明，不做 UI 联动 | `source/common/encoder.cpp` 源码事实；评审稿/control-map 笔误 | A2 note/B1 规则表 |
| M8-A17 | JXL `effort` 上限 10→11（既有 ParamDef 域扩展；默认 7 不变）；`effort=11` 需 `expert_options`，由 B1 预检拦截 | control-map J6；libjxl `JxlEncoderAllowExpertOptions` 语义；既有默认/产物零回归 | B1 |
| M8-A18 | B0 目录接入方式：JXL 追加进 `static_formats()` 的 tech params；HEIF/AVIF 追加进 `introspect_backends()` 的 live tech params；`declared_keys` 并入目录键 | C1 单源；UI/预设/--param/校验读同一份表；目录行 has_default=false ⇒ 默认产物零变化 | B0（d7490e1 后工作区） |
| M8-A19 | UI 高级区分组扩展为目录 9 组词表；`tests/unit/test_smoke.cpp` 组断言按新词表做**语义适配**（仅接受并集，不放宽其它约束） | control-map §4.2；既有 5 组保留，目录行使用新词表 | B2 |
| M8-A20 | B2 临时写作用域扩展：`src/ui/mainwindow_run.cpp` 仅允许在 `ready_to_start()` 失败分支加一行 `focus_first_rule_error()`；另 `settings.h/settings.cpp` 由 Lead 加性加 `quality_score`（默认 true），B2 只接线 UI | 运行前错误定位需要在 start 分支加副作用；避免把整文件划入 UI 卡 | Lead/B2 |
| M8-A21 | B1 未落盘前，B2 允许在 ParamForm 内实现**单一入口**的最小规则集；B1 落地后切换为 `pp::rules::evaluate()`，不得两套并存 | 并行解耦；收口时以 rules 引擎为唯一源 | B2 收口/B1 集成 |
| M8-A22 | `page_output.h` 加性公开方法 `focus_first_rule_error()`（B2 实际交付）；`mainwindow_run.cpp` 仅加一行调用 | 运行前错误定位需要跨文件；纯加性、既有签名零改动 | B2（已交付） |
| M8-A23 | J5（`box_compress` 未开 ⇒ 隐藏 `brotli_effort`）**接受**：既有 `brotli_effort` 本就 `visible=never_visible`（JXL-09），规则只在 box_compress=1 时显示，零回归 | B2 报告张力核查；不改变既有键默认可见性 | B1 规则保持 |
| M8-A24 | 规则引擎**不**改 `pipeline.cpp::validate_run_config`；预检走 `PageOutput::ready_to_start()`（GUI 批量开始运行）+ `validate_params`（dev/编码器兜底）。`--dev` 批量/跨字段预检为已知缺口，登记 D 阶段复核 | 避免改冻结 pipeline 主链路；B2 已在 start 分支拒绝并聚焦；C6 的“运行前”在 GUI 面成立 | B2/Lead |
| M8-A25 | 评分数据面契约（C0/C3 冻结）：sidecar 仅 `--dev && --score && state!=Off` 写顶层 `"metric"`；字段 name/version/state/score/ms/reference/colorspace/note；`state` 小写；`reference="encoder-input"`，`colorspace="sRGB"`；Off 不写键 | C11/C13；旧 sidecar 逐字不变 | C2/C3 |
| M8-A26 | `assert.score` 只认 `state`/`min`/`max`；min/max 隐含 state=ok；区间断言不逐值；金样 4 既有 case 加断言、不新增 case | C13；证据面只增不减 | C3 |
| M8-A27 | B2 后 C3 获授权对 `src/ui/smoke_probes.cpp` 做**纯加性**评分探针（新打印行/新断言段），C2 不改该文件；截图 12 张与冻结行不变 | M8-A19 先例；UI 硬断言在 C++ 探针 | C3 |
| M8-A28 | 评分运行**额外像素预算** `160 B/px × w × h`（C1 实测内部物化 ≈142 B/px，取整）；仅 `quality_score=true` 运行时 acquire，`--dev` 默认不评分 ⇒ 预算行零 diff | C10 允许“评分开启额外 acquire 并登记”；实测 3MP 峰值 325.6MB < 预留 552MB | C2/C1 |
| M8-A29 | `--dev --score`、运行页 `pp-run-score-*`、sidecar `metric` 对象为 C11/C13 加性冻结面；state 小写、Off 不写键、reference="encoder-input"、colorspace="sRGB" | C11/C13；旧 sidecar/日志逐字不变 | C2 |
| M8-A30 | 数值 oracle 采用 **libjxl v0.12 官方源码 + vcpkg 预编译官方内部件**（PyPI 离线不可得）；6/6 对 |Δ|=0（8 位小数）；oracle 工程/数据入库 build/m8/c1/oracle | 用户裁定方案 A 的验证方式；不伪造 | C1 |
| M8-A31 | 指标内部物化整帧（≈142 B/px），未达 C10 流式目标；以 A28 预算预留兜底，P2 流式优化留 D/后续 | C1 如实报告；24MP 峰值风险登记 R6 | C1/D |

## 登记回填

| 裁定 | 提交 | 证据 |
|---|---|---|
| M8-A1…A13 | 待 Stage 提交回填 | 待回填 |
