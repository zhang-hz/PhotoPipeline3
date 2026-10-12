# M7 开发期裁定记录（主对话裁定 + 各波次登记，随波次滚动追加）

> 依据：`docs/deep-review-0.3.2.md`（331 条 R-id）与 `docs/ux-review-14.md`（14 项）的整改落地。
> 冻结接口（PP-FROZEN）的加性扩展走「加性 + 追认」，逐条登记于此。实施归属列括号内为登记来源
> （各工作包 handoff 的「裁定登记」节原样汇总，编号连续化；原文见 `build/m7-wpN/handoff.md`）。
> 汇总人 = WP11；裁定人 = 主对话（各 WP 报出后经波次收口追认）。
> **编号命名空间**：本表 A/B/C/D/E 与 M7-ADJ-*/WP12-B* 均为**裁定表编号**；勿与 `ux-review-14.md`
> §14 的色彩分级子项（A1–D3）及 `deep-review-0.3.2.md` 附录 B 的拆解蓝图（B.1–B.8）混读
> ——三套编号独立，详见 `m7-report.md` 文首「编号命名空间」注。「WP 任务包」= 主对话逐包
> 分派的会话内任务书（未入 docs/），引述以各 `build/m7-wpN/handoff.md` 为凭。

## 一、冻结面加性 + 追认（结构/接口扩展）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | `core/metadata.h` `BatchRules` 末尾追加 `time_enabled/gps_enabled/privacy_enabled`（默认 true） | I5；「该卡参与规则 = 开关 && 原判据」，缺省 true = 旧会话零迁移 | WP1（I5） |
| A2 | `core/pipeline.h` `FileState` 尾部追加 `DoneWithErrors` | I7；消费接线后补 | WP1（I7） |
| A3 | `core/pipeline.h` `RunConfig` 末尾追加 `color_intent/color_dither8/gamut_mapping/linear_resample`（默认=现行为） | I8；能力类 opt-in 数据面先行，消费归后续（WP10 未交付，见 D12 与 m7-report §3.1） | WP1（I8） |
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
| A29 | 跨 TU 衔接用 extern 声明（不新增第四个头文件） | WP8 任务包（会话内分派书，未入 docs；引述见 build/m7-wp8/handoff.md 裁定 8）授权「新文件」仅列三个 .cpp | WP8（裁定 8） |
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
| B7 | R-206 交叉校验保留 en/zh 双通道归属（不做全通道合并） | 合并 = 既有错误面变化，未裁定不做 | WP2（W2-A9） |
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
| B33 | CMakeLists 对 secret.cpp 登记**两行**（pp_core 收录 + UI REMOVE_ITEM 剔除）而非 WP5 任务包的「一行」（任务包为会话内分派书，未入 docs；引述见 build/m7-wp5/handoff.md A10） | 两种单行做法均不可达（LNK2005 / 与 M2-T13 裁定相抵），如实登记 | WP5（A10） |
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
| B47 | UX-5 语义：GPS 卡关 = 规则编辑面/清除钮置灰，**取点面保持可用**（选点/联想自动勾上保留） | 任务包明确保留；meta-loop 探针正是关态取点 | WP7（W7-A9） |
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
| C10 | R-218 RTL 全清**不做**（本波） | 与「行为零变化重构」硬约束正面冲突且需视觉取证；建议独立任务包带 RTL 截图探针 | WP9 |

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
| D8 | R-115 README ctest 计数**改实**：以 `ctest -N` 实测为准（2026-10-12：release 34 / release-dev 35=34+ui_smoke，两树各由 WP11 本机配置实测）；「33 条」为评审日（2026-10-11）口径（出处 `docs/ux-review-14.md:5` 基线 33/33，WP11 任务包沿用），M7 中 test_icons/test_secret 入编后已增至 35 | 如实取数优于抄旧数；同时给「以实测为准」的非计数口径防再漂 | WP11 |
| D9 | R-121 README 设计取舍登记：CMakeLists 无 `install()`、打包器直读构建树，两器默认构建目录统一 `build/release`；引入 install() 属构建面重构不在本轮 | WP3 跨界需求 2 的落点 | WP11 |
| D10 | R-078 发行说明口径入 CHANGELOG/README：线程读数为估算（`active ≤ W ≤ T`、`ΣE ≤ T`；SynthPump/x265 不入账；threads_total_est 非硬上界）；README 旧句「总活跃线程 ≤ T」改实 | WP8 跨界需求 3 的落点 | WP11 |
| D11 | R-292 词表/枚举镜像 13 组**对账登记**（逐行：收敛 5 / 大部收敛 1 / 部分收敛 1 / 登记 6；二分摘要口径「收敛 6 / 登记 7」= 大部收敛并入收敛、部分收敛并入登记），明细见 `m7-report.md` 附录 C；不改逻辑（镜像收敛=结构性重构，超出注释/文案/常量命名级授权） | 红线 3「逻辑改动一律登记不改」 | WP11 |
| D12 | README/CHANGELOG 用户口径**不写未发生的功能**：色彩可选项（色域映射/抖动/渲染意图/线性光重采样）接口数据面已备、行为未入树（`(void)opts;`），**不列为新功能**，仅在「已知」如实披露 | 「诚实显示」纪律；WP11 任务包（会话内分派书）README 清单含该四项，但 WP10 未交付，如实上报不照抄 | WP11 |

## 五、遗留裁决点（等主对话，本轮不决）

| # | 事项 | 现状 | 建议 |
|---|---|---|---|
| E1 | UX-14 A1 精确码：P3 目标下 VUI CICP 实测 unspecified(2)（期望 12） | 核心判据（不得写死 sRGB）已断言；库侧 nclx 状态机所致 | 如需精确 12 要给 libheif 传 `output_nclx_profile`（改输出字节）——裁定后另起 |
| E2 | BMP 不承载 ICC（OIIO writer 事实） | 测试钉现状（一致地未嵌入） | 产品口径（警示/放弃）待定 |
| E3 | R-163 取消错误归类：编码器 cancel 返回 error，pipeline 归 Failed | UI 侧已按逐输出事实补判 | 引擎侧改判 Cancelled 待裁定（行为面） |
| E4 | R-211 强类型位深（bps/bpp 拆分） | 已显式化注释/文案 | 拆字段涉 UI+pipeline 跨面，待一次性裁定 |
| E5 | R-007 长路径 `\\?\` 归一 | 预检+manifest 已落；写路径无归一（259 硬线） | codecs 面归一后放宽硬线 |
| E6 | WP5 真机验收 10 项（Snap/Aero/圆角/多显示器…） | offscreen 无法替代 | Windows 真机人工签收清单（m7-report §2.4） |
| E7 | Linux 回归基线重播种 | Windows 机不做文本替换移植（R-015 反面先例） | Linux 宿主 / nightly update_baseline 通道 |
| E8 | regression.py:335-337 行型表钉 `[main.cpp]`（TU 迁移后误分类） | 归因护栏自计数失真已人工纠正口径 | 修分类器=改判据工具，另行裁定 |
| E9 | R-296 CICP 映射收敛：`src/core/colormanager.{h,cpp}` 的 `map_cicp_source`/`cicp_profile_spec` 分支驱动（加一对 = 4 处联动），建议表驱动单源 | colormanager 跨面（WP2 跨界需求 1），无包认领 | 表驱动重构待裁定后另起 |

## 六、终门禁裁定（WP12，2026-10-12）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tidy-reseed | **tidy 棘轮基线重播种**（`tools/baseline/clang-tidy.tsv`，283/62 → **264/71**）：`ci-tidy.py --update` 落盘，基线 diff 与本提交同在；**frameless.cpp 5→6 为唯一一处既有条目级放宽**（Win32 `reinterpret_cast<HWND>(winId())` / `NCCALCSIZE_PARAMS*` 等 int↔ptr 平台互操作惯用式 + `attach(QWidget*,QWidget*)` 同型参数对，发行门禁期不做签名重构）；总告警 283→264 严格收紧；17 条残余逐条归因（M7 拆 TU 机械迁移 6 / M6 未入基线存量 2 / 发行期禁改的异常路径 2 / 平台惯用式与有意为之 7；等价分解=非 frameless 11 条 + frameless 6 条，见 m7-report §3.7），明细见 `m7-report.md` §3.7 与 `build/m7-wp12/handoff.md`。**NOLINT 抑制案否决**（仓库零先例、抑制注释在关键位点埋回归盲区）。 | 主对话裁决（escalate 问答，条件五条：总量只减 / frameless 放宽逐条理由+本表登记 / 归因随提交正文 / 复跑 exit 0+读数入报告 / 不动 CI 判据与检查集、不加抑制）；触发背景：B.6 四 TU + mainwindow 五域拆 TU 把 mainwindow.cpp(14→1)/main.cpp(8→1) 既有告警机械迁入 10 个基线外 TU，file-keyed 棘轮无法表达「告警随代码搬家」，与「新文件须 0」口径不可兼容 | WP12 |
| WP12-B1 | tidy 棘轮 fallout 的 **38 条修复全部行为零变化**（显式转换 / 删未用量 / 枚举基型 / 按值拷贝 / 字符串拼接序 / 舍入式拆命名临时量保持算子序），金样 20/20 + 回归 zero diff + ctest 35/35 双背书 | 零行为机械修复 | WP12 |
| WP12-B2 | **行为变化（测试面）**：4 个新单测（test_geocoder/test_icons/test_providers/test_secret）的 `main` 包 try/catch——**异常逃逸路径**由 terminate（崩溃）变为打印 `FAILED (unexpected exception)` + 返回 1；仅测试代码、仅崩溃路径，成功与断言路径逐字不动，ctest 判定不变。产品代码的 exception-escape（colormanager `~ErrSync`、mainwindow_run 事件回调 λ）**未动**（修法=吞异常=行为变化，主对话裁决「发行门禁期禁改」） | tidy `exception-escape(main)` 告警的唯一非零行为修复；行为变化按 §6.2 纪律入本表（m7-report §3.7 引用即此条） | WP12 |

## 七、发行裁定（WP13，2026-10-12）

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| M7-ADJ-tag-move | **tag v0.3.3 发布前移动一次**（97cd9da → b1af371）：首落点 97cd9da 树缺 `vcpkg.json` 版本同改（R-122），release.yml「依赖单源一致性断言」6s 红、publish 未执行、**tag 零 Release 零资产** ⇒ 该 tag 不是可发行树。移动前后阴阳性对照在案（97cd9da 树 `ci-versions-check` FAIL，与 CI 报错逐字同因 / b1af371 树 OK (10 keys, expect=2)）；移动操作原子（`git push --force origin refs/tags/v0.3.3`，`ls-remote` 核对 tag 对象 `cb181ee` → `^{}` = `b1af371`）；移动前取证：gh CLI 未认证、releases 列表无 v0.3.3、/releases/tag/v0.3.3 仅 2 个自动源码包（真发布 6 资产，如 v0.3.2）。**裁决人 = 主对话（escalate 问答，条件七条：取证 / 阴阳性对照 / 原子 + ls-remote 核对 / 跟到 publish / 再红即停 / 本登记 / 如实定状态）** | ① 用户本轮指令 = 发布 0.3.3，发布不完成即未交差；② 权威口径 = AGENTS.md §9「tag 资产挂载后不可再移动」+ release.yml 自述「tag 未发布资产前可移动，发布后不可移动」——移动时 tag 零资产零 Release，处明文允许区间；③ 旧目标 97cd9da 仍在历史、b1af371 为其后续修复提交，可逆 | WP13 |
| WP13-B1 | **`vcpkg.json` `version` 0.3.2 → 0.3.3 与 `CMakeLists.txt:19` 同改**（跨出 WP13 文件面的机械同改，落点 `M7-REL-0.3.3-gate-fix2` = b1af371）：`tools/ci-versions-check.py:12-13` 契约注释原文即预点名「R-122：版本 bump 必须同改；WP13 的 0.3.3 bump 会在这里被钉住」——属版本单源 bump 义务；`builtin-baseline`/`dependencies` 逐字未动 | R-122 断言自 M7-WP3 起在 CI 钉住；首推 tag 时该项红灯后补齐，阴阳性对照在案（gate-fix2 提交正文） | WP13 |
