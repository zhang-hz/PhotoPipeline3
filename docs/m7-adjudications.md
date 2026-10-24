# M7 开发期裁定记录（主对话裁定 + 各波次登记，随波次滚动追加）

> 依据：`docs/deep-review-0.3.2.md`（331 条 R-id）与 `docs/ux-review-14.md`（14 项）的整改落地。
> 冻结接口（PP-FROZEN）的加性扩展走「加性 + 追认」，逐条登记于此。实施归属列括号内为登记来源
> （各工作包 handoff 的「裁定登记」节原样汇总，编号连续化；原文见 `build/m7-wpN/handoff.md`）。
> 汇总人 = WP11；裁定人 = 主对话（各 WP 报出后经波次收口追认）。
> **编号命名空间**：本表 A/B/C/D/E 与 M7-ADJ-*/WP12-B*/WP13-B*（R1）以及 `R2A-A*`/`M7R2B-*`/`R2C-W*`/`R2D1-A*`/`M7-R2-A`/`M7-R2-B`/`M7R2D2-*`/`M7R2E2-*`/`R2G-G*`/`R2F-X*`/`R2F-C*`（R2）以及 `M7R21F1-*`/`M7R21F2-*`/`M7R21F3-*`/`M7R21F4-*`（R2.1，十一/十二节）以及 `M7R22G1-*`/`M7R22G2-*`/`M7R22G3-1`/`M7R22G5-1`（R2.2，十四节）以及 `M7R23G7-*`/`M7R23G8-*`/`M7R23G9-*`（R2.3，十六节）均为**裁定表编号**；`D-n` 为 AGENTS §9 实现期发现缺陷号（如 R2G-G5 承载的 D-1，已由 M7R21F1-2 根治）；勿与 `ux-review-14.md`
> §14 的色彩分级子项（A1–D3）及 `deep-review-0.3.2.md` 附录 B 的拆解蓝图（B.1–B.8）混读
> ——三套编号独立，详见 `m7-report.md` 文首「编号命名空间」注。「任务书 / WP 任务包」引述
> 出处 = [docs/m7r2-plan.md](m7r2-plan.md)（R2 轮方案真源 + 附 A「任务书引述出处对照」，
> 含 M7-WP 系分派书摘录；原始分派书未入 docs/，摘录换机可独立复核）。

## 一、冻结面加性 + 追认（结构/接口扩展）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | `core/metadata.h` `BatchRules` 末尾追加 `time_enabled/gps_enabled/privacy_enabled`（默认 true） | I5；「该卡参与规则 = 开关 && 原判据」，缺省 true = 旧会话零迁移 | WP1（I5） |
| A2 | `core/pipeline.h` `FileState` 尾部追加 `DoneWithErrors` | I7；消费接线后补 | WP1（I7） |
| A3 | `core/pipeline.h` `RunConfig` 末尾追加 `color_intent/color_dither8/gamut_mapping/linear_resample`（默认=现行为） | I8；能力类 opt-in 数据面先行，消费归后续（WP10 未交付，见 D12 与 m7-report §3.1；**R2 轮追记**：消费已由 R2A `c540630` 入树，见八节） | WP1（I8） |
| A4 | `core/metadata.h` `EffectivePreview` 末尾追加 XMP 日期 ×2 + OffsetTime* ×3 | R-085 生效值显示面补全；加性 | WP1 |
| A5 | `core/metadata.h` `write_gps` 加性尾参 `out_warnings`（默认 nullptr） | 告警通道；既有调用点零改动 | WP1 |
| A6 | `core/fsops.h` `resolve_conflict`（+软保留参）/`collect_inputs`（+排除集参）加性重载，旧签名转发 | R-005/R-031；旧行为逐字不变 | WP1 |
| A7 | `codecs/encoder.h` `EncodeRequest::encode_threads = 1` 默认值 | R-111；聚合初始化兼容（末字段默认） | WP2（W2-A1） |
| A8 | `codecs/encoder_registry.h` 宏体改 `register_encoder_or_report`（失败 stderr+assert） | R-110/R-299；既有注册语义零改动，仅失败分支不再静默 | WP2（W2-A2） |
| A9 | `codecs/encoders.h` 追加 `resolved_backend()` | R-299 tie-break 可观测 | WP2（W2-A3） |
| A10 | `core/params.h` 追加 `select_tech`/`declares_lossless` 导出 + 6 个 `kKey*` 绑键常量 | R-208/R-207；presets 逐字副本删除，语义合并单口径 | WP2（W2-A4） |
| A11 | `codecs/heif_profile.h`、`codecs/heif_nclx.h` 两个新头（header-only） | I4 编码档位接口 + UX-14 A1 推导可单测；新文件承载 | WP2（W2-A6） |
| A12 | `core/simd/simd.h` 追加 `set_force_scalar/force_scalar_enabled/use_avx2` | R-303 脚手架；默认关 = 行为零变化 | WP2（W2-A7） |
| A13 | `tools/versions.env` 末尾追加 6 键（CLANG_FORMAT_VERSION/SHA512×2/AQTINSTALL/CMAKE/NINJA） | R-036/039/128 单源化；既有键零改动 | WP3（W3-A1） |
| A14 | `tests/golden/SCHEMA.md` 加性扩展 `warnings_exact`/`warnings_empty` 两字段 | R-040；v1 字段全保留，warnings_contain 逐字不变 | WP3（W3-A2） |
| A15 | `platform/paths.h` 末尾追加 `path_under()` | R-309 纯策略真源；空目录→空路径 | WP5（A1） |
| A16 | `FileListModel::Roles` 末尾追加 `StateToneRole`；`FileRow` 追加 `zero_byte_output`；新增 `set_zero_byte_output/clear_class_checks` | R-069/R-024/R-142；既有 role 序零改动 | WP6（T1） |
| A17 | `PreviewPanel` signals 追加 `navigated(int)`（仅翻图动作发） | R-151；既有信号零改动 | WP6（T2） |
| A18 | `MainWindow` private slots 追加 `on_run_again()` | R-006/R-146 | WP6（T3） |
| A19 | `ParamDef` 末尾追加 `std::string group` + `kGroup*` 词表 | UX-8 分组判据单源；聚合初始化兼容，未写=空串 | WP6（T4） |
| A20 | `src/ui/contextmenu.{h,cpp}`、`src/ui/term_words.h` 新文件（CMake glob 自动收编） | UX-1 公共菜单件 + R-139 终态词表单源 | WP6（T11） |
| A21 | `coord.h` 末尾追加 `kChinaBBox*` 四常量 | R-307 包围盒单源；coord.cpp/dms.cpp 双份字面量删除 | WP7（W7-A1） |
| A22 | `mapwidget.h` 末尾追加 `search_select_key(QString)` + 信号 `pin_selected(QString)` | R-223 稳定键对齐；`search_select/pin_clicked` 冻结行为零改动 | WP7（W7-A2） |
| A23 | `providers.h` 末尾追加 `parse_provider_search(...)`（冻结名 `parse_search` 转发） | R-158 同名反序消歧；冻结签名/行为零改动 | WP7（W7-A3） |
| A24 | `pipeline.h` 加性块：`OutputState`/`ErrorKind` 枚举、`OutputResult::state/error_kind`、`FileResult::error_kind`、`output_state_name/set_output_state/error_kind_name/error_kind_of/verdict_of_rows/finalize_aggregate/template_has_format/validate_run_config/path_ctx_for/default_bitdepth_for/build_output_specs/OutputSpecRequest` | B.8 契约收口（R-172/176/173/175/322/325）；均末尾/新增声明，既有签名零改动。**枚举声明插在 OutputResult 之前**（字段类型需要完整声明）——「新声明插入」非「修改既有声明」，如实披露 | WP8（裁定 1） |
| A25 | `scheduler.h` `RunSummary` 末尾追加 `peak_active_files/peak_sum_encode_threads/threads_total_est` | R-078 读数口径；加性 | WP8（裁定 3） |
| A26 | `progress.h` 加性 `OutputProgress` + `output_chunk_frac/file_frac_from` 纯函数 | R-284 算术单源；API 面零改动 | WP8（裁定 4） |
| A27 | `logger.h` 加性声明 `log_level_from_text` | R-157 单源 | WP8（裁定 5） |
| A28 | dev 三 TU 新建 + CMakeLists §4b（仅 PP_BUILD_DEV） | B.6；发布构建零 dev 路径 | WP8（裁定 7） |
| A29 | 跨 TU 衔接用 extern 声明（不新增第四个头文件） | WP8 任务书授权「新文件」仅列三个 .cpp（引述出处 `docs/m7r2-plan.md` 附 A） | WP8（裁定 8） |
| A30 | 新增私有共享头 `src/ui/mainwindow_impl.h`（非冻结面）承载 Impl 完整定义 + 壳层自绘件 | 探针 TU 需同一 Impl 视图；公开冻结面零增删 | WP9（W9-2） |
| A31 | 自由函数进头补 `inline`；Impl 追加 8 个分段方法声明 | W9-3/W9-4；机械 + 私有面加性 | WP9 |

## 二、行为变化（授权变更，逐条披露）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| B1 | **R-002 元数据写回失败 = 输出真失败**（原：吞成 warning + ok=true）；**编码产物不删除**（磁盘上是完整像素；删用户数据比保留像素更危险） | P0 修复明令；`test_pipeline_contract` 换锚 now-fatal | WP1（裁定 9） |
| B2 | R-029 GPS 时间戳非法**不再擦除**既有 GPSDateStamp/GPSTimeStamp（保留源值 + Warning） | P2 修复明令 | WP1 |
| B3 | R-005 软/硬保留双保险：Skip 判据 = 磁盘存在 \|\| 硬保留；Rename/Overwrite 避让面不变 | 防「幽灵跳过」；旧 4 参转发=旧行为 | WP1（裁定 14） |
| B4 | `class_file` 默认改惰性空串（原默认构造即 mkdir） | R-050；消费点「空=默认」口径使然 | WP1（裁定 15） |
| B5 | manifest `longPathAware` 用 SMI/**2016** 命名空间（元素自带 xmlns） | 误用 2019 前缀 SxS 拒启（实测踩坑修正） | WP1（裁定 16） |
| B6 | dct_method 锁定值取 **0**（表默认）而非实际 FLOAT(2) | jpegli 不读该字段（行为等价），锁 2 会改写参数快照/预设字节 ⇒ 回归零漂移优先 | WP2（W2-A8） |
| B7 | R-206 交叉校验保留 en/zh 双通道归属（不做全通道合并）——**R2 轮追记：本条已被 M7R2B-2 取代**（R-206「改道定稿」：通道归属退场、双通道共用规则，`5d19146`） | 合并 = 既有错误面变化，未裁定不做（R1 期口径；R2 期经任务明文改道） | WP2（W2-A9）→R2B |
| B8 | UX-14 A1 nclx 推导改 ICC 反推（默认 sRGB 情境字节不变；P3/Adobe 情境改 VUI 信令） | 金样 20/20 背书；heif/avif 回归差异归因本条 + B9 | WP2（W2-A10） |
| B9 | R-089 chroma 量化统一 quantize16（±1 LSB 级） | 平面间 1 LSB 口径差根治 | WP2（W2-A11） |
| B10 | R-011 float predictor 显式拒绝（lzw/zip 下） | 旧 = libtiff 写盘期英文底层错；新 = 明确因由 | WP2（W2-A12） |
| B11 | `CMakePresets` ubsan/dev `halt_on_error=0 → 1` | R-017 假绿通道；UBSan 报告即失败 | WP3（W3-A4） |
| B12 | `.clang-tidy` Checks 收敛门禁冻结口径（bugprone/performance/portability） | R-127 单源；有效检查集零变化（fingerprint 一致） | WP3（W3-A5） |
| B13 | `pp_mkfixtures --verify` 缺件 SKIP→FAIL | R-095；断言收紧 | WP3（W3-A6） |
| B14 | tsan.supp `race:QCallableObject` → `race:>::QCallableObject(`（收窄） | R-027；坏捕获类竞态恢复可见 | WP3（W3-A7） |
| B15 | ccache launcher 仅 GNU\|Clang 挂载（MSVC 不挂） | R-124 保守侧 | WP3（W3-A8） |
| B16 | make_appimage.sh 默认构建目录 build/m2-release → build/release | R-121 口径统一 | WP3（W3-A9） |
| B17 | vcpkg.json version 0.3.0→0.3.2 + 显式 libjpeg-turbo/tiff 依赖 | R-122/123；未升级任何 port；vcpkg 缓存键一次性失效需 warm-cache 重播 | WP3（W3-A10） |
| B18 | `test_metadata write-warnings` 断言换锚：失败=真失败 + out_warnings 通道（同严格度） | R-171 消灭 const_cast 的直接推论 | WP1（裁定 8） |
| B19 | UX-3 预览门槛拆分：`kPreviewEmbeddedMinLongEdge` 64→1600、`kPreviewCacheMaxPx` 2048→4096、`kPreviewCacheCapacity` 16→8（新增 `kThumbEmbeddedMinLongEdge=64` 保缩略图通道零变化） | UX-3 明令（冻结面语义变化，M0 冻结头加性块内改值） | WP4（T1） |
| B20 | test_thumbs「64px 门槛内」1 条 → 两通道分叉 4 条（断言只增，机械换锚）；LRU 上限断言改符号常量 | 随 B19；旧断言与新契约互斥 | WP4（T2） |
| B21 | 视觉/行为面变化一组：滚动条 10px→6px 细浮层（含弹窗新规则）、按钮内距 11/14→12、combo 右内距 26→24、page_meta 卡体上内边距 0→10、ElidedLabel 统一实现补悬浮全文 | UX-6/UX-11/UX-12 授权 | WP4（T3） |
| B22 | GUI-R2/运行页探针 `ppField` → `theme::kFkProperty` 换锚；「danger 档退役」否定断言扩三重不出现 | 钩子三轨收敛 ppFk 的随码换锚 | WP4（T4） |
| B23 | 判定修正：`paint_spin_arrows` 非死码（保留）、`go_text` 非死值（保留） | 评审判定修正，证据在案 | WP4（T5） |
| B24 | `hit_test` 命中序：三钮优先于边缘/角缩放带（顶栏 6px 只在三钮之外生效） | UX-7②/R-312 明令；UI-SMOKE 23 冻结采样点不受影响 | WP5（A2） |
| B25 | WM_NCCALCSIZE 客户区=整窗（原生标题栏/复古三钮消失）；最大化钳工作区 | UX-7① 明令；仅 Windows 真窗口生效 | WP5（A3） |
| B26 | `rect_of` 物理换算并入 `to_physical`（分数 DPR 下可能差 1px；dpr=1 字节等价） | R-308 取整单轨 | WP5（A4） |
| B27 | frameless Impl 裸指针 → QPointer | R-310；对外 API 零改动 | WP5（A5） |
| B28 | settings.ini `amap_key` 落盘格式：明文 → `ppsec1/ppsec2` 封存串（老明文读入兼容、首次保存升级）；`test_settings` 断言换锚为 save-no-cleartext+sealed+roundtrip（**收紧**非放宽） | R-094 明令；旧断言与 R-094 直接矛盾 | WP5（A6） |
| B29 | `settings_file/presets_dir/logs_dir` 空 data_dir 时返回空路径（旧：CWD 相对名） | R-309 明令；四调用方均已核实可容 | WP5（A7） |
| B30 | `harden_file` 非 Windows = chmod 0600；Windows = no-op true；DPAPI 失败兜底退编码档（不落明文、不丢密钥） | 数据安全优先级；「编码非加密」口径如实注释 | WP5（A12/A13） |
| B31 | test_icons 用 `PP_ICONS_TEST_NO_WIDGETS` 编译缝收窄 Widgets 依赖（生产构建照常） | 单测只链 pp_core 的既有约束 | WP5（A8） |
| B32 | **文件面扩展（主对话授权）**：`assets/icons/caption-{min,max,restore,close}.svg` 4 个设计源随 kDefs 同步重画（仅 path[@d]） | UX-7④ 正当文件面，同源锁步；icons 探针 bad-svg=0 | WP5（A9） |
| B33 | CMakeLists 对 secret.cpp 登记**两行**（pp_core 收录 + UI REMOVE_ITEM 剔除）而非 WP5 任务书的「一行」（引述出处 `docs/m7r2-plan.md` 附 A；该「一行」字面系任务书笔误，见九节误识别登记） | 两种单行做法均不可达（LNK2005 / 与 M2-T13 裁定相抵），如实登记 | WP5（A10） |
| B34 | DPAPI 导入库 `#pragma comment(lib,"crypt32.lib")` 收在 secret.cpp（不加 CMake 链接面） | 同 B33 约束 | WP5（A11） |
| B35 | 「再次运行」= 补齐语义（只提交缺件 + Skip）；仅元数据 GUI 默认冲突 = Skip | R-006/R-074 明令；**仅 GUI 路径**，CLI/金样默认面零改动 | WP6（T8） |
| B36 | 终态词表统一「已完成/部分成功/跳过/失败/已取消」（列表胶囊随之更名） | R-139；运行页胶囊文案逐字不变 | WP6（T9） |
| B37 | 运行期拒收拖放（setAcceptDrops(false) + 事件拒收） | R-071/R-154 | WP6（T10） |
| B38 | ui_smoke 走查 stdout 改非缓冲（内容零变化） | 崩溃时探针行不整批丢失 | WP6（T12） |
| B39 | `tab-label` 探针换锚（去「位」断言）；`bitdepth-lock` 第四合取项换 `!contains("位")`（控制面三项逐字保留） | UX-9 §16-1 口径（只去页签位深文案）；原「逐字保留」与该口径不可同存，只摘该合取项并登记 | WP6（T5） |
| B40 | `static_assert(kStateStyle == 12)` → `== 14` | 映射表须覆盖 FileState 全枚举（语义同一） | WP6（T7） |
| B41 | 分类菜单文案「清空勾选」→「清空该类勾选」（探针 T13 同步换锚） | R-142；菜单项集合/顺序不变 | WP6（T13） |
| B42 | `end_run` 失败档结束语只在**全批失败**时启用 | R-073 原文口径；`single-source` 探针逐字钉住的句子不动，行为收窄 | WP6（T14） |
| B43 | UX-9 页签等宽实现改案：自绘定宽 → `QTabBar::setExpanding(true)` 平摊 | 自绘方案两处不成立（右端露白判红 + tabSizeHint 布局反馈环栈溢出实测）；「按最长格式名定档」细节以等宽为准，登记为已知差异 | WP6（T15） |
| B44 | Geocoder `reverse_url` 入参语义换锚为 WGS-84（边界内转换） | R-219 同结构内约定矛盾的修法；test_geocoder 换锚（强度不降） | WP7（W7-A4） |
| B45 | layers 子域机制换锚：atomic 轮换 → `(x+y+z) mod 4+1` 纯函数 | R-220 兑现「纯函数 URL 事实表」；同瓦片恒同子域；test_layers/test_providers 换锚 | WP7（W7-A6） |
| B46 | UX-4 版面删除 3 个 `-row` objectName（category/layer/address）；其余 objectName 全保留；新增 6 个 objectName | ux-review §4 已定档；`-row` 行距探针自动少 3 行（floor ≥12 满足） | WP7（W7-A8） |
| B47 | UX-5 语义：GPS 卡关 = 规则编辑面/清除钮置灰，**取点面保持可用**（选点/联想自动勾上保留） | 任务书明确保留（引述出处 `docs/m7r2-plan.md` 附 A）；meta-loop 探针正是关态取点 | WP7（W7-A9） |
| B48 | clear_gps 不再脱离卡开关独立生效 | 「关闭=整卡不参与规则」直接推论；含值旧状态恢复=开 | WP7（W7-A10） |
| B49 | R-315 只接 `tiles_pending_changed`（加载 chip）；`search_finished` 不接 | 同批数据双接会刷两遍状态行；冻结信号仍发 | WP7（W7-A11） |
| B50 | mainwindow.cpp 探针换锚（136→260 三处 + (i-6b) 只增分类菜单断言 + R-262 注释） | UX-4⑤ 定档使旧锚必假；任务清单显式指派 | WP7（W7-A12） |
| B51 | 分类按钮文案「分类 ▾」→「分类」 | ▾(U+25BE) 在 §3.3 禁字形清单（ui_smoke 全局扫查实证命中） | WP7（W7-A13） |
| B52 | TileProvider 僵尸层**保留并标注**（不删）；`search_url/tile_url` 标注「冻结遗留、仅测试可见」 | 冻结头只加性，删除属解冻；R-060 口径落定 | WP7（W7-A5） |
| B53 | `pipeline.h` 删除未使用的 `#include "codecs/encoder.h"` | R-106/R-286；全树重编通过（无消费者依赖该传递包含） | WP8（裁定 2） |
| B54 | 错误收尾口径统一（R-162）：编码器失败不再 log_error（pipeline 行保留）；Timing 成败皆填 | 仅失败路径受影响 | WP8（裁定 10） |
| B55 | 校验前置（R-173）：unknown-format/bitdepth 错误在 probe 前报出；多错误配置首条消息可能不同 | 旧口径下无断言覆盖 | WP8（裁定 10） |
| B56 | `--dev` 失败文案带格式前缀；日志 `[file]` 标签迁移 `[main.cpp]`→`[dev_cli.cpp]`（≈180 行，行为 0） | B.6 TU 拆分机械推论；回归基线归因即此（M7-GATE 已按此重播种） | WP8（裁定 10） |
| B57 | `mainwindow.h:45` 过期成员清单注释修正（**仅注释**，签名零改动） | R-257；冻结头前言允许注释更新 | WP9（W9-1） |
| B58 | `page_run.cpp` `out_completion/run_frac` 拆「纯核 + 旧签名转发」（值语义逐字一致） | B.2 可单测口径 | WP9（W9-5） |
| B59 | 抽出的 tr 面 `PageRun::tr` → `QCoreApplication::translate("PageRun",…)` | 头不依赖 Qt Widgets；上下文名同名 → 译文查找面逐字一致 | WP9（W9-6） |
| B60 | `fmt_duration(61s)` 期望 = `01:01`（修正我方期望而非放宽实现） | 复核实现本就补零；断言锚定既有行为 | WP9（W9-7） |
| B61 | warn_preset_op/PresetOp 由死码接线进预设 CRUD 5 个 QMessageBox 点（文案逐字不变） | R-240 家族注释失实的收口 | WP9（W9-8） |

## 三、保留/不删裁定（评审建议与冻结面/证据面冲突）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| C1 | R-282 `write_metadata_exiv2` 的 BMP 分支**保留** | 评审称「不可达」只对 pipeline 生产路径成立；它是 §5.8 公开 API 文档化契约且被 `test_post_write_formats` 钉住；删除=改冻结契约+减证据面 | WP1（裁定 11） |
| C2 | R-282 `fsops::with_extension` **保留** | PP-FROZEN 声明 + test_fsops 4 例覆盖；删除同时违反「冻结面只读」与「断言只增不减」 | WP1（裁定 11） |
| C3 | R-210 classify.cpp 内嵌 JSON 栈**保留**（标注真实边界：接受/拒绝面、深度上限 32） | core Qt-free 语义面 + 禁新增依赖 + test_classify 全量覆盖 | WP1 |
| C4 | R-097 test_metadata PNG 分支加硬断言 `png_ok==true`（能力降级即判红） | 断言收紧方向 | WP1 |
| C5 | R-127 `HeaderFilterRegex` 扩面**不做**（留裁决） | 扩面新增历史诊断 ⇒ 棘轮计数变化 ⇒ 须 `--update` 重播种（口径动作须先确认） | WP3 |
| C6 | R-044 口径如实化（ui_smoke 头注：截图仅存档、硬判在 C++ 探针）；像素容差本体在 C++ 探针（跨界） | 「评审与改动分离」：口径文书先落，容差补强归 UI 面 | WP3 |
| C7 | R-158 `parse_search` 冻结名保留转发，不改名 | 冻结签名零改动 | WP7（W7-A3） |
| C8 | R-325 非空容器包装类（metadata_only↔outputs.size()==1 强类型）**不做** | 会改冻结签名；以 validate_run_config 校验器 + set_output_state 单点替代。若走包装类型需一次性解冻裁定表 | WP8 |
| C9 | DevOptions has_* optional 化（R-272 可选项）**不做** | 纯可读性收益、无判据面，避免无谓 churn | WP8 |
| C10 | R-218 RTL 全清**不做**（本波） | 与「行为零变化重构」硬约束正面冲突且需视觉取证；建议独立任务包带 RTL 截图探针。**R2 轮追记**：已按此建议由独立包 R2E2 落地（`52b9870`，逻辑坐标+单点镜像，LTR 逐字节不变，`rtl-mirror` 探针钉镜像关系），见八节 M7R2E2-1/3 | WP9 |

## 四、WP11 长尾清扫裁定（本轮汇总）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| D1 | R-283 余量注释修实：`fsops.h`「10 种输入」→「13 种扩展名」；`settings.h` `workers` 注释「0=物理核数」→「0=自动（线程预算或逻辑核数）」 | 注释失实，注释级修复零行为 | WP11 |
| D2 | R-157 尾巴注释：`settings_dialog.cpp` 「与 main.cpp 的 parse_log_level 一致」→ 指向单源 `pp::log_level_from_text` | main.cpp 该函数已随 B.6 拆分删除，注释失实 | WP11 |
| D3 | R-295 余量：`paramform.cpp` 头注释的 `"__lossless"` 字面量改引 `pp::kLosslessKey` 单源 | 注释级；代码读取点本就走单源 | WP11 |
| D4 | R-272 余量：`preset_io.cpp` 魔法数 `9.007199254740992e15` 命名 `kMaxExactInt`（2^53） | 常量命名级；语义逐字不变 | WP11 |
| D5 | R-242 注释修实：`presets_dialog.h` `path()` 注释「SaveAs 返回 sanitized 路径」→ 「SaveAs = 空串（目录由调用方推导）」 | 与实现相反（presets_dialog.cpp:14 已自述空串），注释级 | WP11 |
| D6 | R-268 注释修实：`icons.cpp` 未支持命令字母「跳过」→「截断返回已解析部分」；`paramform.h` 前置声明理由（theme.h 需包含者先引 QApplication）改按 M5-A5 现状（theme.h 已自含） | 注释与实现相反/过期，注释级 | WP11 |
| D7 | UX-6① 余量：`docs/qt-gui-design-spec.md` §5 R1 显式登记两档例外——列表行 36（kListRowH）/ 表头 24（kTableHeadH），不属「信息行」 | WP4 跨界需求 1 的落点（docs 面归 WP11）；新增例外须先修订该节 | WP11 |
| D8 | R-115 README ctest 计数**改实**：以 `ctest -N` 实测为准（2026-10-12：release 34 / release-dev 35=34+ui_smoke，两树各由 WP11 本机配置实测）；「33 条」为评审日（2026-10-11）口径（出处 `docs/ux-review-14.md:5` 基线 33/33，WP11 任务书沿用——引述出处 `docs/m7r2-plan.md` 附 A），M7 中 test_icons/test_secret 入编后已增至 35 | 如实取数优于抄旧数；同时给「以实测为准」的非计数口径防再漂 | WP11 |
| D9 | R-121 README 设计取舍登记：CMakeLists 无 `install()`、打包器直读构建树，两器默认构建目录统一 `build/release`；引入 install() 属构建面重构不在本轮 | WP3 跨界需求 2 的落点 | WP11 |
| D10 | R-078 发行说明口径入 CHANGELOG/README：线程读数为估算（`active ≤ W ≤ T`、`ΣE ≤ T`；SynthPump/x265 不入账；threads_total_est 非硬上界）；README 旧句「总活跃线程 ≤ T」改实 | WP8 跨界需求 3 的落点 | WP11 |
| D11 | R-292 词表/枚举镜像 13 组**对账登记**（逐行：收敛 5 / 大部收敛 1 / 部分收敛 1 / 登记 6；二分摘要口径「收敛 6 / 登记 7」= 大部收敛并入收敛、部分收敛并入登记），明细见 `m7-report.md` 附录 C；不改逻辑（镜像收敛=结构性重构，超出注释/文案/常量命名级授权） | 红线 3「逻辑改动一律登记不改」 | WP11 |
| D12 | README/CHANGELOG 用户口径**不写未发生的功能**：色彩可选项（色域映射/抖动/渲染意图/线性光重采样）接口数据面已备、行为未入树（`(void)opts;`），**不列为新功能**，仅在「已知」如实披露 | 「诚实显示」纪律；WP11 任务书 README 清单含该四项（引述出处 `docs/m7r2-plan.md` 附 A），但 WP10 未交付，如实上报不照抄。**R2 轮追记**：C1–C4 行为实现已由 R2A（`c540630`）交付（默认关=旧行为逐字节一致），本条口径随之成立性转移：四能力入「新增」如实记录，**GUI 开关仍未接线**（不宣称界面新选项）——见八节 R2A 各条。**R2.1 追记（M7R21F4-4，`6ff43f9`）**：GUI 开关**已接线**（page_output 全局卡四控件，opt-in 默认关 = 现行为）——「GUI 开关仍未接线（不宣称界面新选项）」一句自该时点失效，界面已如实呈现四选项。**R2.2 校读修订（口径限定）**：「默认关=旧行为逐字节一致」的比较基线按 m7-report §6.2-1 降准口径**限定为 R2A 实施前的树**，非 0.3.2 发行面（M7/R2 默认路径另有两处已归因字节变化：UX-14 A1 nclx +20B/文件、B9 chroma ±1 LSB） | WP11 |

## 五、遗留裁决点（等主对话，本轮不决；R2 轮终态已注记于各行）

| # | 事项 | 现状 | 建议 | R2 终态 |
|---|---|---|---|---|
| E1 | UX-14 A1 精确码：P3 目标下 VUI CICP 实测 unspecified(2)（期望 12） | 核心判据（不得写死 sRGB）已断言；库侧 nclx 状态机所致 | 如需精确 12 要给 libheif 传 `output_nclx_profile`（改输出字节）——裁定后另起 | **已修**（R2A `c540630`：显式传 `output_nclx_profile`，位流直读 P3=12/sRGB=1；字节变化 +20B×31 已归因重播种） |
| E2 | BMP 不承载 ICC（OIIO writer 事实） | 测试钉现状（一致地未嵌入） | 产品口径（警示/放弃）待定 | **受约束口径闭环**（R2A B3：`EmbedIccUnsupported` 警示口径 + keep 不虚报；表内 tooltip 无可标注面，见九节） |
| E3 | R-163 取消错误归类：编码器 cancel 返回 error，pipeline 归 Failed | UI 侧已按逐输出事实补判 | 引擎侧改判 Cancelled 待裁定（行为面） | **已修**（R2A `c540630`：取消归类单点 → `ErrorKind::Cancelled` → `Verdict::Cancelled` 短路，四面一致断言） |
| E4 | R-211 强类型位深（bps/bpp 拆分） | 已显式化注释/文案 | 拆字段涉 UI+pipeline 跨面，待一次性裁定 | **登记**（R2B `5d19146` 已落单位单源 `bitdepth_unit/label/bpp/bps` + UI tooltip 消费；拆字段维持待解冻，M7R2B-5） |
| E5 | R-007 长路径 `\\?\` 归一 | 预检+manifest 已落；写路径无归一（259 硬线） | codecs 面归一后放宽硬线 | **已修**（R2B `5d19146`：`normalize_extended_path()` 统一写入口 + 259/255 硬线解除；**R2.1 追记（`93001a9`）**：余量 codecs 侧 fopen / resolve_conflict exists 探测已归一 `normalize_extended_path`——**E5 余量清零**，与 m7-report 附录 A R-007 行一致。**R2.2 校读修订**：早稿「余量…登记」系 R2B 时点快照未随 R2.1 追记，已改实） |
| E6 | WP5 真机验收 10 项（Snap/Aero/圆角/多显示器…） | offscreen 无法替代 | Windows 真机人工签收清单（m7-report §2.4） | **登记**（外部条件未达，待人工签收） |
| E7 | Linux 回归基线重播种 | Windows 机不做文本替换移植（R-015 反面先例） | Linux 宿主 / nightly update_baseline 通道 | **受约束口径闭环**（宿主能力约束：R2 轮 132 行重播种同未移植，归 Linux 宿主/nightly 通道，见九节） |
| E8 | regression.py:335-337 行型表钉 `[main.cpp]`（TU 迁移后误分类） | 归因护栏自计数失真已人工纠正口径 | 修分类器=改判据工具，另行裁定 | **已修**（R2B `5d19146`：行型表锚点扩新 TU 名集 + `--self-check` 19 例机械化钉住） |
| E9 | R-296 CICP 映射收敛：`src/core/colormanager.{h,cpp}` 的 `map_cicp_source`/`cicp_profile_spec` 分支驱动（加一对 = 4 处联动），建议表驱动单源 | colormanager 跨面（WP2 跨界需求 1），无包认领 | 表驱动重构待裁定后另起 | **已修**（R2A `c540630`：`kCicpTable` 单源，加一对 = 加一行，test_cicp_mapping 全绿） |

## 六、终门禁裁定（WP12，2026-10-12）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tidy-reseed | **tidy 棘轮基线重播种**（`tools/baseline/clang-tidy.tsv`，283/62 → **264/71**）：`ci-tidy.py --update` 落盘，基线 diff 与本提交同在；**frameless.cpp 5→6 为唯一一处既有条目级放宽**（Win32 `reinterpret_cast<HWND>(winId())` / `NCCALCSIZE_PARAMS*` 等 int↔ptr 平台互操作惯用式 + `attach(QWidget*,QWidget*)` 同型参数对，发行门禁期不做签名重构）；总告警 283→264 严格收紧；17 条残余逐条归因（M7 拆 TU 机械迁移 6 / M6 未入基线存量 2 / 发行期禁改的异常路径 2 / 平台惯用式与有意为之 7；等价分解=非 frameless 11 条 + frameless 6 条，见 m7-report §3.7），明细见 `m7-report.md` §3.7 与 `build/m7-wp12/handoff.md`。**NOLINT 抑制案否决**（仓库零先例、抑制注释在关键位点埋回归盲区）。**R2 轮追记（R2G `6a6e043`）**：「NOLINT 案否决 / 仓库零先例」口径已**不再成立**——R2G 终门禁 tidy fallout 采行级 NOLINT 消音 11 条（八节 R2G-G2），本仓自此有 NOLINT 先例；两裁定不矛盾但前后有别：WP12 期否决的是「为压计数而加抑制」，R2G 期在「棘轮只减不增、禁 `--update`」约束下选定「修码 + 消音」（含翻案替代方案与成本），逐条理由注释在代码内。**R2.1 追记（M7R21F4-0，R2G 消音翻案）**：R2G 的 11 处行级 NOLINT 已**全部撤销**（`M7-R2.1-F4`）——同型形参对改参数组结构化（`DitherGrid`/`ScanTally`/`Tally`/`EventRow`/`GeoRow`），Win32 int↔ptr 惯用式改显式安全转换封装（frameless `hwnd_of`/`nccalc_params_of`，std::bit_cast 位拷贝逐位等价），AVX2 内核 6 条 + QtConcurrent 任务 lambda 1 条无零行为修法，按本裁定口径 `ci-tidy --update` 重播种 + 逐条归因（十一节 M7R21F4-0/-0-1 附表；两次重播种 264/76 → **263/75**，总量 264→**263** 只减不增，终读数 `TIDY total=263 baseline=263 files=75 violations=0 seed=0`）。**「仓库零抑制」自此恢复成立**；R2 追记「本仓自此有 NOLINT 先例」仅覆盖 `6a6e043` 至本次翻案之间的历史区间，WP12 期的「否决 NOLINT / 零抑制」口径重新为现行口径。 | 主对话裁决（escalate 问答，条件五条：总量只减 / frameless 放宽逐条理由+本表登记 / 归因随提交正文 / 复跑 exit 0+读数入报告 / 不动 CI 判据与检查集、不加抑制）；触发背景：B.6 四 TU + mainwindow 五域拆 TU 把 mainwindow.cpp(14→1)/main.cpp(8→1) 既有告警机械迁入 10 个基线外 TU，file-keyed 棘轮无法表达「告警随代码搬家」，与「新文件须 0」口径不可兼容 | WP12 |
| WP12-B1 | tidy 棘轮 fallout 的 **38 条修复全部行为零变化**（显式转换 / 删未用量 / 枚举基型 / 按值拷贝 / 字符串拼接序 / 舍入式拆命名临时量保持算子序），金样 20/20 + 回归 zero diff + ctest 35/35 双背书 | 零行为机械修复 | WP12 |
| WP12-B2 | **行为变化（测试面）**：4 个新单测（test_geocoder/test_icons/test_providers/test_secret）的 `main` 包 try/catch——**异常逃逸路径**由 terminate（崩溃）变为打印 `FAILED (unexpected exception)` + 返回 1；仅测试代码、仅崩溃路径，成功与断言路径逐字不动，ctest 判定不变。产品代码的 exception-escape（colormanager `~ErrSync`、mainwindow_run 事件回调 λ）**未动**（修法=吞异常=行为变化，主对话裁决「发行门禁期禁改」） | tidy `exception-escape(main)` 告警的唯一非零行为修复；行为变化按 §6.2 纪律入本表（m7-report §3.7 引用即此条） | WP12 |

## 七、发行裁定（WP13，2026-10-12）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tag-move | **tag v0.3.3 发布前移动一次**（97cd9da → b1af371）：首落点 97cd9da 树缺 `vcpkg.json` 版本同改（R-122），release.yml「依赖单源一致性断言」6s 红、publish 未执行、**tag 零 Release 零资产** ⇒ 该 tag 不是可发行树。移动前后阴阳性对照在案（97cd9da 树 `ci-versions-check` FAIL，与 CI 报错逐字同因 / b1af371 树 OK (10 keys, expect=2)）；移动操作原子（`git push --force origin refs/tags/v0.3.3`，`ls-remote` 核对 tag 对象 `cb181ee` → `^{}` = `b1af371`）；移动前取证：gh CLI 未认证、releases 列表无 v0.3.3、/releases/tag/v0.3.3 仅 2 个自动源码包（真发布 6 资产，如 v0.3.2）。**裁决人 = 主对话（escalate 问答，条件七条：取证 / 阴阳性对照 / 原子 + ls-remote 核对 / 跟到 publish / 再红即停 / 本登记 / 如实定状态）** | ① 用户本轮指令 = 发布 0.3.3，发布不完成即未交差；② 权威口径 = AGENTS.md §9「tag 资产挂载后不可再移动」+ release.yml 自述「tag 未发布资产前可移动，发布后不可移动」——移动时 tag 零资产零 Release，处明文允许区间；③ 旧目标 97cd9da 仍在历史、b1af371 为其后续修复提交，可逆 | WP13 |
| WP13-B1 | **`vcpkg.json` `version` 0.3.2 → 0.3.3 与 `CMakeLists.txt:19` 同改**（跨出 WP13 文件面的机械同改，落点 `M7-REL-0.3.3-gate-fix2` = b1af371）：`tools/ci-versions-check.py:12-13` 契约注释原文即预点名「R-122：版本 bump 必须同改；WP13 的 0.3.3 bump 会在这里被钉住」——属版本单源 bump 义务；`builtin-baseline`/`dependencies` 逐字未动 | R-122 断言自 M7-WP3 起在 CI 钉住；首推 tag 时该项红灯后补齐，阴阳性对照在案（gate-fix2 提交正文） | WP13 |

## 八、R2 轮裁定登记（M7-R2，2026-10-12；方案真源 = [docs/m7r2-plan.md](m7r2-plan.md)）

> 各包 handoff「裁定登记」节汇总；任务书出处 = `docs/m7r2-plan.md` §2/§3。验证读数摘录见
> `docs/m7-evidence/m7r2-*-gates.md`。

### 8.1 R2A（`c540630`，色彩能力与解码语义；14 项设计实施 = plan §2）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| R2A-A1 | 加性（PP-FROZEN `core/types.h`） | `WarningKind` 末尾追加 `EmbedIccUnsupported` | B3 设计明示「WarningKind 可加性追加」；既有值数值/语义零变化 |
| R2A-A2 | 加性（PP-FROZEN `codecs/encoder.h`） | `EncodeRequest` 末尾追加 `bool dither8 = false` | C2 通路必经（RunConfig→to_u8）；W2-A1 先例；聚合初始化兼容 |
| R2A-A3 | 加性 | `decode/oiio_reader.h` 末尾追加 `exif_colorspace_from_spec` | D2 设计指定暴露面 |
| R2A-A4 | 加性 | `thumbs.h decode_preview_detailed` 末位缺省参数 `bool linear_resample=false` | C4 加性；默认路径逐字不变 |
| R2A-A5 | 行为（新增档位） | C3 意图映射 1/3 不叠 BPC（仅 0 携带、2 明示不叠） | 设计字面；默认 0 不变 |
| R2A-A6 | 断言换锚（同严） | D3 HDR miss 文案换锚（transfer 16/18 → HDR 明确文案）；test_color `misses[]` 7 改 3 增 | 设计给定文案逐字；等值断言非放宽 |
| R2A-A7 | 断言定标 | C1 护栏显著性：mode2 ≤ 0.05×（构造性）；mode1 满饱和 max/mean ≤ 0.85×、中饱和 max≤0.6×/mean≤0.5× | knee=0.85 固定公式下的实测定标（读数见 m7r2-a-gates）；断言只增不放水 |
| R2A-A8 | 断言面选择 | A1 判据取**码流 VUI 位流直读**，解码侧 `heif_image_get_nclx` 仅对照留证 | 解码侧回填随库漂移（实测 P3 位流=12 而回填=2）；判据原文=码流 |
| R2A-A9 | 算法口径 | C1 mode2 = Lab 空间 L 轴收敛（保 L、压 C） | 设计「向明度轴收敛」字面；RGB 域备选实测色相偏 45° 否决 |
| R2A-A10 | 测量路径换锚 | 跨库对拍 Rec709 侧用 OIIO `color.h` 曲线（本 OIIO ColorConfig 无 "Rec709" 名，实测） | 同库同曲线，对拍语义不变 |
| R2A-A11 | 登记（量化面限定） | C2 抖动**只接** jpegli/webp/jxl/oiio 四家 to_u8 量化段；heif/avif 的 8bit 输出走 `quantize16(maxv=255)` 面**不接**（扩面需 `quantize16_dithered`，另裁） | plan §2-2 任务面口径；如实限定防「全部格式生效」误读；默认关下无行为差异 |

### 8.2 R2B（`5d19146`，核心与参数域尾单）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R2B-1 | 加性（PP-FROZEN） | `scheduler.h` 尾部 `file_state_of()`；`params.h` 尾部 `find_param/lookup_param_def/BitdepthUnit/bitdepth_unit(_label/_bpp/_bps)`；`fsops.h` 尾部 `normalize_extended_path()` | R-076/195/211/007 单源收口导出面；末尾追加先例同 WP2/WP5/WP8 |
| M7R2B-2 | 行为变化 | 跨字段规则通道归属退场：validate_params 与 cross_validate 共用全部规则（各自语言文案）——**取代二节 B7**（R1 期「保留双通道归属」口径的追认链终点） | R-206「改道定稿」任务明文；en/zh 冻结文案逐字保留；B7 ↔ 本条双向互指 |
| M7R2B-3 | 行为变化 + 断言换锚 | `validate_output_filename` 解除 259/255 长度硬线；test_fsops `invalid/*-too-long` 换锚 `r007/long-*-accepted` | R-007 任务明文；旧断言与任务直接矛盾（WP5-A6 先例，非放宽） |
| M7R2B-4 | 行为变化 | validate_preset 补 cross_validate 判定；fsops/metadata 写入口归一 `\\?\`；settings map_provider 词表外回落 "osm"；default_class_file 空 data_dir 返回空串 | R-231/R-007/R-104/WP5 跨界 1 明文；默认/常规路径字节级不变 |
| M7R2B-5 | 登记（维持待解冻） | R-211/R-325 强类型路线（bps/bpp 拆字段、metadata_only 非空容器包装）**不做** | 会改冻结签名；维持 WP8 C8「登记待解冻」，需一次性解冻裁定表 |
| M7R2B-6 | 新文件 | `src/core/error_text.h`（header-only 中文文案单源表） | R-070/273 指定新头；引擎错误串零改动 |

### 8.3 R2C（`210fce5`，CI 与发布链）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| R2C-W1 | 登记（不补） | 不给 Windows job 补 ccache save/restore | R-124 MSVC 不挂 launcher ⇒ CCACHE_DIR 恒空；补对称步骤=存/取空缓存伪命中（造假不属韧性） |
| R2C-W2 | 修复口径 | warm-cache.yml 删非法空 `env:` 键（保留归因注释），**不**恢复 `QT_DIR:` 值行 | 恢复值行回退 R-063（版本字面量，ci-versions-check 实测判红）；GITHUB_ENV 注入等价 |
| R2C-W3 | 登记（不改判据） | 不改 `PP_VCPKG_WARM_MIN=20`、不改 timeout-minutes、不改任何断言口径 | 硬约束「timeout 不许改大」；暖度假绿缺口（计数≠ABI 命中）登记进 runbook §1.2，不伪加固 |
| R2C-W4 | 定性修正 | 任务书对 warm-cache 故障的定性（「孤儿 PowerShell 块」）与实况不符——实为 247 行空 `env:` 键（YAML `env: null`）；按实况修并双向留证 | 原字节 dump 证明 `$lines` 块在合法 step 内；空键与 Line 247、#9–#13 秒败时间线精确对位 |

### 8.4 R2D1（`6e001da`，主窗/列表/预览/分类/page_meta）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7-R2-B | 断言换锚（**主对话授权**） | `smoke_probes.cpp` 两处 `map->height() == 260` → `>= 260`（geometry 逐位断言逐字保留；其余 smoke_probes 逐字不动） | UX-4 stretch 后 260 是下限非定值；escalation 裁决 A 改良版 |
| R2D1-A1 | 行为定义 | R-143 选「给过滤语义 + .on 拆态单义」（弃「选中项归属高亮」通道，归属由行内色点承担） | UX 评审二选一取同时修「二义」与「点击死端」者 |
| R2D1-A2 | 行为约束 | add_paths 扫盘后台化，但 `PP_UI_NO_STATE`（smoke/dev harness）下保持同步 | dev_ui_smoke 无等待点，异步会成竞态；判据确定性优先 |
| R2D1-A3 | 冻结面删除（追认） | `filelistmodel.h` 移除零消费死 API `collapsed_keys()`（R-278 授权；全仓 grep 仅定义处） | 死码清理指令 vs 冻结面只加性：删除零消费声明不改行为，登记追认 |
| R2D1-A4 | 加性 | FileRow 追加 `dedup_key`、FileListModel 追加 `unreadable_count/apply_scanned_paths`、FileDelegate 追加 `visible_row_rect`、role 基址命名（值不变）、ClassifyPanel 追加 `class_filter_requested`（`set_current_class_id` 随 .on 拆态退役） | 冻结面「加性 + 追认」先例（WP6 T1 同模式） |
| R2D1-A5 | 语义选择 | UX-1 语义（时间·从文件读取=跟随项重读；GPS·地图选点=大地图点选；GPS·取地图中心=既有按钮链路；时间·清除规则=复位+退启用） | 评审表词组与既有按钮非一一对应，按最贴近既有收口落，避免第二套行为 |
| R2D1-A6 | 图标单源 | icons 追加 `CheckAll`（枚举末尾）+ `assets/icons/check-all.svg` 同形设计源 | 图标单源契约（C3）：SVG 与 kDefs 逐元素同形，icons 探针硬判 |
| R2D1-A7 | 行为（补齐分支） | run-again 前删除 0 字节产物（仅 size==0、仅补齐分支、源同实体绝不触碰） | 「自家残次品重做」最小实现；红线守卫 = fs::equivalent |

### 8.5 R2D2（`27c9f40`，输出/运行/表单/编辑器/设置）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7-R2-A | 断言换锚（**主对话授权**） | ui_smoke `spin-group` 期望宽 `kFieldShort(88)` → `kFieldStd(160)`（一处断言三常量 + 注释；其余逐字不动） | UX-8 定稿「废除折叠=88 第二档、与核心一致 160」；语义等价、严格度不降 |
| M7R2D2-1 | 加性（PP-FROZEN `paramform.h`） | 追加 `limit_enum_choices(key, allowed, reason)` | UX-10.1 色度限幅需收窄枚举视图；末尾追加式 |
| M7R2D2-2 | 行为变化 | 预设落值路径跳过 avif+alpha 预选（R-066）；预设后端往返保真 | 「preset 应用路径跳过预选或设 override 语义位」；load→save 字段改写属数据保真缺陷 |
| M7R2D2-3 | 行为变化 | 折叠区数值行宽 88→160；子分组默认只展开首组；format_changed「若变」才发；alpha 提示两态如实；模板示例只取一份样本；非确定路径主题回滚 | 均为任务书/评审明文；ui_smoke 35/35 + 金样 20/20 背书 |
| M7R2D2-4 | 复核零增量 | R-229 维持 WP6 已修结论（remove_tab 先快照） | `git log -S "R-229"` = d99df1b |
| M7R2D2-5 | 加性（theme） | `kFkWarn` 钩子 + QSS 规则；Metrics 补 menu_*/tab_* 常量（R-270 余量令牌化，取值逐值相同） | X5 任务明文 + R-270 余量；色值/几何零变化 |

### 8.6 R2E2（`52b9870`，RTL 与结构拆分）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R2E2-1 | 加性（PP-FROZEN `filelistmodel.h`） | `row_rects(..., bool rtl)` / `check_rect(..., bool rtl)` 末尾追加式重载；旧签名转发 = LTR 逐值相同 | 绘制/命中/探针共用同一镜像几何单源 |
| M7R2E2-2 | 加性（`mainwindow_impl.h`） | Impl 末尾追加 5 个 `smoke_probe_*` 声明（PP_BUILD_DEV 段内） | 第 3 波常驻探针；发布构建零影响 |
| M7R2E2-3 | 行为变化（仅 RTL 路径） | 自绘几何/内距/QSS 非对称值/页签方向键随布局方向镜像 | R-218 明文；LTR 逐字节不变（ui_smoke + 金样背书） |
| M7R2E2-4 | 行为变化（隐藏面） | `FileGroupProxyModel` dataChanged 转发扩为两列区间 | R-203 明文缺陷；行数/roles 语义不变 |
| M7R2E2-5 | 断言换锚（同严） | settings_geom_probe QSS 断言换空白宽容正则（属性名/值/色值逐字） | R-281 明文；严格度不降 |
| M7R2E2-6 | 死码删除 | page_output 四件 index 映射函数退役（itemData 单真相） | R-190 明文；读回落值语义逐值相同 |
| M7R2E2-7 | 新文件 | `src/ui/format_policy.h`（B.3 纯核）、`src/ui/leading_indent.h`（R-218 逻辑边距） | header-only（R-109 路线）；glob 自动收编，CMakeLists 零改动 |

### 8.7 R2G（`6a6e043`，终门禁复核；原始登记 `build/m7r2-g/handoff.md`，按 plan §4-9 汇入本表）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| R2G-G1 | 判据处置 | tidy 新增告警**不走 `--update` 重播种**，走「修码 12 条 + 行级 NOLINT 消音 11 条（带契约注释）」 | 任务判据「棘轮只减不增」：`--update` 264→287 = 只增 = 放宽判据面；消音为注释级零语义面，基线文件零改动 |
| R2G-G2 | **NOLINT 新惯例首次进仓**（与 M7-ADJ-tidy-reseed 的「否决案」前后有别，见六节 R2 追记） | 消音 11 条 = `kernels.cpp` AVX2 intrinsics ×6 + `dithered_range` 形参组 ×2 + `collect_from`/`expect_card_menu` 出参对 ×2 + QtConcurrent 任务 lambda ×1；每处注释写明理由与出处，只覆盖 R2 新增行 | 三类理由：①结构性误报（AVX2 硬线，AGENTS §1.4）②同文件基线内同类先例口径 ③探针签名冻结形态。**翻案替代方案**（成本=换处置+重跑 tidy）：a) 形参组结构化小 struct；b) 主对话授权 `--update` 重播种 + 归因 |
| R2G-G3 | 行为零变化 | `apply_gamut_mapping` 形参序调整（colormanager 非冻结面，函数在 cpp 匿名空间） | 金样 20/20 + 回归 zero diff 证明输出逐字节不变 |
| R2G-G4 | 回归首跑 DIFF **不重播种** | 归因 0 行真实变化（OS 瞬态文件锁 `Access is denied` @ `fsops.cpp:541` rename，同二进制复跑 ×2 zero diff） | R-015：重播种仅用于真实行为变化；瞬态环境噪声不落基线 |
| R2G-G5 | **D-1（实现期发现，AGENTS §9 编号系首例）** | 输出名碰撞（`base/rgb16.png` 与 `base/rgb16.tif` 的 `$dir/$file` 模板同产 `rgb16.jpg`，conflict=overwrite）的 rename 替换路径对 OS 瞬态锁敏感，与并发负载同现一次 | 登记为已知差异：**未加锁重试机制**（行为新增，超 R2G「修复 fallout」范围）；根治=原子提交加锁重试，待裁定。**R2.1 追记：已根治（M7R21F1-2，`0555947`）**——`commit_temp_file` rename 有界重试（仅 Windows 共享违规类 5/32/33、≤3 次、退避 25/50/75ms；非瞬态与 POSIX 一次失败即出，失败消息/返回值口径逐字不变），D-1 从「登记」转「已修」（十二节 M7R21F1-2） |

## 九、误识别关闭与受约束登记（R2 轮四类审视，plan §1）

| # | 类 | 条目 | 事实与处置 | 终态 |
|---|---|---|---|---|
| R2F-X1 | 误识别关闭 | **R-282 死码前提** | 评审称「BMP 分支不可达 / with_extension 零调用」——前提不成立：BMP 分支是 `design.md` §5.8 公开 API 文档化契约且被 `test_post_write_formats` 钉住（C1）；`with_extension` 是 PP-FROZEN 声明 + test_fsops 4 例覆盖（C2）。删除 = 改冻结契约 + 减证据面，**保留**；同条的 effective_datetime 收敛已修（`12ce994`） | **误识别关闭** |
| R2F-X2 | 误识别关闭 | **R-011 证伪** | 评审称 `tiff:predictor` 无人消费——WP2 复核 OIIO 3.1.17 `tiffoutput.cpp:1026-1029` 证明 `tiff:Predictor`（大小写不敏感）真实消费且在自动规则之后跑 ⇒ 用户值生效。评审结论被证据推翻；修法改「键名对齐 + float 档显式拒绝」（W2-A12，`33d09a8`） | **误识别关闭** |
| R2F-X3 | 误识别关闭 | **WP5-A10 任务书笔误** | 任务书「CMakeLists 登记 secret.cpp 一行」字面与工程现实矛盾：两种单行做法均不可达（仅进 pp_core → LNK2005；header-inline + 锚点 TU → 与 M2-T13 裁定相抵）。实际可达做法 = 两行对偶登记（B33），「一行」按**任务书笔误**关闭，不改实现 | **误识别关闭** |
| R2F-C1 | 受约束口径闭环 | **B3 BMP ICC 警示口径** | OIIO BMP writer 不承载 ICC（库事实，test_enc_oiio 钉住）；产品口径取「警示」：`EmbedIccUnsupported` 逐输出报、keep 不虚报（R2A）。表内 tooltip 无可标注面（FormatDef 无 tooltip 字段）——不扩冻结面，事实面由警示 + 测试承载 | **受约束口径闭环** |
| R2F-C2 | 受约束口径闭环 | **非 Windows 密钥** | DPAPI 仅 Windows：非 Windows = `ppsec2:` 编码 + chmod 0600（B30/A12），口径如实「编码不是加密」；真加密需 keyring/libsecret（负面清单禁扩依赖）。Windows 侧 DPAPI 失败兜底退编码档（不落明文、不丢密钥，A13） | **受约束口径闭环** |
| R2F-C3 | 受约束口径闭环 | **Linux golden.log 移植** | Windows 宿主不做基线文本替换移植（R-015 反面先例）；M7/R2 两轮行为行（TU 标签 180 + heif profile 46 + A1 nclx 132 + 版本行 12）均**未移植** Linux 基线 → 归 Linux 宿主 / nightly `update_baseline` 通道重播种后确认 | **受约束口径闭环** |
| R2F-C4 | 受约束口径闭环 | **R-325 登记（强类型路线）** | 13 项契约核对已收口（校验器/单点/断言承载，R2B 核对表）；强类型包装（metadata_only↔outputs.size()==1 非空容器、bps/bpp 拆字段）会改 PP-FROZEN 签名 → 维持 C8/M7R2B-5「登记待解冻」，如走包装类型需一次性解冻裁定表 | **受约束口径闭环** |

## 十、发行裁定（R2REL，2026-10-12）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tag-move-2 | **tag v0.3.3 二次移动授权在案、本轮未执行**：授权依据 = 用户明确指令「修复后发布 0.3.3」（任务书明文允许 git push、授权二次移动，条件同首次：预检零 Release 零资产 / 原子操作 + `ls-remote` 核对 / 移动后不得再动）。预检全过（`/releases/tags/v0.3.3` 404；release #13 publish skipped 零 Release 资产；`ls-remote` 核对 v0.3.3 → `cb181ee`/`^{}`=`b1af371`，证据 `docs/m7-evidence/m7r2-release.md` §1）；**但流程 3 的前提「main-full 绿」未满足**：push `2c50bfc` 后 main-full #44（run 38255712342）红——linux（全 ctest+金样）step 13 `ctest --preset release` **8 条失败**（退出码 8 = 失败测试数，本机 `ctest -R no_such_test` rc=0 反证「无测试」假说；构建成功）；二分对照 main-full **#41 @ b1af371 同 runner linux success** ⇒ **M7-R2 波次引入的 Linux 侧回归**（非 R2F-C3 登记的基线移植债）。按 ask 流程 3 分支「main-full 红 ⇒ 修复推送（允许）或停止上报」：修复须改 `src/**`/`tests/**`，**独占文件面外**（本包任务书「不得改 src/ 与 tools/」+ 纪律 3）⇒ **停止上报**，tag 保持 b1af371（零 Release 零资产），release.yml 未触发。授权未消耗：待 8 条 Linux 失败修复、main-full 绿后，可由后续发行包按同一条件执行移动 | ① 判据不可绕过（纪律 9）：main-full 绿是 tag 移动的明文前提，红灯期推 tag = 赌 release.yml 且违背门禁文化；② 修复跨界不可「顺手改」（纪律 3）；③ 停在零资产状态无新增不可逆性（tag 仍可按本条授权移动，`b1af371..2c50bfc` 全在历史） | R2REL |

## 十一、R2.1 轮裁定登记（M7-R2.1，2026-10-13；F4 跨面包收口）

> 任务书出处 = 会话内分派书（F4 包）；编号命名空间新增 `M7R21F4-*`（R2.1）。

### 11.1 M7R21F4-0：NOLINT 处置翻案（主对话裁定）+ tidy 棘轮重播种

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R21F4-0 | **处置翻案（主对话裁定）** | 撤销 R2G-G2 的 **11 处行级 NOLINT 消音**（kernels.cpp 2 块 / file_collect.cpp 1 块 / smoke_probes.cpp 1 块 / mainwindow_run.cpp 1 行），恢复「仓库零抑制」。替代处置：**零行为修复 14 处**——① 同型形参对 → 参数组结构化/命名包装 9 处：`dithered_range` → `DitherGrid`（kernels，2 告警）、`collect_from` 出参对 → `ScanTally`（file_collect，1）、`expect_card_menu` 出参对 → `Tally`（smoke_probes，1）、`probe_progress` 行对 → `EventRow`（smoke_probes 基线内，1）、make_result lambda 三 double → `GeoRow`（smoke_probes 基线内，1）、exif_editor 三枚举基型 int→uint8_t（F3 R-197/199 新增告警，3）；② Win32 int↔ptr 惯用式 → 显式安全转换封装 5 处：frameless `hwnd_of`/`nccalc_params_of`（std::bit_cast 位拷贝与 reinterpret_cast 逐位等价）。**无零行为修法 7 处**（AVX2 内核 portability-simd-intrinsics ×6 = AGENTS §1.4 AVX2 硬线下的结构性误报，换修法=换内核；QtConcurrent 任务 lambda bugprone-exception-escape ×1 = 修法是 try/catch 吞异常 = 行为变化）→ 按 M7-ADJ-tidy-reseed 口径 `ci-tidy --update` 重播种 + 逐条归因（下表） | WP12 期 M7-ADJ-tidy-reseed 明文否决 NOLINT（「零抑制」口径），R2G 在「棘轮只减不增、禁 --update」约束下采取消音并自注「翻案替代方案」；本轮主对话裁定执行该翻案（M7R21F4-0），取替代方案 a（结构化）为主 + b（授权重播种）兜底 |
| M7R21F4-0-1 | **tidy 基线重播种（逐文件归因）** | `tools/baseline/clang-tidy.tsv` 264/71 → **264/76**（`TIDY-UPDATE total=264`；总量零增长 = 「总量只减不增」满足）。逐文件放宽登记：kernels.cpp 31→**37**（+6，avx2 内核 portability 告警随消音撤销显形，无零行为修法）、mainwindow_run.cpp 1→**2**（+1，第二个 QtConcurrent 任务 lambda exception-escape，与基线内 :531 同形同因）、page_run.cpp 5→**6**（+1，R2 波后新增、非本包文件面，登记待属主零行为修复后回收）、meta_state.cpp 0→**7** / meta_format.cpp 0→**4** / meta_card_privacy.cpp 0→**3** / filegroup_proxy.cpp 0→**2** / map_view.cpp 0→**1** / output_page_build.cpp 0→**1** / output_template_ui.cpp 0→**1**（19 条 = F2/F3 拆 TU **机械迁移**：page_meta 16→1、page_output 7→5、filelistmodel 2→0、mapwidget 5→4 的告警随代码搬家，file-keyed 棘轮「新文件须 0」不可兼容，同 M7-ADJ-tidy-reseed 触发背景）。逐文件收紧：frameless.cpp 6→**1**（−5，int↔ptr 封装；余 1 = `attach(QWidget*,QWidget*)` 同型参数对，公开壳层 API 签名重构不做，沿 WP12「发行期不做签名重构」口径维持基线内登记）、smoke_probes.cpp 2→**0**、page_meta.cpp 16→**1**（−15）、page_output.cpp 7→**5**、filelistmodel.cpp 2→**0**、mapwidget.cpp 5→**4**——**收紧侧合计 −27**。**R2.2 校读对账（算术闭合，`git show` 两版基线实测）**：放宽 +27 / 收紧 −27 = 净 0 ⇒ 264→264 自洽；早稿把 exif_editor「5→2（−3，回基线值）」误列入收紧侧——实测 `751f479` 与 `7b89cc3` 两版基线 exif_editor 均为 **2、零变化**，该条是修码后**活动告警**回落到基线既有值（「回基线值」本义），非基线账面变动；列之则 +27/−30 = −3 不闭合，故移出本表账面口径 | M7-ADJ-tidy-reseed 五条件逐条满足：总量只减（本轮 264→264 零增长；R-081 连带补记后再播种 264→263，终读数 263/75，见 M7R21F4-10）/ 放宽逐条理由 + 本表登记 / 归因随提交正文 / 复跑 exit 0 + 读数入报告（handoff ⑤）/ 不动 CI 判据与检查集、不加抑制（NOLINT 指令零残留，`grep -rE "NOLINT(BEGIN|END|NEXTLINE)?\(" src/ tools/ tests/` = 0） |

### 11.2 M7-R2.1-F4 其余裁定（跨界收口批 / 色彩四选项 / 探针批，2026-10-13）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R21F4-1 | 冻结面删除（追认） | `page_output.h` 删除孤儿信号 `format_changed`（声明 + 3 处发射全链退役；config_changed 承载通知） | 任务明文「R-133 接线或删除」；全仓零 connect；R2D1-A3（collapsed_keys 零消费删除）先例；消费面行为逐字不变 |
| M7R21F4-2 | 加性（多面） | R-319 --base GUI 入口：page_output `mirror_base()/set_mirror_base()/mirror_base_changed` + FileListModel `set_mirror_base/ mirror_base` + 全局卡 pp-base-edit 行；derive_base_dir 单点（含则基准、不含回退 src.parent，同 dev_cli choose_base 单基准语义） | R2D2 ③-3 草图「控件落全局卡 + 收集/重算两触点」；opt-in 空=现行为；不做半接线死控件 |
| M7R21F4-3 | 词表单源落点调整 | R-137 map_style 词表与序落 `core/settings.h kMapStyleIds`（settings.cpp 校验 / layers.cpp name↔enum / mainwindow 档位表三处副本全走单源，MapStyleRow 表退役） | 评审措辞「以 layers.h 为单源」在 settings.cpp（core）侧受分层约束（core 不引 mapwidget）→ 调整为「词表在 core、映射在 layers」；词表/序逐字不变 |
| M7R21F4-4 | 加性 + 行为接线 | 色彩四选项 UI 面：page_output 全局卡 pp-intent/pp-gamut/pp-dither8/pp-linear 四控件（opt-in 默认 0/false = 现行为）；config_base 读入 RunConfig 四字段；PreviewCache/PreviewPanel `linear_resample` 加性透传（切换清缓存）；mainwindow config_changed 同步预览通道 | UX-14 C1–C4 的 UI 面（R2A 引擎面已交付）；默认值 = 现状字节级一致（金样/回归背书）；能力口径中文 tooltip 明示 |
| M7R21F4-5 | 取舍注记 | UX-11 page_meta 侧 lock_card_size：info/time/switch 三卡接线；两处判据取舍——① 锁在 refresh_all **之后**（建树瞬时 sizeHint 破坏 meta-geom 行距 28 不变量，实测）；② **GPS 卡不锁**（UX-4 stretch 富余高载体，meta-map-expand 恢复基线与 map 高探针钉住，锁高与之互斥） | 探针判据优先；如实登记半接线（三卡防压缩达成、GPS 卡维持动态高） |
| M7R21F4-6 | 行为修复（探针抓出） | R-153 尾单：restore_selection_by_paths 末尾 `view->setCurrentIndex(first)` 默认选择命令把整组恢复塌缩成一行 → 改 `selectionModel::setCurrentIndex(NoUpdate)` + 显式 scrollTo；R-183 前半：rebuild_rows 旧代 teardown 同步断信号面（deleteLater 时序解耦第一步）+ 探针不跨 pump 缓存部件指针纪律 | R-152/153 回归探针抓到 R2D1 修复未覆盖的塌缩路径（评审场景原文「多选后敲过滤字符」实测 1/2）；修复后 edit-target 3/3；行为面 = 评审意图兑现 |
| M7R21F4-7 | 加性（冻结面） | `core/metadata.h` 末尾 `camera_model()`（R-204）；`core/fsops.h` 末尾 `conflict_policy_name/parse_conflict_policy`（R-239）；`core/thumbs.h` PreviewCache `set_linear_resample/linear_resample`（C4）；preview_panel `set_linear_resample`（C4）；page_output `linear_resample()`（C4） | 各面末尾追加式加性，既有签名/行为零改动 |
| M7R21F4-8 | 冻结面加性文本 | `--dev` usage 增 R-145 醒目标注（dev/GUI 默认面分歧：conflict=overwrite vs rename、单输出模板 $dir/$file vs $format/$dir/$file）；README 中文面同步 + R-065「默认物理核数」措辞改实 | 评审修法二选一取「--help 醒目标注」（R2D2 ③-2 指定路线）；R-237/283 曾同口径改 usage 文本 |
| M7R21F4-9 | 行为边界披露 | R-195 三调用点转发 `pp::lookup_param_def`（first-wins 单源）：preset_io 本地实现、dev_cli set_param last-wins 循环退役；**边界语义差异**：tech 指名但该技术不声明该键时，由「报错/保留字符串」变为「回退后端全技术并集」（单源既定语义） | R-195 收敛口径 = 「全部调用面经本函数」；词表/决胜规则统一为 first-wins |
| M7R21F4-10 | tidy 基线补记 | R-081 异步化新增 QtConcurrent 任务 lambda bugprone-exception-escape ×1（exif_editor.cpp :1925）→ 重播种 exif_editor 2→3（修法 = 吞异常 = 行为变化，同 WP12-B2/M7R21F4-0 口径不采）；**总量 263 ≤ 264 不增**。**R2.2 校读对账（算术闭合，`git show 81d0b34` 基线 diff 实测）**：同次重播种账面另含两笔回落——dev_cli 1→0（−1）、settings_dialog 4→3（−1）（`93001a9` 收口期修码消除的既有告警随基线随动；逐条修法归因超出基线文件可见面，按基线 diff 登记）；净额 +1−2 = −1 ⇒ 264→263 自洽（早稿只列 +1 未列对冲项，故显不闭合） | 十一节 M7R21F4-0-1 同一口径的连带登记（本条为该包验证期发现，非 R2G 消音翻案面） |

## 十二、R2.1 轮裁定登记（M7-R2.1-F1/F2/F3 包，2026-10-12～13）

> 任务书出处 = 会话内分派书（F1/F2/F3 包）；编号命名空间 = `M7R21F1-*`/`M7R21F2-*`/`M7R21F3-*`。
> 验证读数摘录见 `docs/m7-evidence/m7r21-f{1,2,3}-gates.md`。F4 各条见十一节。

### 12.1 F1（Linux 回归定位与修复；提交 `3339492`/`b673ee4`/`d4099bd`/`0555947`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R21F1-1 | 判据可见性（**永久保留**） | linux ctest 失败注解遥测 v1：失败测试名 + 失败断言行 → `::error` 注解（`.github/workflows/main-full.yml` 仅注解遥测一处） | 匿名日志 API 403、check-runs 注解接口公开可读；失败明细必须免鉴权可见才能闭环 Linux-only 回归。非临时探针，是判据可见性改进 |
| M7R21F1-1b | 遥测二版口径 | 失败集合只收真失败（摘要块 + LastTestsFailed.log + FAIL 断言行所在段）；注解总数封顶 10 且首条 = `CTEST-FAIL-SUMMARY` 名单汇总；**全量明细写 `$GITHUB_STEP_SUMMARY`** | 一版实证（run #46）：LastTest.log 全段登记泛滥 35 条 + GitHub 每步仅存前 10 条注解 ⇒ 真失败名被截断；summary 通道免鉴权、无上限 |
| M7R21F1-2 | 行为（鲁棒性）| `fsops::commit_temp_file` rename **有界重试**：仅 Windows 共享违规类（GetLastError 5/32/33）、≤3 次、退避 25/50/75ms；非瞬态错误与 POSIX 一次失败即出；失败消息与返回值口径逐字不变（fsops.h 零改动 = 无冻结面扩展） | **D-1 根治**（R2G-G5 的「待裁定」收口）：瞬态文件锁是 fsops write_atomic/commit 真实鲁棒性缺口（m7-report §6.4-1 原登记「不修」，F1 任务书令修）；有限重试不改成功/失败语义 |
| M7R21F1-3 | 断言样例换拼写（判据不变） | test_fsops R-007 长路径断言样例按平台拼写（`#ifdef _WIN32`：WIN32 根 = `C:\out` / POSIX 根 = `out`，分隔符同理），两侧断言逐条相同、严格度不变 | POSIX 的 fs::path 不切分 `\`，`C:\out\…` 整串是**一个**含 `:` 的组件、被 validate_output_filename 非法字符防线误拒 ⇒ Linux 必红；被测规则跨平台逐字一致、无 #ifdef，样例必须同构才测到同一规则。**断言不放宽**（只换样例拼写） |

### 12.2 F2（R2E1 重做：page_meta + mapwidget RTL 与结构拆分；12 提交 `b83f185`…`d6360f6`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R21F2-1 | 冻结面零改动 | `page_meta.h` **逐字节未动**；B.1 拆分全部走新增内部头（`src/ui/meta_*.{h,cpp}`） | B.1 硬约束「page_meta.h 逐字节冻结，拆分只动 .cpp」；新能力入新文件（同 F2-A1 handoff 口径） |
| M7R21F2-2 | 结构载体选择 | MetaState 拆分载体 = 六具名聚合 + 组合成员（core/follow/info/time/gps/privacy + overlay），非蓝图字面的「B1 单文件重组」 | 聚合按域成文（各自契约注释、各卡 TU 归属明确）；925 处访问点编译器全量核对 |
| M7R21F2-3 | 加性 + 显式化 | MetaState 加 Q_OBJECT（moc 面新增）；`state_of` 动态属性锚改 `qobject_cast` + `Q_ASSERT` | R-177 后半「动态属性 static_cast 藏状态」显式化；属性未挂 = nullptr 与既有空态一致 |
| M7R21F2-4 | 行为零变化锚定 | RuleInputs 值聚合 + 兼容薄重载（`build_rules`/`current_time_shift(const MetaState*)` 保留，调用点零改动） | B3 定稿「入参改值聚合」；纯函数面可单测（R-181 规则半边收口） |
| M7R21F2-5 | 隐式约定显式化 | tag 行改 `MetaState::tag_edits` 显式键值对清单（add_tag_row 登记/移除钮同序移除/clear 清空），`findChild("tag_key")` 消灭；objectName 保留给探针 | R-180；行为逐位不变（顺序 = 布局顺序） |
| M7R21F2-6 | 词表单源 | `kFieldProperty` 取值收敛 `fieldprop::` 常量表（20 档）；QSS 选择器锚值逐字不变 | R-179「stringly 20+ 种值」收口；本面 31 处 setProperty 字面量换具名 |
| M7R21F2-7 | 坐标基准红线注记 | mapwidget 自绘 HUD 镜像**只作用于界面构件**（比例尺/版权/气泡），瓦片/图钉/主钉等地理投影内容不镜像 | 公共坐标 WGS-84 语义不随语言翻转；R-218 的「镜像」限界面几何 |
| M7R21F2-8 | 词表覆盖 | `meta_probe_context.h` 的 `objname::` 词表含 8 个 info 值槽名（原 raw `const char*` 数组） | 词表单源必须覆盖全部在用 objectName（含非 QStringLiteral 用法） |
| M7R21F2-9 | 探针纪律 | 临时 RTL 探针（`build/m7r21-f2/rtl_probe.cpp`）不入库；常驻化规格入 F4 探针需求 | 纪律 5「临时探针放 build/」；常驻探针由 F4 统一落地 |

### 12.3 F3（R2E2 结构余量；10 提交 `6250b3b`…`0ca8c62`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R21F3-1 | 新文件 | `byte_text.h`/`time_tag_keys.h`/`format_selection_model.h`/`filelistmodel_util.h`/`page_output_impl.h` + `output_page_build/output_template_ui/filedelegate/filegroup_proxy/filegroup_delegate/file_collect/group_scan.cpp` | 任务条目明文命名或拆分必然产物；header-only（R-109 路线）或逐字搬移；CMake glob 自动收编，CMakeLists 零改动 |
| M7R21F3-2 | 加性（PP-FROZEN `page_run.h`） | `~PageRun() override` 声明（R-185 注册表收敛为 unique_ptr RAII 所需） | 加性 + 追认；既有签名零改动 |
| M7R21F3-3 | 加性（PP-FROZEN `settings_dialog.h`） | private 五个 `build_*_page` 声明 + `theme_mode_seg_` 成员 + formkit 前置声明 | 加性 + 追认；公开 API（ctor + settings()）逐字不变 |
| M7R21F3-4 | 注释级（PP-FROZEN `filelistmodel.h`） | 契约块补「状态写入权」条目（Queued=add_paths；运行/终态只经 set_state；apply_thumb 探测失败补判为唯一例外） | R-205 收口成文；代码零改动 |
| M7R21F3-5 | 行为零变化锚定 | FormatSelection 的 R-266 游离槽语义逐字保留（空集保留被移除 id 作回退槽、不判现役） | test_smoke 第 9 节真值表钉住 |
| M7R21F3-6 | 同值幂等重放 | reproject_format_selection 含 refresh_tab_labels/sync_tiles 等原路径未调的幂等重放 | 值不变、无信号（QSignalBlocker）；4 sync 坍缩为 reproject 的必要组成 |
| M7R21F3-7 | 行为零变化锚定 | R-192 per-format 后端覆盖：set_batch_has_alpha 逐格式清 = 原全局清；换格式不再误判 | 修假阳假阴是 R-192 明文（缺陷修复方向） |
| M7R21F3-8 | 行为零变化锚定 | R-254 选中映射判据统一（越界行过滤提前；remove_rows 内部本就过滤） | 稳定态逐值相同 |
| M7R21F3-9 | 机械搬移 | R-255 跨节共享定义（pick_lat/pick_lon/GPS 胶囊 lambdas）从 (d)/(e) 间挪到前置 | 逐字搬移；ctx 分发；12 个 meta-* 探针输出行不变 |
| M7R21F3-10 | **行为变化（明文授权）** | R-259：运行中关闭的 join 期增加反馈（状态行「正在等待在途任务收尾…」+ 等待光标 + 一次同步重绘），阻塞期从析构挪到关闭路径 | 任务条目明文（R-259 =「析构期 join 无反馈」）；wait 幂等、~MainWindow 收口序不变 |

## 十三、发行裁定（M7-R2.1-F7，2026-10-13）

> 任务书出处 = 会话内分派书（F7 包「发布 0.3.3」）；实况证据真源 =
> [docs/m7-evidence/m7r21-release.md](m7-evidence/m7r21-release.md)（预检/push/main-full #48/
> 本机复核/停止决策逐步可复跑）。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tag-move-2（F7 消耗尝试） | **二次移动仍未执行，授权保持未消耗**：F7 按流程 1–4 走完预检（`/releases/tags/v0.3.3` 404、releases 列表无 v0.3.3、`ls-remote` 核对 v0.3.3 → `cb181ee`/`^{}`=`b1af371`）、push main（`0555947..e1017fe`，HEAD `e1017fe6`）、本机复核（ci-format `violations=0` 187 文件 + `make_winzip --smoke-exe` 五门禁 rc=0 含 E-2 冻结行逐字）；**但流程 3 前提「linux job 必须绿」未满足**：main-full **#48（run 38308098290）completed/failure**——唯一失败 = linux（全 ctest+金样）**step 15 release-dev ctest 的 `ui_smoke` 一条**（F1 遥测注解逐字 `CTEST-FAIL-SUMMARY 1 tests: ui_smoke`；step 13 release ctest 全过）。二分归因：#47 @ `0555947` 同 job **success** ⇒ 失败由 `0555947..e1017fe`（R2.1 F2–F4）引入（`tests/ui_smoke.py` 与 `.github/` 零改动，变化全在 `src/ui/` 探针/几何面）。修复须改 `src/ui/`/`tests/` = 独占文件面外（任务书「不改 src/ 与 tools/」+ 纪律 3）⇒ 按纪律 9 **停止上报**：tag 保持 `b1af371`（收口 `ls-remote` 复核未动）、release.yml 未触发（最新仍 #13 cancelled）、GitHub Release 未创建（收口复检 404）。**授权未消耗**：待 ui_smoke 修复、main-full 全绿后由后续发行包按同一条件执行（预检零 Release 零资产 / 原子 + `ls-remote` 核对 / 此后不得再动） | ① 判据不可绕过（纪律 9）：「linux job 必须绿」是 tag 移动的明文前提，红灯期移动 = 把一次性授权烧在坏树上（此后不得再动 = 无第三次机会）；② 修复跨界不可「顺手改」（纪律 3）；③ 停在零资产状态无新增不可逆性（tag 仍可按 M7-ADJ-tag-move-2 条件移动，`b1af371..e1017fe` 全在历史）；④ 未采用空提交复跑辨瞬态——取证更支持确定性回归（#47 绿/#48 红与 src/ui 大改同步）且空提交污染历史，留主对话裁决 | R2.1-F7 |

## 十四、R2.2 轮裁定登记（M7-R2.2，2026-10-13；G1/G2/G3 三包 + G4 文档终稿 + G5 终门禁）

> 任务书出处 = 会话内分派书（G1/G2/G3/G5 包）；原始登记 = `build/m7r22-g{1,2,3,5}/handoff.md`
> 「裁定登记」节（逐字汇入，本表为唯一对账面）；验证读数摘录见
> [docs/m7-evidence/m7r22-g*-gates.md](m7-evidence/) 与 `m7r22-gate.md`。G4（文档终稿）零行为
> 改动、零冻结面扩展，无新裁定。**R2.2 终态四档计数不变**（326/15/3/1，主标签零改判；R2.2 消
> 的是「已修」行内的余量注记，见 m7-report 附录 A 表头注）。

### 14.1 G1（探针总集与 Linux ui_smoke 定点修复；5 提交 `bbe5101`/`fd5b838`/`07cc8c5`/`297d22b`/`4addc51`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R22G1-1 | 判据可见性（**永久保留**） | 失败遥测三版：提取式补 4 种失败行形态（单测正序 `FAIL <case>: <detail>` / C++ 断言 `UI-SMOKE FAIL <reason>` / 计数 `UI-SMOKE FAILED <n>` / 反序驱动 `ui_smoke: FAIL (…)`）+ ui_smoke 段**尾探针行**锚（崩溃/超时定位）；覆盖 release + release-dev 两棵树（stdout/LastTestsFailed/LastTest）+ ui-smoke-raw.log，遥测步后置到全部测试步之后（`if: failure()`）；`CTEST-FAIL-SUMMARY` 首条注解带探针名/断言行（断言+驱动+尾探针三锚），Step Summary 全量明细照旧 | main-full #48 实证「反序不匹配 ⇒ 无 FAIL 断言行兜底不表征失败形态」（[m7-evidence/m7r21-release.md](m7-evidence/m7r21-release.md) §3.3）；判据可见性 = M7R21F1-1 永久保留的延续 |
| M7R22G1-2 | **判据加固（AA 容差机制）** | settings-tabs-light 像素档 R-044 容差落地：判据①原众数 ±6 保留（充分条件）+ ②字形内核（非背景像素最深 15% 逐通道中位数）沿 bg→want 覆盖度 t≥0.5 + 逐通道残差 ≤12 + ③内核须更接近 want 而非互斥对照令牌（text↔text2）；自检行加 mode/est/t/resid 取证 | R-044「字体敏感断言给容差」的落地；Linux 灰阶 AA 实测（run 38314701500 注解：选中 #1e2028/#1b1c20、未选中 #222638/#5d5f68）为同色 AA 漂移非语义差异；断言只增不减、语义仍钉 |
| M7R22G1-3 | 探针扩充（常驻） | rtl-mirror 扩充四查常驻化（stars/hud/logical-align/geom-mirror；±2px 镜像容差；HUD 不钉绝对宽只钉关系式+左缘锚） | F2⑤-1 规格；F2 实测注记「盒宽受缺失字体回退放大」⇒ 绝对宽不可钉 |
| M7R22G1-4 | 探针定论 | rtl-deep：QLabel/ElidedLabel（继承 QLabel paintEvent）内部 QStyle::visualAlignment 自动随向镜像，无需显式 visualAlignment；±2px 容差 | e2⑤-6 的「未实测」问题定论；关系断言抗字体回退 |
| M7R22G1-5 | 探针（判据取舍） | format-roundtrip 按 e2⑤-5 规格判「四面一致 + toggle-on 现役=新开项（R-189）」；关现役落点**取证不判据** | 见 M7R22G1-6 的分歧发现 |
| M7R22G1-6 | **跨界发现（不判据，交 page_output 属主）** | 关现役后的现役落点 UI 实际 = 页签滑动邻接位（remove_tab → currentChanged 末写者覆盖），与 `format_selection_model.h` 的 R-266 契约「现役前移首位」分歧。751f479 旧序 = remove_tab **后**显式 front 赋值（末写者=front）；R-189（F3）重排 toggle_format 顺序（sel.toggle 先行）后信号胜 ⇒ 疑似 R-189 引入的语义漂移。读数：off-active=webp front=jpeg（实测）。建议修法 = remove_tab 后重申 front 或信号屏蔽。**R2.3 追记：已由 M7R23G8-1 归位**（`4c9d7db` 采「remove_tab 后重申 front」修法，见十六节；读数归位 off-active=jpeg front=jpeg） | 模型真值表（tests/unit/test_smoke.cpp）钉 front-move；UI 组合层分歧不在本包文件面 |
| M7R22G1-7 | 探针 | color-options 档位断言 = 4/3 档 + 默认值=现行为 + config_base 往返（M7R21F4-4 口径） | F4⑤-7 规格 |
| M7R22G1-8 | 探针补强（断言只增） | context-menu/check-all 补强 = 分隔线全序 + 收菜单无残留 + 快捷键动作执行语义 | F4⑥-1 的「已落地」项做深度补强 |
| M7R22G1-9 | 探针加深 | R-152/153 回归深度 = 全可见行映射 + 二轮重建保留 | e2⑤-2 规格的机械化加深 |
| M7R22G1-10 | 探针（真实编排） | 0 字节补齐双跑按 R-006/M7-R2D1 语义真实编排（pp-run-again 用户路径；损坏样本自建自清） | F4⑤-1/D1⑤-3 规格；不改任何既有断言 |
| M7R22G1-11 | 探针 | R-144 失败清单并入双跑（真实失败样本 → `路径\t原因` 行式落盘 + 状态栏 tooltip 出处/手型 + 点击下钻断言） | F4⑤-2 规格；行式契约 = `mainwindow_impl.h` 失败清单行格式 |

### 14.2 G2（结构余量与语义裁定落地；15 提交 `12234f1`/`0208651`/`8c90c54`/`9f01cb1`/`6d5d07e`/`fa60b38`/`77e3c90`/`d677c52`/`05181ac`/`847a83d`/`4c81bd0`/`0552023`/`ec1e3ba`/`7541683`/`4dee5c8`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R22G2-0 | **行为变化（主对话裁定）** | R-197 时间「清除」= **连带 touched 全部 9 键**（OffsetTime* 3 + Xmp MetadataDate 一并 remove），与〔批〕命中面 / shift 触达面同集，消「清除 3 键 vs 命中 9 键」漂移；原「清除只动 wall_clock 5 键」口径**换锚** | 主对话裁定 M7R22G2-0 明文；语义安全核 = `apply_edits` 对缺席键 remove 为 no-op（`core/metadata.cpp:1299-1301` 实读），无新错误面 |
| M7R22G2-0-1 | 跨面触碰（`time_tag_keys.h`） | 契约块改写 + 加性谓词 `is_time_clear_key`（= touched 全集） | R-197 语义的单源面；该文件即 R-197 载体（F3 产出），换锚的必然产物 |
| M7R22G2-0-2 | 跨面触碰（`test_smoke.cpp` 第 8 节） | 断言换锚 3 条 + 新增 2 条（断言只增不减、严格度不降）：「清除不含 OffsetTime*/MetadataDate」「清除谓词只认墙钟 5 键」→「清除含 OffsetTime*/MetadataDate」「清除谓词 = touched 全集」+ 9 键全清覆盖 / 非时间键不入清集 | 行为裁定的测试锚必须随动（原 3 条钉旧语义 = 会撒谎）；任务条目明文「测试断言换锚登记」 |
| M7R22G2-1 | 跨面触碰（`formkit.cpp`，成对自绘） | Combo 箭头位 / paint_spin_arrows 箭头列按 `widget->layoutDirection()` 贴 trailing（LTR 逐位不变） | 只翻 QSS 不翻自绘会让 RTL 值区与箭头错位；theme.h 非对称几何改 logical 的必要对应面 |
| M7R22G2-2 | 跨面触碰（`meta_format.cpp`） | `PP_META_FORMAT_TEST_NO_WIDGETS` 编译开关 + `tr_page` 改名（查表键逐字相同） | test_icons `PP_ICONS_TEST_NO_WIDGETS` 同款先例；test_meta_format 15 纯函数单测的唯一可行接线（单测 target 只链 pp_core） |
| M7R22G2-3 | 跨面触碰（`meta_card_info.cpp`） | 3 条 static_assert 对拍 + 循环界 `std::size`（值不变） | F2 残余 ⑥-3 本体：对拍必须在表侧才咬合；测试侧复述双端钉 |
| M7R22G2-4 | 跨面触碰（`test_gcj02.cpp`） | 新断言 29 条（tile_cache LRU/预算 8 + 交互状态机 21） | 任务验证明文「test_gcj02 新断言」；B.5②⑥ 纯核的判据面 |
| M7R22G2-5 | 加性（PP-FROZEN `settings_dialog.h`） | private 对象图成员 15 件 + `new_cell/…`（公开 API 逐字不变） | 同 M7R21F3-3 先例（加性追认）；R-201 后半 findChild 字符串退役的载体 |
| M7R22G2-6 | 行为零变化锚定 | R-256 行序不变稳态逐事件同值；R-201 null 守卫同义；R-183 稳态值表逐值同旧（ui_smoke classify 六探针逐值）；B.5 步②–⑥ 全五步绘制/交互/网络逐位 | 各条验证段（G2 handoff ④） |
| M7R22G2-7 | 查表键不变声明 | R-254(1/2) T() 统一：成员函数 tr() 与 T() 同上下文逐字键不变；辅助类裸 tr() 归一上下文（仓库零 .ts/.qm，运行期无差） | 若未来接入 .ts，辅助类键需按 "MainWindow" 上下文重新导出（登记） |

### 14.3 G3（判据小项与文档补录；3 提交 `3c35c00`/`39942bf`/`55b13a1`）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7R22G3-1 | **SCHEMA `state` 值表加性补 `done_with_errors`（冻结面加性 + 追认）**：`tests/golden/SCHEMA.md` progress_trace 事件字段表 `state` 行**逐字不动**，其后**新增一行**补 `done_with_errors`（`FileState::DoneWithErrors` 的小写名；多输出部分成功的源文件级终态） | ① 事实缺口：`FileState::DoneWithErrors` 自 M7-WP1/I7 起为 `src/core/pipeline.h` 枚举**末尾加性值**，`src/dev_cli.cpp` `state_text`（M7-R2.1-F4）已按「`FileState` 的小写名」词表口径落 `"done_with_errors"`，而 SCHEMA 值表 13 值未列本值（F4 ③-3 登记余量、F5 ③-1 转交 G3 收口）；② 走「加性 + 追认」：新值以**新表行**追加，v1 既有值表行逐字不动、词表口径不变、断言零放松、金样/夹具零改动（SCHEMA.md 非语料非夹具） | G3（`tests/golden/SCHEMA.md`） |

### 14.4 G5（终门禁复核；git 主题 `M7-R2.2-GATE:`，2 提交）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7R22G5-1 | **tidy 棘轮基线收口重播种（授权 M7R22G2-0 衍生；M7-ADJ-tidy-reseed 口径）**：`tools/baseline/clang-tidy.tsv` 263/75 → **262/76**（`TIDY-UPDATE total=262`）。逐文件放宽登记（唯一）：`tests/unit/test_meta_format.cpp` 0→**1**（`bugprone-suspicious-include`——G2 M7R22G2-2 单实现接线 `#include "ui/meta_format.cpp"` 的结构性告警，与基线内 `tests/unit/test_icons.cpp=1` **同型同因**；替代修法 = CMake 目标级接线（`target_sources` + `target_compile_definitions`）可两处同清零，发行门禁期不动测试接线，登记交后续）。逐文件收紧：`mapwidget.cpp` 4→**3**（B.5③⑤ 拆分搬出）、`classify_panel.cpp` 3→**2**（R-183 后半）。fallout 修复 5 条（test_meta_format 加宽乘法/枚举基型、geocode_client/tile_fetcher 不必要拷贝、test_gcj02 main 异常逃逸——全零行为）。**F4 登记待回收三组（M7R21F4-0-1/-10）维持登记、逐条归因**：page_run +1（`refresh_overview_progress` swappable-params，签名重构沿 WP12 口径不做）、exif_editor +1（QtConcurrent lambda exception-escape，修法=吞异常=行为变化不采）、F2/F3 拆 TU 机械迁移 19 条（easily-swappable ×8 / narrowing ×4 / no-automatic-move ×3 / empty-catch ×2 / misplaced-widening ×2；empty-catch 与 swappable 无零行为/非签名重构修法） | 任务书明文授权「按 M7-ADJ-tidy-reseed 口径 `ci-tidy --update` 重播种 + 逐条归因（总量只减不增、逐文件放宽登记）」；五条件逐条满足：总量 263→262 只减 / 唯一放宽逐条理由 + 本表登记 / 归因随提交正文 / 复跑 `TIDY total=262 baseline=262 files=76 violations=0 seed=0` exit 0 / 不动 CI 判据与检查集、NOLINT 零新增（grep 0 命中） | G5（`tools/baseline/clang-tidy.tsv` + 4 处零行为修复；明细 [m7r22-gate.md](m7-evidence/m7r22-gate.md) ③-2/③-3） |

## 十五、发行裁定（M7-R2.2-G6，2026-10-13）

> 任务书出处 = 会话内分派书（G6 包「发布 0.3.3」，明文允许 git push、授权消耗
> `M7-ADJ-tag-move-2`）；实况证据真源 =
> [docs/m7-evidence/m7r22-release.md](m7-evidence/m7r22-release.md)（预检/push/main-full #54/
> 本机复核/撞顶归因/停止决策逐步可复跑）。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tag-move-2（G6 消耗尝试） | **三次移动仍未执行，授权保持未消耗**：G6 按流程 1–4 走完预检（`/releases/tags/v0.3.3` 404、releases 列表无 v0.3.3、`ls-remote` 核对 v0.3.3 → `cb181ee`/`^{}`=`b1af371`）、push main（`4addc51..35c4410`，HEAD `35c4410d704e`）、本机复核（ci-format `violations=0` 196 文件 + `make_winzip --smoke-exe` 五门禁 rc=0 含 E-2 冻结行逐字）；**流程 3 明文前提「linux job 全绿（含 release-dev ui_smoke）」达成**（main-full #54 linux 11m40s success，step 15/16 ui_smoke 与冻结行取证全绿，G1 修复实证），**但「main-full 至绿」未达成**：#54 总闸 failure = **winzip job 撞 `timeout-minutes: 20` 硬顶被取消**（step 12 build release 14m20s success → step 13 dev 子集 4m42s 在建撞顶 → step 14 make_winzip skipped；归因 = **代码量**：对照 #48 同 job 19m59s 绿 / build 13m05s→14m20s，`e1017fe..35c4410` G1 探针批 + G2 拆 TU 编译面增长；暖度假绿排除 = 暖度断言 1s 过、vcpkg restore 命中；Windows 无 ccache 重跑同型）。**停止决策**（不执行移动）+ 不可逆性论证：release.yml winzip 与 main-full winzip 两个构建步**逐字同命令**（`cmake --build --preset release` + `cmake --build build/release-dev --target photopipeline`，两 workflow 实读对照）、同 runner 同 20min 硬顶（`release.yml:293`）⇒ 本树预计 **≈21–22min > 20min**，放行移动则 publish 大概率 skipped（release #13 同型先例：winzip 20m16s 撞顶 ⇒ probe/publish skipped 零 Release）；而移动后**不得再动 + 禁止第三次移动** ⇒ 移动 = 把唯一授权烧在发不出的树上，0.3.3 卡死「tag 已动、零 Release、无第三次机会」死角。收口 `ls-remote` 复核 tag 仍 `cb181ee`/`^{}`=`b1af371`，release.yml 未触发 | ① 判据不可绕过/不放宽（纪律 9）：流程 3 的判据是「main-full 至绿」，linux 绿只是明文前提之一，总闸红即未达成；② runbook §6 失败即停触发项逐字命中「任一 job 撞 timeout-minutes: 20 被取消 ⇒ 停止发布链；归因（暖度/预算/代码量）后再处置」，归因 = 代码量，处置权在预算/设计口径；③ 不可逆性不对称：停 = 授权保留、无新增不可逆（`b1af371..35c4410` 全在历史）；移 = ~15% 成功率赌上唯一机会；④ 两班先例（十节 R2REL、十三节 F7）同一裁决口径：停在坏状态之前；⑤ 未采用「放行移动赌 runner 变快」——实测对照（#48 vs #54）+ 同命令同硬顶推算支持确定性超预算，非瞬态 | R2.2-G6（`docs/m7-evidence/m7r22-release.md` §3–§5；跨界预算处置需求见 §7） |

## 十六、R2.3 轮裁定登记（M7-R2.3，2026-10-13；G7 winzip 预算工程解 + G8 尾单清理 + G9 文档收口）

> 任务书出处 = 会话内分派书（G7/G8/G9 包）；原始登记 = `build/m7r23-g{7,8}/handoff.md`
> 「裁定登记」节（逐字汇入，本表为唯一对账面）；验证读数摘录见
> [docs/m7-evidence/m7r23-g7-gates.md](m7-evidence/m7r23-g7-gates.md)、
> [m7r23-g8-gates.md](m7-evidence/m7r23-g8-gates.md)。G9（文档收口）零行为改动、零冻结面
> 扩展；其唯一口径决定 = M7R23G9-1（受约束登记，M7R22G2 ③-2 衍生）。R2.3 消的是「已修」行内
> 余量注记（R-266 分歧 / R-318 两面 / R-260·268 界外注记 / R-201 dialogkit 余量 / R-253 分段
> 余量），**主标签零改判、四档计数不变**（326/15/3/1，见 m7-report 附录 A 表头注）。

### 16.1 G7（winzip 预算工程解；3 提交 `83a9cb5`/`fa871c8`/`fe3b43f`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R23G7-0 | **主对话裁定（判据零改动）** | winzip 预算超出走**产物复用/拆 job**：打包侧消费**已被验证的**构建产物（双 artifact），release-dev 子集段（构建 + ui_smoke + 金样）迁独立 job（main-full `windows-dev` / release.yml `winzip-build-dev`）；**不改 CI 判据、不放宽任何 timeout-minutes（全 20 未动）、不减五门禁与 E-2 `--smoke-exe`**；`tools/make_winzip.py` 零改动（「仅参数/输入面小改」授权未消耗） | 十五节 M7-ADJ-tag-move-2（G6 消耗尝试）残余①「release 链 winzip job 预算超出（≈21–22min > 20min）待裁定」的工程解；per-job ≤20min 由 main-full **#58/#59 双轮全绿**实证（winzip 97s/89s，m7r23-g7-gates.md ②）；「打包对象必须是被验证过的构建产物」经 winzip `needs: [windows, windows-dev]` 双背书后打包（G7-7） |
| G7-1 | 判据逐步保留的范围解释 | 打包侧（winzip / winzip-pack）**逐步保留**「缓存目录预建 + vcpkg binary cache(restore) + vcpkg 缓存暖度断言」（断言文本/判据 ≥20 逐字不动）；vcpkg clone/bootstrap（R-091 SHA 断言）与 binary cache save 随 vcpkg install 留在各构建段 job | 打包段不再调用 vcpkg 二进制（运行时依赖走 artifact）；clone 是 install 的前置而非断言的前置。ccache 链零改动（Windows 无 ccache = R-124 查证事实） |
| G7-2 | ci-budget 键同步（报告性配置，非门禁） | main-full：`windows-dev=7` 追加；release：`winzip-build=15,winzip-build-dev=7,winzip-pack=8` 替换旧 `winzip=15` | job 增改名后旧键失配会整列打「—」；ci-budget 脚本头明文「超预算只是报告事实」，timeout-minutes 全部维持 20 未动 |
| G7-3 | artifact 命名与布局 | 双 artifact：`winzip-build-input`（release 树 + 许可面）/ `winzip-smoke-exe`（dev smoke exe）；单目录 staging（`$RUNNER_TEMP`）上传、下载 `path: .` 还原原布局 | 消除 upload-artifact 多路径 LCA 语义的不确定性；`PP_BUILD_DIR`/`--smoke-exe` 命令逐字不变 |
| G7-4 | 不传 `vcpkg_installed/bin`（281MB） | 只传 share（3.5MB）；运行时 DLL 以 app-local 的 `build/release/*.dll`（40 个，含 jxl_threads.dll）随树传递 | make_winzip 的 vcpkg 只读面 = collect_licenses 的 share 与 jxl_threads 兜底（主源在构建树）；57MB vs 340MB |
| G7-5 | `docs/m7-evidence/release-runbook.md` 增补 | 「job 预算工程解」节（两轮归因 / 复用链路图 / 双 artifact 命名 / #58 每 job 读数）+ §1 事实地图口径修正 | 任务书第 4 项点名；独占文件面之外的唯一改动，由任务书直接指派 |
| G7-6 | ui_smoke/金样的归属 | 随 release-dev 段整体迁入 windows-dev（判据逐步逐字迁移，冻结行断言不动）；winzip 仍独立跑 E-2 | 测试与其所测构建树同 job；E-2 是打包产物级判据，保持在打包段 |
| G7-7 | 「打包对象必须是被验证过的构建产物」的实现 | winzip `needs: [windows, windows-dev]`——windows 全量 ctest 绿（release 树）+ windows-dev ui_smoke/金样绿（smoke exe）双背书后才打包 | needs 成功语义 = 两侧测试先绿；gate 再总闸 |

### 16.2 G8（尾单清理；4 提交 `4c9d7db`/`af7687f`/`9b37f75`/`0b9a4bb`）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R23G8-1 | **行为修正（修 R-189 语义漂移；R-266 契约归位）** | 关现役后现役落点 = R-266「现役前移首位」（`toggle_format` remove_tab 后按契约重申 front：`closing_active && !sel.empty()` → `sel.set_active(sel.items().front())`）；原「页签滑动邻接位」口径废止。format-roundtrip 探针由「取证不判据」升为**断言钉住**（断言只增） | 模型真值表 tests/unit/test_smoke.cpp 钉 front-move（M7R22G1-6 的模型侧口径不变）；UI 组合层向契约归位。回归影响面 = GUI 页签现役落点，`--dev` 语料回归不经 page_output 交互面 |
| M7R23G8-2 | 加性（PP-FROZEN `settings_dialog.h`） | `dialogkit::TitleBar` 末尾追加 `icon` 成员（构造期直赋）；restyle_title_bar 退役 `pp-dlg-title-icon` findChild 反向伸入 | M7R22G2 ③-1 点名的收口修法原文（「可加性扩 TitleBar 结构（icon 指针）收口」）；objectName/公开 API 逐字不变 |
| M7R23G8-3 | 词表加性（`meta_probe_context.h`） | objname:: 补 **13** 键 + 2 辅助常量（kInfoRow0 派生 / kPpMetaPrefix 前缀；文件新增常量 15 条 = 13+2，git diff 实测，词表 111→126；值串逐字与既有 objectName 一致）；smoke_probes 迁移 100 站点 + hook 8 处类型化访问器，meta 族字面量清零 | ProbeContext 全面迁移的必然产物；「加性 + 追认」（只增常量，不改既有值串）。新键数与任务书「11」的差额 = G1 前缀口径漏计 privacy_strip/mtime_sync 两族 4 键，如实报 13 |
| M7R23G8-4 | 断言只增 | format-roundtrip +1 断言（R-266 关现役后现役前移首位 active==front），checked 15→16 | 行为修正须有机械化探针钉住（AGENTS §6.1 单测随码走）；严格度只增不减 |
| M7R23G8-5 | 行为零变化锚定 | R-253 分段逐字搬移（调用序/接线 context 对象不变；MainWindow 冻结头签名零改动）；R-318 tooltip 双态与 page_output 同款；R-268/R-260 注释级；dialogkit objectName 不变；词表迁移值串逐字不变 | G8 handoff ④ 各条验证段（ctest 36/36 + ui_smoke 冻结行 + 金样 20/20 + tidy 262/76，m7r23-g8-gates.md ①） |

### 16.3 G9（文档收口；M7R22G2 ③-2 衍生受约束登记）

| # | 类型 | 内容 | 理由 / 依据 |
|---|---|---|---|
| M7R23G9-1 | **受约束登记（Exiv2 钉版限制；防御分支保留）** | `white_balance_text` 的色温拼接分支（`src/ui/meta_format.cpp:172-175` 消费 `Exif.Photo.ColorTemperature`）经 ExifData **恒不可达**：本 Exiv2（vcpkg 钉版）键表不接受该键（ExifKey 构造即抛 `Invalid tag name or ifdId 'ColorTemperature', ifdId 5`）⇒ `exif_text` 同键恒空串 ⇒ **色温显示从不出现**。产品口径 = 受约束登记：不升级/不换 Exiv2（依赖钉版纪律）、不加兜底假数据，**防御分支保留**（只读消费、零副作用，键可构造时自然生效）；test_meta_format wb 真值表**不立色温夹具**（`tests/unit/test_meta_format.cpp:214-216` 注释如实注明不构造理由） | M7R22G2 ③-2 跨界需求原案（「产品口径是否按『不存在的功能』披露/删码，交主对话裁定；本包只在 test_meta_format 如实注明不立夹具」）；披露口径取「色温段从不出现（受 Exiv2 钉版约束）」入 CHANGELOG「已知」，既不虚报功能也不删防御代码 |

## 十七、发行裁定（M7-R2.3-G10，2026-10-13）

> 任务书出处 = 会话内分派书（G10 包「发布 0.3.3」，明文允许 git push、授权消耗
> `M7-ADJ-tag-move-2`）；实况证据真源 =
> [docs/m7-evidence/m7r23-release.md](m7-evidence/m7r23-release.md)（预检/push/main-full #60/
> 本机复核/tag 移动/release 链/Release 指纹逐步可复跑）。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tag-move-2（G10 **正式消耗**） | **二次移动已执行，授权消耗完毕**：前提全过——预检 `/releases/tags/v0.3.3` 404（零 Release 零资产，移动前 07:01Z 复检仍 404）、`ls-remote` 核对 v0.3.3 → `cb181ee`/`^{}`=`b1af371`、push main（`fe3b43f..6b571cb`，HEAD `6b571cb`）、**main-full #60 全绿**（completed/success，十 job 含总闸，17m11s）、本机复核全绿（ci-format violations=0 196 文件 + make_winzip 五门禁 rc=0 含 E-2 冻结行逐字）。原子执行 `git tag -f -a v0.3.3 -F tagmsg.txt 6b571cb`（注解原文保留）+ `git push --force origin refs/tags/v0.3.3` → `+ cb181ee...0aba959 (forced update)`；`ls-remote` 核对 tag 对象 **`0aba959`** → `^{}` = **`6b571cb`**（= 任务书「指向新 HEAD」）。release.yml run 38360204850 七 job 全绿、publish 执行，**GitHub Release v0.3.3 已创建**（published 2026-10-13T07:19:43Z，4 资产，API digest ↔ 下载件 SHA256SUMS.txt 逐字交叉一致）。**此后 tag 落点不得再动（含任何红灯情形）——任务书步骤 8「禁止第三次移动」自此生效** | ① 用户明确指令「修复后发布 0.3.3」= 任务书授权原文；② 三条件逐条满足（预检零 Release 零资产 / 原子 + ls-remote 核对 / 移动后不得再动），与十节首次移动同口径；③ 前置「main-full 至绿」经 #60 实测达成（非放宽）——G7 工程解（M7R23G7-0）消化三班残余③后，判据可诚实达成；④ 移动后即刻跟到 publish 并核对 Release 资产（十节条件六「跟到 publish」），发布链无死角 | G10（[m7r23-release.md](m7-evidence/m7r23-release.md) §1–§6；CHANGELOG [0.3.3]「发行终态」条 + m7-report §3.12） |

## 十八、0.3.4 热修裁定（M7-R2.4，2026-10-13）

> 背景 = 0.3.3 Windows 发行件「双击打不开」P0（M7-WP5 无边框收口的 `winId()` 重入递归建窗）。
> 任务书出处 = 会话内用户指令四条：「1 先进行修复 2 为什么门禁没有发现 3 开发测试为什么没有发现
> 4 发布0.3.4」——明文授权修复 + 发布（含 git push / tag / Release）。根因与归因真源 =
> [docs/0.3.4-report.md](0.3.4-report.md)。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7R24-1 | **frameless 过滤器/路由句柄只读化**：`nativeEventFilter` 与 `trigger()` 的句柄来源 `winId()`（会隐式建窗）改为 `internalWinId()`（只读当前句柄、永不建窗）；未建窗 = 0 → 消息放行 Qt。冻结面零改动（frameless 非 PP-FROZEN 面；`--version`/SMOKE/UI-SMOKE 冻结行/SCHEMA 逐字未动） | 根因定位：窗口创建期（`CreateWindowEx` 同步投递 `WM_NCCALCSIZE`）`winId()` 重入 `createWinId()` 递归建窗直至栈崩（0.3.3 打不开 P0）；句柄只读化从机制上消灭"消息处理期隐式建窗"整类隐患，行为面零变化（金样 20/20 + 回归 zero diff + ui-smoke 冻结行背书） | R2.4-G1（[0.3.4-report.md](0.3.4-report.md) §1–§2） |
| M7R24-2 | **winzip 门禁 5 → 6**：新增 E-3 真机启动探针（`tools/win_launch_probe.py`，冻结行前缀 `WIN-LAUNCH OK`），staging exe 走真实 qwindows 平台断言「可见主窗 + 顶层窗口数不超阈 + 进程存活」 | 判据只增不减：offscreen 判据（金样/--ui-smoke/ctest）不触达真实建窗路径，此类缺陷需要真实平台判据；探针自带阳性对照（0.3.3 坏包被拦 `window-storm windows=47>32`，修复后 `WIN-LAUNCH OK windows=6 visible=1`），本地与 CI 同一判据 | R2.4-G1（同上 §2） |
| M7R24-3 | **回归基线重播种 0.3.4**：`golden.windows.log` 走 `--update`（R-015 归因护栏先行）、`golden.log` 机械换锚 `version=0.3.3→0.3.4`；归因 = **版本行 12 / 行为 0** | 版本单源 bump 的既有体例（先例 97cd9da「0.3.3 重播种：12 行逐行归因：版本行 12 / 行为 0」）；版本串是日志行内容、非行为面，语义零变化 | R2.4-G2 |
| M7R24-4 | **发行授权与 tag 纪律**：用户明文「发布 0.3.4」= push/tag/Release 授权；tag `v0.3.4` 指向发行树（修复 + 门禁 + 版本 0.3.4 + 口径），**报告类提交不入 tag**（收口报告与产物指纹在 tag 后补记）；v0.3.3 tag 落点与 Release 记录**不再改动**（坏件只做口径标注） | 对齐 §9 发行纪律与 M7-ADJ-tag-move-2 终态条款（tag 资产挂载后不可移动）；0.3.3 坏件的处置取「标注 + 引导换 0.3.4」最小面 | R2.4-G3（CHANGELOG [0.3.4]「发行」节） |

## M7R25 批（GUI 视觉缺陷根治 + 识图门禁；起因 = `debug/debug1.png` + `debug/debug2.png`）

> 背景 = 用户两张红框截图（7 处可见缺陷）+ 追问「0.3.3 之后的发布没有任何测试吗？…没有识图步骤吗？」。
> 真源 = [m7r25-report.md](m7r25-report.md)（缺陷逐条根因 + 门禁缺口审查）。判据只增不减：冻结行
> `UI-SMOKE OK shots=12 pages=3` / `SMOKE total=20 pass=20 fail=0` / `--version` 逐字未动。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7R25-1 | **`formkit::lock_card_size` 下限改实时 sizeHint**：不再 `setMinimumHeight(sizeHint)` 钉一次性数值，改 `setSizePolicy(Preferred, Minimum)`（无 ShrinkFlag ⇒ 父布局把 sizeHint 当下限，随内容实时变化）+ 清掉历史钉数 | 一次性钉数在内容一变就失效，两侧都出过可见缺陷：① 钉高时把隐藏行计入 ⇒ 空态收起 5 行 kv 后卡体整片留白（debug1 红框 2）；② 建卡时参数表单还空 ⇒ 只钉 84，等参数行建好后整卡被压到 84，28px 行被布局压缩后又被固定高弹回 ⇒ 按 6px 节奏互相重叠（debug2 红框 3/4）。layout-audit ⑥/② 钉守 | R25-1 |
| M7R25-2 | **三页滚动条常驻**（`page_meta` / `page_output` / `page_run` 的页级 QScrollArea 均 `Qt::ScrollBarAlwaysOn`） | 内容高随选中/运行态变化会把竖滚动条"顶"出来/收回去，内容宽随之 ±15px，磁贴 2×4 栅格（列 stretch 等分）整体横移 —— 既是用户可见的整卡回流，也让既有断言 `tile-zero-deform`（勾选零形变含位置）漂移；常驻 ⇒ 内容宽恒定 | R25-2 |
| M7R25-3 | **渲染意图项文案改短 + 默认标记入注释槽**：`相对比色 + 黑点补偿（默认）` → `相对比色+黑点补偿`，「（默认）」改由行尾 10px 注释槽按当前选择显示/隐藏 | R2 的 160 标准轨不许拉满（禁止同列不同宽）；旧串 150px 墨迹在 132px 文本区里被硬裁成「相对比色 + 黑点补M」（debug2 红框 2）。默认档标记移到 R3 的提示位，未选默认档时不显示（诚实显示） | R25-3 |
| M7R25-4 | **左栏分组下拉改短档 88 + 最长项改「按机型」**（原「按相机型号」） | 306px 工具行要装 4 个 28px 图标钮 + 下拉 + 搜索框；标准档 160 会把 `pp-search` 挤成 36px、占位符硬裁成「···」（debug1 红框 1 旁）。短档即 mockup 工具行口径；短档文本区 62px 只容 ≤4 字，故最长项改「按机型」（节头仍用「相机型号」，`pp::GroupKey` 语义不变） | R25-4 |
| M7R25-5 | **新增 layout-audit 常驻探针 + ui_smoke.py 追加齐备行**（`UI-SMOKE layout-audit:`）：六类几何/像素断言（text-clip / overlap / blank-btn+ink-btn / overflow / ghost / card-blank） | 用户点名「没有识图步骤吗」——`tests/ui_smoke.py` 头此前明写「截图仅存档，不做像素比对」，渲染面缺陷全靠人眼。探针把识图机械化：12 张截图仍按原口径人工留档（不改"不比对"的既定取证口径），但渲染面从此有机器看守。判据只增不减：冻结末行与既有清单零改动 | R25-5 |
| M7R25-6 | **`make_flex_field` 拉伸语义修正**：控件一律贴字段轨起点、富余宽归行尾 stretch（旧写法给首个控件 stretch 1） | 单开关行的 30×16 开关被居中到字段中段，与「分文件夹」行的开关不同列（debug2 红框 3）；同列同轴是 R2 的硬口径 | R25-6 |
| M7R25-7 | **`--version` 冻结行正则机械换锚**：`^PhotoPipeline \d+\.\d+\.\d+$` → `^PhotoPipeline \d+\.\d+\.\d+(\.[A-Za-z][A-Za-z0-9]*)*$`（三处同锚：`tools/make_winzip.py` 烟测 E-1、`.github/workflows/release.yml` appimage 烟测、`.github/workflows/main-full.yml` 同款） | 版本口径引入**预发布标签**（0.3.5.alpha1 = 数字段 + ".alpha1"）后，旧锚只认三段数字 ⇒ 发行链当场红（winzip E-1 实测拦截）。换锚**语义不变**（仍断言"`PhotoPipeline <version>` 单行"这一冻结格式），只是允许版本带一个可选标签段；§5.3「机械换锚（语义不变）须登记裁定」 | R25-7（REL-0.3.5.alpha1） |

## 十九、0.3.5.alpha2 缺陷修复裁定（2026-10-25）

> 背景 = 用户七条实测问题（高德搜索 / Adobe RGB / ICC / HEIF 无损与 10bit / 无边框三钮 /
> 仅元数据图像字节保真）。原则：冻结面只走「加性 + 追认」；引擎行为变化由金样 20/20 +
> 全语料回归 zero diff 双重背书；断言只增不减。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A2-1 | **`core/thumbs.h` `ThumbImage` 末尾加性追加 `std::string icc`** | 预览/缩略图需要源 ICC 才能在 UI 侧附 `QColorSpace`（Adobe RGB 显示缺陷）。字段追加在末尾、既有字段/聚合初始化兼容；像素字节不变，仅显示面 | REL-0.3.5.alpha2 |
| A2-2 | **无边框三钮 `WM_NCHITTEST` 返回 `HTCLIENT`**（不再返回 `HTMINBUTTON/HTMAXBUTTON/HTCLOSE`） | 三钮是 Qt 客户区控件；返回 HT* 会让 Qt 平台插件吞掉点击（真机实测关闭既不触发 clicked 也不产生 WM_CLOSE）。改 HTCLIENT 由 QPushButton 处理 press/release/hover；代价 = 放弃 Win11 Snap Layouts 悬停菜单 | REL-0.3.5.alpha2 |
| A2-3 | **HEIF 参数派发修复**：`ParamType::Enum` 按 `ParamValue` 实际类型分派 int/string setter；`lossless` 显式消费 `lossless`/`__lossless`；落盘后回读位深 | 旧实现把字符串 Enum 当整数丢弃（chroma/preset/tune 从未生效）、`--lossless` 只写内部键时对 HEIF 无效；位深回读防止静默 8bit。用户实测缺陷 + 金样/回归背书 | REL-0.3.5.alpha2 |
| A2-4 | **HEIF/AVIF 容器 ICC 在 probe 边界补读**（libheif `heif_image_handle_get_raw_color_profile_*` 注入 `ImageSpec`） | OIIO heif reader 不暴露 `colr/prof` ICC ⇒ `has_icc=false`/预览 sRGB/“ICC 没了”的感知缺陷；补读不改变像素，输出仍由管线统一嵌 ICC | REL-0.3.5.alpha2 |
| A2-5 | **搜索源与瓦片源解耦**：有高德 Key 时搜索/联想/逆地理一律高德；Key trim + `AMAP_KEY=`/引号兼容；错误码追加提示 | 默认 OSM 瓦片 + 高德 Key 的用户永远走 Nominatim（大陆不可达）⇒ “搜索失败”；本条为可达性修复，不改 URL 事实表 | REL-0.3.5.alpha2 |
| A2-6 | **HEIF 档位文案改「Main10（10 位 · Intra）」** | x265 静止图固定写 Main10 Intra（idc=4，参考 T2.heic 相同）；位深确为 10。文案对齐真实信号，避免工具显示 Main 时被误判为 8 位 | REL-0.3.5.alpha2 |
| A2-7 | **回归基线重播种 alpha2**：`golden.windows.log` 走 `--update`；`golden.log` 机械换锚 | 归因：版本行 12 + HEIF 档位日志标签 23；行为 0（无像素/元数据/字节变化） | REL-0.3.5.alpha2 |
| A2-8 | **发行授权**：用户明文「发布0.3.5.alpha2」= 版本 bump + git push + tag `v0.3.5.alpha2` + GitHub Release 授权 | 对齐 §9 发行纪律；tag 指向发行树，报告类/收口提交不入 tag；tag 资产挂载后不可再移动 | REL-0.3.5.alpha2 |
