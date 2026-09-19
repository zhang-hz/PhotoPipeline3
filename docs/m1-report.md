# PhotoPipeline M1a（引擎批次）交付报告

> **批次范围**：M1 拆分后的**批次 1 = M1a 引擎**（core / decode / codecs / pipeline / scheduler / dev harness / 校验器）。**不含 UI**（T9–T13 + T14b 归 M1b 下一批次）。
> **执行方式**：subagent 分波次执行，全部接口与行为契约由主对话在 `docs/m1-tasks.md` 冻结；每波次报告经主对话裁定后并入文档。
> **结论**：出口准则 **8/8 PASS**（独立收口复验 T14 + 收尾清理 T15）；引擎侧无开放高危风险项。

---

## 1. 任务与提交谱系

| 波次 | 任务 | 结果 | 提交 |
|---|---|---|---|
| W1 | T1 core 基础设施（logger/fsops/pixelbudget + CMake glob 收编） | ✅ | `7cf2d49` `b4b09ab` |
| W1 | T2 参数引擎 + 预设（78 条 ParamDef 谓词 + preset JSON） | ✅ | `f82272b` `99efa72` |
| W2 | T3 解码层（probe/decode/orientation/ICC） | ✅ | `1a3e637` `c29d104` |
| W2 | T4 色彩层（lcms2 浮点 + LRU + ICC 生成） | ✅ | `bdb879d` `baf1884` |
| W2 | T5 元数据层（合成/时间偏移/GPS/三写入路径/仅元数据） | ✅ | `e267430` `cead210` |
| W3 | T6 编码器 jpegli / libjxl / libwebp + 内部注册表 | ✅ | `9c4c8bb` `45650da` `4a1c07f` `18b5d0c` `4d37d31` |
| W3 | T7 编码器 heif/avif/oiio + 内省 + 位深探测（+ T7c whole-archive） | ✅ | `5bd3476` `92b42f5` `b501ed3` `d3d9a49` `0c108ea` |
| W4 | T8 pipeline + scheduler + `--dev` harness + `pp_verify` + **首次端到端鼓点** | ✅ | `5ebe057` `128513c` `66c4adf` |
| 修复 | T5b exiv2 zlib/PNG（R1） | ✅ | `6e98ca7` |
| 修复 | T5c PNG 路径复核 + 能力探测（R1 闭合） | ✅ | `0235175` |
| 修复 | T6b `EncodeRequest::tech_id`（加性，结构末尾） | ✅ | `aab5d43` `0c04562` |
| 修复 | T7d E9 未知参数 → 仅 `log_warn` | ✅ | `6beea57` |
| 修复 | T7b x265 多比特深度（R19/"禁止精简"） | ✅ | `201c8a8` |
| 修复 | T2b heif/avif 静态位深集 → {8,10,12} | ✅ | `c9d1adb` |
| 复核 | T7e R19 独立复核（+ 断言收紧） | ✅ | `a2b1218` |
| 收口 | T14 引擎收口（复验/清单/基线/README/接口冻结） | ✅ | `5840ad2` |
| 清理 | T15 ccache 回退 + M0 TODO 清理 + smoke 默认目录 + release `--dev` 拒绝 | ✅ | `0852cbc` |

代码规模：`src/{core,decode,codecs}` + `ui/preset_io` = **33 文件 / 8402 行**；测试可执行 **20** 个。

## 2. 出口准则（批次 1，逐项）

| # | 准则 | 结果 | 关键证据 |
|---|---|---|---|
| 1 | 引擎模块落库、无 `TODO(M1)` | PASS | 33 文件 8402 行；`grep TODO(M1)` = 0；linkprobe PLUGINS 24 项 + 3 编码器在列 |
| 2 | 全语料 `--dev` 零崩溃、8 格式全出图 | PASS | 27 输入 × 8 格式 = 216 次编码零崩溃；7 格式 23 ok / 4 预期失败，avif 19 ok / 8 预期失败 |
| 3 | 8+1 对 smoke 全 OK | PASS | `SMOKE total=9 pass=9 fail=0` |
| 4 | 日志完整 | PASS | 运行头 13 项版本（含注入的 qt）+ 参数快照 + 每文件 6 段耗时 + 警告 + budget report + run summary |
| 5 | 单测全绿（含 M0 五条无回归） | PASS | `ctest` → **20/20 passed**（3.95 s） |
| 6 | 仅元数据模式字节级保真 | PASS | JPEG SOS 尾字节 sha256 一致（798 B）+ 像素 hash 一致；PNG IDAT 逐字节；TIFF 像素 hash；WebP VP8L 载荷一致（单测 + 独立 python 复跑） |
| 7 | 规模验证 | PASS | 48MP jxl 2 worker **16.96 s**，预算 `need=peak=1152000000`（=2×帧）`used=0`；100 文件批 webp 8 worker **ok=100**（0.049 s），`peak=786432`（=8×2×帧） |
| 8 | `TODO(M2)` 清单 + 基线归档 | PASS | 23 条分组清单；`.cache/m1-baseline/`（29 文件 + matrix-logs 16 文件） |

**纯 release 验证（设计 §8.6）**：`strings` 检查 dev usage 串 → plain **0** / dev **1**（dev 路径确不存在）；GUI offscreen rc=0；`--dev` 在 plain 构建下 **rc=2 + stderr 明确提示**（T15 在 `QApplication` 之前拦截，避免 Qt 静默吞掉未知长选项）。

## 3. 实测指标（复现基线）

**27×8 矩阵（`--workers 1`，记账口径：27 输入 / 23 成功 / 4 预期失败；avif 19/8）**

| format | ok | 预期失败 | distinct outputs | bytes | total_ms |
|---|---|---|---|---|---|
| jpeg | 23 | 4 | 20 | 14298 | 88.3 |
| jxl | 23 | 4 | 20 | 23205 | 121.6 |
| png | 23 | 4 | 20 | 27521 | 55.6 |
| tiff | 23 | 4 | 20 | 54568 | 52.5 |
| webp | 23 | 4 | 20 | 7602 | 65.3 |
| bmp | 23 | 4 | 20 | 255194 | 41.5 |
| heif | 23 | 4 | 20 | 23826 | 456.2 |
| avif | 19 | 8 | 16 | 16900 | 67.8 |

失败明细全部落在预期类别：CMYK 不支持 ×16、损坏 PNG ×8、损坏 JPEG ×8、`svt-av1 does not support 10-bit with alpha; use backend libaom` ×4（仅 avif 默认后端）。
**注**：镜像路径同 stem 碰撞属设计使然（`base/{gray16,rgb8,rgb16}.{png,tif}` → 同输出名），27 输入产生 20 个不同输出文件。

**位深能力（R19，静态集 ∩ 运行期探测）**：`heif/x265 = 8,10,12`、`avif/libaom = 8,10,12`、`avif/svt-av1 = 8,10`；越界请求 → 明确 error（不静默降档，`avif/svt --bitdepth 12` rc=2）。

## 4. 接口冻结基线（M1b UI 只消费、不改接口）

15 个头文件 / 737 行，全部 `PP-FROZEN(file)`，命名空间 `pp`（仅 `preset_io` 为 `pp::ui`）：

| 头文件 | 关键接口 |
|---|---|
| `core/types.h` | `Stage` / `WarningKind`(8) / `Warning` / `Timing` / `ImageInfo` / `ParamValue` / `ParamSet` |
| `core/logger.h` | `log_init` / `log_*` / `library_versions` / `set_qt_version_string`（M1 加性） |
| `core/fsops.h` | `mirror_path` / `resolve_conflict` / `is_inside` / `collect_inputs` / `input_extensions`(13) / `with_extension` |
| `core/pixelbudget.h` | `PixelBudget{acquire,release,capacity,used,peak,frame_bytes,default_capacity_bytes}` |
| `core/params.h` | `ParamDef/TechDef/BackendDef/FormatDef` + `static_formats` + 谓词/取值/校验/查找/快照 14 函数 |
| `core/colormanager.h` | `ColorTarget` / `to_string` / `parse_color_target` / `ColorManager` / `load_target_icc` |
| `core/metadata.h` | `read_metadata` / `build_plan` / `make_payloads` / `write_metadata_exiv2` / `rewrite_metadata_only` / `sync_file_mtime` / 时间与 GPS 工具函数 |
| `core/pipeline.h` | `FileEntry` / `RunConfig` / `FileResult` / `FileState`(12) / `run_one_file` / `run_metadata_only` / `format_supports_metadata_only` |
| `core/scheduler.h` | `RunSummary` / `Scheduler{set_event_callback,start,cancel,wait,running,results,summary}` |
| `core/presets.h` | `PresetData` / `validate_preset` / `normalize_preset` |
| `decode/oiio_reader.h` | `probe_file` / `decode_float` / `orientation_from_spec` / `icc_from_spec` |
| `codecs/encoder.h` | `MetadataPayloads` / `EncodeRequest`（末位 `tech_id`）/ `EncodeResult`（末位 `error`）/ `IEncoder` |
| `codecs/encoders.h` | `make_encoder` / `introspect_backends` / `probe_bitdepth_support` |
| `codecs/encoder_registry.h` | 内部；**消费 pp_core 的新 target 必须 `$<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>`** |
| `ui/preset_io.h` | `save_preset` / `load_preset` / `list_presets`（QtCore-only，编入 pp_core） |

**两处加性修订**（均追加在结构体末尾）：`EncodeResult::error`（非空=失败，`bytes==0`）、`EncodeRequest::tech_id`（`"vardct"|"modular"|"lossy"|"lossless"|"runtime"|""`）。
**保留键 `__lossless`**：bool；由 `default_params/apply_locks/validate_params/fill_defaults` 与 `run_one_file` 注入；**T10 无损开关必须同步写入**；不序列化、不显示、不计入 `snapshot_params`。
**M1b 待新建**：`core/settings.h`、`platform/paths.h`、`mapwidget/*`、`tests/ui_smoke.sh`（文件当前不存在，按任务书 §3.13/§3.14 冻结文本创建）。

## 5. TODO(M2) 清单（23 条）

- **core 14**：`metadata.cpp:322`（const_cast 回传 warnings，UI 须传非 const plan）、`pipeline.cpp:239`（CICP→lcms2 映射）、`pipeline.cpp:450`（元数据错误串回传）、`fsops.cpp:104`（reserved 归一化 O(n²)）、`fsops.cpp:193`（follow-symlink/进度回调）、`scheduler.cpp:21`（FileEntry 无 spec → 每文件 probe 两次）、`scheduler.cpp:61`（输出名去重为词法近似）、`logger.cpp:207`（日志体积上限）、`pixelbudget.cpp:24`（仅 Linux 内存探测）、`params.cpp:233`（交叉参数约束集中校验）、`colormanager.cpp:44/:306/:458/:599`（仅 float formatter / 第三方 sRGB ICC 非恒等 ≤1.8e-4 / 单例不释放 / 灰度一律升维）
- **codecs 5**：`enc_jpegli.cpp:113`（未知参数 log_warn）、`enc_heif.cpp:383`（x265 无 threads，E2 不可实现）、`enc_heif.cpp:426`（svt 高位深+alpha 上游限制，守卫待撤）、`enc_heif.cpp:631` + `enc_oiio.cpp:280`（冗余链接锚点）
- **ui 2**：`preset_io.cpp:31`（非 UTF-8 locale 路径）、`preset_io.cpp:38`（色彩目标字面量重复）
- **tools 1**：`pp_verify.cpp:222`（元数据比较依赖 Exiv2 渲染）
- **tests 1**：`test_pixelbudget.cpp:83`（sleep 握手可能 flake）

`TODO(M0-CD)` = 0（T15 清理完毕）；`TODO(M1)` = 0。

## 6. 风险册变化（design.md §10）

**闭合**：R1（PNG 元数据，真因 exiv2 无 zlib）· R10（四格式无损重写保真）· R11（float→int 单次转换）· R13（OIIO 的 JXL 色彩描述 = CICP+ICC）· R18（ccache 可用性回退）· R19（x265 多比特深度恢复 8/10/12）。
**M0 已退役**：R2（vcpkg feature 缺口）· R4（libheif 参数面，运行期内省）· R8 的 Linux 段（triplet 近全静态）。
**仍开放**：R15（CI 未在 GitHub 实跑，gcc-13 组合待验）· R8 的 Windows 打包段（M3）· R6/R7（地图 ToS/精度，已接受）· R9（缩略图 ICC，已决策）· R14（jpegli 无 tag，SHA 钉死）· R16/R17（已解决/低）。

## 7. 工程纪律沉淀（本轮新增，后续沿用）

1. **加性接口修订一律追加在结构体末尾**（否则位置式聚合初始化静默错位；`tech_id` 首版误插第 3 位已复现该坑）。
2. **STATIC 库自注册必须 whole-archive**（否则注册表为空、`make_encoder` 全 nullptr；这是"首次通电才炸"的典型）。
3. **`WarningKind` 不承载配置缺陷**（未知参数走 `log_warn`；warnings 只表示影响输出的逐图像质量/语义偏差）。
4. **运行期能力探测 ∩ 静态允许集**（位深等能力；不静默降档，越界即明确 error）。
5. **上游 bug 前置守卫**（svt-av1 的 10bit+alpha 会破坏堆 → 编码前拒绝并点名 libaom，绝不进入上游崩溃路径）。
6. **静态库依赖的自注册/链接问题必须在集成前用 `nm`/`strings` 证伪**（`strings | grep usage` 验 dev 路径、`nm` 验符号归属）。

## 8. M1b（UI 批次）启动条件

- **接口**：只消费 §4 的 15 个头文件，不得修改；新增文件按 §3.13/§3.14 冻结文本创建。
- **三条 UI 专属规则**：① 位深可选值 = 静态集 ∩ `probe_bitdepth_support()`，默认逐格式高质量档（jpeg 8 / jxl 16 / png 16 / tiff 16 / webp 8 / bmp 24 / heif 10 / avif 10）；② avif 10bit + 源含 alpha → 预选/提示 libaom；③ 无损开关必须同步写 `__lossless`（否则 JXL 锁定/vardct 隐藏与 WebP exact 可见性失效），且 JXL 无损时技术选择器自动切 modular。
- **构建**：`CMakeLists.txt` 所有者序列已排到 T9（删 `PP_M0_SMOKE`）→ T14b；建议下一批次在 `CMakePresets.json` 增 `release-dev` 预设（release + `PP_BUILD_DEV=ON`），使 `smoke.sh` 默认目录即可用（当前须显式传构建目录，已在 README 说明）。
- **收口**：`tests/ui_smoke.sh`（offscreen 冒烟）+ 手动走查清单 + 出口准则批次 2 四项。
