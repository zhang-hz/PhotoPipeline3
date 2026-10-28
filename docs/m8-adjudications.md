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

## 登记回填

| 裁定 | 提交 | 证据 |
|---|---|---|
| M8-A1…A13 | 待 Stage 提交回填 | 待回填 |
