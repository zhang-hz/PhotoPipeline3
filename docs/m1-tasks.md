# PhotoPipeline M1 任务书 v1.0

> **阶段目标**：Linux 上**一次性完成全部编码**——核心引擎（core）+ 8 编码器 + 元数据全量 + 调度器 + dev harness + 三页 UI。
> **文档地位**：M1 唯一执行依据，由主对话（架构者）维护。执行者是 subagent，**只执行本文档明确写出的动作**。
> **继承**：M0 任务书 `docs/m0-tasks.md` §1 的 16 条开发纪律在 M1 继续全额生效；本文档 §1 为 M1 补充与强化。
> **M0 成果基线**：依赖已装（OIIO 3.1.14 / libheif 1.23.1 / libjxl 0.11.2 / libwebp 1.6.0 / lcms 2.19.1 / exiv2 0.28.8 / spdlog 1.17.0 / x265 4.2 / aom 3.13.3 / svt-av1 4.1.0 / tiff 4.7.2 / jpegli@031a0077）；语料 27 fixture 字节稳定；参数表 78 条已编译；fresh 链路 8.1s；出口准则 9/9 PASS。

---

## 1. 开发纪律（M1 专属，在 M0 §1 之上叠加）

1. **只做任务书明列的动作**；未覆盖情况 = 停该项，写入报告 `next-needed`，禁止自行决策。
2. **禁止修改 docs/**（全部文档只读）。
3. **PP-FROZEN 逐字节**：M0 三个头文件（`core/types.h` / `core/params.h` / `codecs/encoder.h`）+ 本文档 §3 新增全部头文件 + §5 CLI/schema 契约，**签名与语义一字不改**。实现体（.cpp）自由，接口不得动。
4. **禁止再下发 subagent**——你是最底层执行者。
5. **禁止改依赖与构建基础设施**：`vcpkg.json`、`vcpkg-overlay/`、`tools/bootstrap.sh`、`tools/env.sh*`、`tools/gen_corpus.sh`、`vcpkg/`、`.toolchain/`。CMake 改动**仅限**本文档 §2.3 明确列出的构建增量。
6. 设计/结构/接口/参数语义疑问 → 报告，不实现、不绕过。
7. 网络仅限：官方文档/仓库源码查询、OSM/Nominatim 与高德地图服务（仅 UI 地图运行时用途）。禁止抓取其他内容。
8. 文件写入仅限仓库内；临时文件用 `.cache/tmp/`。
9. **Git 纪律**：每个任务至少 2 次提交——`M1-<task-id>: <模块>` 与 `M1-<task-id>: tests`（可合并为一次若测试与实现同文件域）；只 `git add` 本任务文件；禁止 push/rebase/reset；遇 `index.lock` 等 5s 重试。**红构建不提交**。
10. **事实来自命令输出**：版本号/API 签名/头文件行号必须来自 `vcpkg_installed` 实际文件或运行输出。
11. **鼓点纪律（M1 核心）**：每完成一个模块，必须跑①该模块单测 ②（T8 起）`--dev` 全语料冒烟；**红了不许开下一个任务**，修复或报告。
12. **`TODO(M2)` 标签纪律**：写码时觉得糙、可优化、未覆盖边界处一律 `// TODO(M2): <具体描述>`；M1 收口时由 T14 汇总成清单。
13. **断言策略**：内部不变量用 `assert`（通道∈{1,2,3,4}、位深∈格式集合、预算非负、指针非空）；用户输入错误走错误返回（`std::string err`），不 assert。
14. **取消语义**：像素预算 `acquire` 处必须检查取消标志（防死锁）；阶段边界检查；编码器内部不中断。
15. **禁止新增第三方库**（Qt 只准用 Widgets/Core/Gui/Network；JSON 走 Qt；无 nlohmann/无 GTest——单测继续手写断言）。
16. **报告 ≤150 行**，按 §9 格式；错误日志每段 ≤40 行。

## 2. 任务分组、波次与构建增量

### 2.1 任务总表

| 波次 | 任务 | 内容 | 依赖 |
|---|---|---|---|
| **W1** | T1 | core 基础设施：logger + fsops + pixelbudget | — |
| | T2 | 参数引擎：params 谓词/锁定/序列化 + PresetData + preset_io(QtCore) | T1 |
| **W2** | T3 | 解码层：oiio_reader（probe + float32 读取） | T1 |
| | T4 | 色彩层：ColorManager（lcms2 浮点 + 变换缓存） | T1 |
| | T5 | 元数据层：读取/合成/时间偏移/GPS/载荷/三路径写入/仅元数据模式 | T1 |
| **W3** | T6 | 编码器：jpegli(JPEG) + libjxl(JXL) + libwebp(WebP) | T3,T4 |
| | T7 | 编码器：libheif(HEIF/AVIF) + OIIO(PNG/TIFF/BMP) + 10bit 能力探测 | T3,T4 |
| **W4** | T8 | pipeline + scheduler + `--dev` harness + `pp_verify` + **首次端到端全语料鼓点** | T2–T7 |
| **W6（本批次）** | T14 | **引擎收口**：全量复验（ctest / smoke / 27×8 矩阵 / 48MP+100批）+ TODO(M2) 汇总 + 基线归档 + 引擎侧出口准则 | T1–T8、T5b、T6b、T7b、T7d |
| **W5（下一批次）** | T9 | UI：主窗口三区 + 文件列表模型 + 缩略图 | T8 |
| | T10 | UI：参数表单引擎 + 输出页 + 设置对话框 | T8 |
| | T11 | UI：元数据页 + 单文件元数据编辑器 + 预设管理 | T8 |
| | T12 | UI：运行页 + platform/paths + settings.ini | T8 |
| | T13 | UI：地图控件 + 瓦片提供方 + GCJ-02 | T12 |
| **W7（下一批次）** | T14b | **UI 收口**：offscreen 冒烟 + UI 走查 + 出口准则（UI 侧） | T9–T13 |

> **批次划分（主对话决策）**：**本批次（本轮）= W1–W4 + W6 引擎收口，不做 UI**；W5/W7（T9–T13、T14b）整体归**下一批次**。design.md §9.1 的 M1 相应拆为 **M1a（引擎）** / **M1b（UI）**；本轮结束时把全部 core/codecs 接口冻结清单交下一批次（UI 只消费、不改接口）。
| **修复** | T5b | 依赖修复：exiv2 打开 zlib/PNG（R1）——feature 优先、overlay 兜底 | T5 | ✅ `6e98ca7`（Path A：port 自带 `png` feature，一行改动 + 17s 重装） |
| | T7b | 依赖修复：x265 高比特深度（10bit HEIF，R19/"禁止精简"）——同样 feature 优先 | T7 | ✅ `201c8a8`（Path B：overlay 多库合并，heif/x265 → 8,10,12） |
| | T2b | 数据扩展：heif/avif 静态位深集 → `{8,10,12}`（默认仍 10，运行期交集收敛） | T7b | 待派 |
| | T7e | R19 复核：T7b 后重跑位深探测 + 单测 | T7b | 待派 |

### 2.2 执行编排（主对话控制，执行者只关心自己的任务）

- W1 两任务可并行（T2 只依赖 T1 的头文件，不依赖实现）；W2 三任务并行；W3 两任务并行；W4 单任务（关键集成点）；W5 五任务并行；W6 单任务。
- 并行任务的公共纪律：**绝不修改他人文件域**；跨域需求写报告由主对话裁决。
- **并行构建隔离（强制）**：并行波次中每个任务使用**自己的构建目录**：
  `cmake --preset release -B build/<task-id> -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build build/<task-id> && ctest --test-dir build/<task-id>`
  （依赖已在 `vcpkg_installed` 就位，关掉 manifest 自动安装以避免并发写；`build/release` 归 T8/T14 的正式端到端验证，其他人不得占用。）

### 2.3 构建增量（唯一授权的 CMake 改动）

> 为消除并行任务的 `CMakeLists.txt` 争用，采用**源文件 glob 自动收编**：新增 `.cpp` 落盘即被编译，任何任务都不需要改 CMakeLists（T1/T2/T3–T8 全部受益）。
> **所有者**：T1 在一次提交内完成下列改造；T9 只负责删除 `PP_M0_SMOKE` 定义；T8 只负责新增 `pp_verify` target。

```cmake
# 1) 组件与静态 jpegli
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)   # Core/Gui 随 Widgets
find_package(libjpeg-turbo CONFIG REQUIRED)             # jpegli-static（M0 已就位，勿删）

# 2) pp_core：glob 收编 core/decode/codecs（CONFIGURE_DEPENDS → 新增文件自动生效）
file(GLOB_RECURSE PP_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/core/*.cpp
     ${CMAKE_SOURCE_DIR}/src/decode/*.cpp
     ${CMAKE_SOURCE_DIR}/src/codecs/*.cpp
     ${CMAKE_SOURCE_DIR}/src/ui/preset_io.cpp)   # 唯一 Qt 适配层（JSON），UI/harness/测试三处复用
add_library(pp_core STATIC ${PP_CORE_SOURCES})
target_include_directories(pp_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pp_core PUBLIC OpenImageIO::OpenImageIO JPEG::JPEG
    libjpeg-turbo::jpegli-static Exiv2::exiv2lib heif PkgConfig::LIBJXL
    PkgConfig::WEBP PkgConfig::LCMS2 PkgConfig::TIFF4 spdlog::spdlog Qt6::Core)

# 3) 单测：glob tests/unit/test_*.cpp，每个文件一个 target + ctest 条目
file(GLOB PP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/unit/test_*.cpp)
foreach(src ${PP_TEST_SOURCES})
  get_filename_component(tname ${src} NAME_WE)          # test_fsops → ctest 名 test_fsops
  add_executable(pp_${tname} ${src})
  target_link_libraries(pp_${tname} PRIVATE pp_core)
  add_test(NAME ${tname} COMMAND pp_${tname})
endforeach()
# 删除 M0 的 add_executable(pp_unit …) 与 add_test(NAME unit …)（被 glob 取代；test_smoke.cpp 自动收编）

# 4) UI 可执行：glob ui/mapwidget/platform
file(GLOB_RECURSE PP_UI_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
     ${CMAKE_SOURCE_DIR}/src/mapwidget/*.cpp
     ${CMAKE_SOURCE_DIR}/src/platform/*.cpp)
list(REMOVE_ITEM PP_UI_SOURCES ${CMAKE_SOURCE_DIR}/src/ui/preset_io.cpp)  # 已在 pp_core，避免重复符号
add_executable(photopipeline src/main.cpp ${PP_UI_SOURCES})
target_link_libraries(photopipeline PRIVATE pp_core Qt6::Widgets Qt6::Network)

# 5) 既有 M0 target（pp_linkprobe / pp_mkfixtures / pp_spikes / 其 ctest 条目）保持不变
#    新增工具按需显式 add_executable（T8 加 pp_verify；其它工具不得改动此块）

# 6) 静态库自注册链接（T7c）：编码器 TU 靠静态初始化注册，静态档案按需选取成员
#    → 不 whole-archive 则 make_encoder 全 nullptr（T7 实测）
#    适用：photopipeline、pp_test_*（glob 循环）、pp_verify、三个 M0 tool target
#    target_link_libraries(<target> PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>)   # CMake ≥3.24
```

**`CMakeLists.txt` 所有者序列（冻结；其余任务一律不得改）**：T1（glob 收编）→ **T7（T7c：whole-archive 链接）** → T8（新增 `pp_verify` target）→ T9（删 `PP_M0_SMOKE` 定义）→ T14（ccache 可用性回退）。

`photopipeline` 现有 `PP_M0_SMOKE` 定时退出宏在 T9 中**删除**（改为 ctest 的 offscreen 冒烟脚本控制超时）。

---

## 3. 冻结接口全集（PP-FROZEN；实现体自由，签名一字不改）

### 3.0 头文件落地约定（T2 提出并实现，主对话批准）

W1 阶段 `presets.h`（§3.11）冻结文本会 include `core/metadata.h`（T5 交付）、`core/pipeline.h`（T8 交付），而后者又 include `core/colormanager.h`（T4 交付）。为避免 W1–W3 期间 `pp_core` 因缺头文件而不可构建，**T2 已把这三个头文件按 §3.6/§3.7/§3.9 的冻结文本逐字节落地为纯接口占位**（无任何实现）。
- T4 / T5 / T8 **不得重写**这三个头文件；开工第一步用 `diff` 与本文档 §3.6/§3.7/§3.9 核对（应逐字节相同），只写 `.cpp` 实现。
- 若发现占位与本文档不一致 → 报告，由主对话裁决；不得自行改动冻结文本。
- `pp_core` 的 glob 会收编这些模块的 `.cpp`；占位期无未定义符号（无人调用），W2/W4 落地实现后自然生效。

> 命名空间统一 `pp`。核心层（`src/core/`）**零 Qt 依赖**（`preset_io` 例外，见 §3.7，位于 `src/ui/` 但只用 QtCore）。

### 3.1 `src/core/logger.h`（T1）

```cpp
// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace pp {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical };

using LogFields = std::initializer_list<std::pair<std::string_view, std::string_view>>;

// log_dir 不存在则创建；min_level 可被环境变量 PP_LOG_LEVEL 覆盖
// （取值 trace/debug/info/warn/error/critical，大小写不敏感）
// 文件名 run-YYYYMMDD-HHMMSS.log；目录内保留最近 20 个 run 文件，多余的删最旧
// 行格式：HH:MM:SS.mmm [lvl] [tid] [stage] [file] message {k=v k=v}（英文）
// warn 及以上立即 flush；日志文件打开失败时降级为 stderr 输出，不得抛异常
void log_init(const std::filesystem::path& log_dir, LogLevel min_level = LogLevel::Info);
void log_shutdown();
void log_set_level(LogLevel lv);
LogLevel log_level();

void log_write(LogLevel lv, std::string_view stage, std::string_view file,
               std::string_view msg, LogFields fields = {}) noexcept;

inline void log_trace(std::string_view s, std::string_view f, std::string_view m, LogFields fl = {}) { log_write(LogLevel::Trace, s, f, m, fl); }
inline void log_debug(std::string_view s, std::string_view f, std::string_view m, LogFields fl = {}) { log_write(LogLevel::Debug, s, f, m, fl); }
inline void log_info (std::string_view s, std::string_view f, std::string_view m, LogFields fl = {}) { log_write(LogLevel::Info,  s, f, m, fl); }
inline void log_warn (std::string_view s, std::string_view f, std::string_view m, LogFields fl = {}) { log_write(LogLevel::Warn,  s, f, m, fl); }
inline void log_error(std::string_view s, std::string_view f, std::string_view m, LogFields fl = {}) { log_write(LogLevel::Error, s, f, m, fl); }

// 版本清单（运行头日志用）：返回 "key=value" 列表
std::vector<std::pair<std::string, std::string>> library_versions();

}  // namespace pp
```

**行为契约**：线程安全（spdlog 自身同步）；`log_shutdown` 幂等；`library_versions()` 至少含 `app`（PhotoPipeline 版本）、`qt`（用 `qVersion()`？→ core 无 Qt，改用编译期宏传入，见下）、`oiio`（`OIIO::get_string_attribute("version")`）、`jpegli`（`jpegli_...`？→ 用 libjpeg 的 `jpeg_...`？无版本 API → 写 overlay 钉的 commit `031a0077`）、`libjxl`（`JxlEncoderVersion()` 十六进制）、`libheif`（`heif_get_version()`）、`exiv2`（`Exiv2::versionString()`）、`lcms2`（`cmsGetEncodedCMMversion()`）、`webp`（`WebPGetEncoderVersion()`）、`tiff`（`TIFFGetVersion()`）、`x265`/`svt-av1`/`aom` 经 libheif 编码器名报告。Qt 版本由 `pp::set_qt_version_string(std::string)` 在 main 里注入（core 不依赖 Qt）。

> **落地修订（T1 完成，主对话批准）**：① `<vector>` 补充 include（§3.1 原文本漏，冻结块无法单独编译）；② 新增声明 `void set_qt_version_string(std::string version);`（命名空间末尾，带 `// PP-FROZEN(block): M1-T1 新增声明`）——**T8 必须在 `main()` 中调用 `pp::set_qt_version_string(qVersion())`**，否则运行头日志缺 `qt` 项；未注入则不输出该项。③ 实测版本字符串形态：`oiio=3.1.14.0`、`lcms2=2190`、`webp=1.6.0`、`libjxl=0x00002afa`（libjxl 的 11002 按位段解码）、x265/svt-av1/aom 用 libheif 描述名。④ app 版本字面量 `"0.1.0"` 硬编码于 logger.cpp（改 `project(... VERSION)` 时需同步，已登记 §8）。

### 3.2 `src/core/fsops.h`（T1）

```cpp
// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

enum class ConflictPolicy { Skip, Overwrite, Rename };

struct OutputPlan {
    std::filesystem::path out_path;  // 最终路径
    bool skip = false;               // Skip 策略且目标已存在
    int rename_index = 0;            // Rename 序号（0=原名；1→"name (1).ext"）
};

// 镜像路径：out_root / (src 相对 base_dir 的路径)，扩展名替换为 new_ext（不含点）
// base_dir 非 src 前缀时 → out_root / src.filename()
// new_ext 为空 → 保留原扩展名
std::filesystem::path mirror_path(const std::filesystem::path& src,
                                  const std::filesystem::path& base_dir,
                                  const std::filesystem::path& out_root,
                                  std::string_view new_ext);

// 冲突解析：desired 已存在或落在 reserved 中时按 policy 处理
// Rename：依次尝试 "stem (1).ext"、"stem (2).ext"…（上限 10000，超出返回 err）
// reserved 为本批次已分配但可能尚未落盘的路径（批内冲突，G4）
OutputPlan resolve_conflict(const std::filesystem::path& desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path>& reserved,
                            std::string& err);

// child 是否位于 parent 之内（词法比较，均先 weakly_canonical）
bool is_inside(const std::filesystem::path& child, const std::filesystem::path& parent);

// 递归收集：扩展名白名单（大小写不敏感，不含点）；目录不存在/无权限 → 跳过并记入 errors
std::vector<std::filesystem::path> collect_inputs(const std::vector<std::filesystem::path>& roots,
                                                  const std::vector<std::string>& exts,
                                                  std::vector<std::string>& errors);

// 输入扩展名白名单（10 种输入）：tif/tiff/png/jpg/jpeg/jxl/heic/heif/avif/webp/bmp/gif/tga
const std::vector<std::string>& input_extensions();

// 替换扩展名（保留目录与 stem）
std::filesystem::path with_extension(const std::filesystem::path& p, std::string_view new_ext);

}  // namespace pp
```

> **语义澄清（T1 实现并实测，主对话批准为冻结解释）**：
> ① `with_extension(p, "")` → 保留原扩展名（与 `mirror_path` 一致，容忍前导点）；
> ② `is_inside(child, parent)` 为子树语义，`child == parent` → `true`；
> ③ `collect_inputs`：同路径去重、结果按路径排序、**空白名单 = 收全部常规文件**、目录不存在/无权限记 `errors`；
> ④ `resolve_conflict` 仅在 rename 序号耗尽（10000）时写 `err`（此时 `out_path` 仍为 `desired`、`skip=false`）。
> `input_extensions()` 返回 13 项（文档注释原写"10 种输入"有误，以列表为准）。

### 3.3 `src/core/pixelbudget.h`（T1）

```cpp
// PP-FROZEN(file)
#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace pp {

// 全局像素预算（令牌池）。容量 = min(可用RAM×50%, 8GB)，由 Scheduler 构造时决定。
class PixelBudget {
public:
    explicit PixelBudget(uint64_t capacity_bytes);

    // 阻塞直到可用；期间每秒检查一次 cancelled（true → 返回 false，不扣额）
    // bytes > capacity → 直接返回 false（调用方报错，不得死等）
    bool acquire(uint64_t bytes, const std::function<bool()>& cancelled);
    void release(uint64_t bytes);

    uint64_t capacity() const;
    uint64_t used() const;      // 已占用
    uint64_t peak() const;      // 历史峰值（日志用）

    // float32 帧字节数 = w * h * channels * 4；旋转/合成峰值为 2×（G2）
    static uint64_t frame_bytes(int w, int h, int channels);
    static uint64_t default_capacity_bytes();  // min(可用RAM×50%, 8GB)

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    uint64_t capacity_, used_ = 0, peak_ = 0;
};

}  // namespace pp
```

### 3.4 `src/core/params.h` 增量（T2；在 M0 冻结文件上**追加**，不得改动既有内容）

```cpp
// PP-FROZEN(block): 追加在 static_formats() 声明之后
// —— 参数引擎 ——
// 求 lock：无 locked 谓词或返回 nullopt → 未锁定；返回有值 → 该值为强制值
std::optional<ParamValue> eval_lock(const ParamDef& p, const ParamSet& s);
bool eval_visible(const ParamDef& p, const ParamSet& s);

// 取默认值全集（含 lossless 技术与技术默认选择）
ParamSet default_params(const FormatDef& f, const std::string& backend_id,
                        const std::string& tech_id, bool lossless);

// 应用锁定：把所有被锁定参数的当前值改写为强制值（返回被改写的 key 列表）
std::vector<std::string> apply_locks(const FormatDef& f, const std::string& backend_id,
                                     const std::string& tech_id, bool lossless, ParamSet& s);

// 类型安全取值（缺失或类型不符 → 返回 fallback）
int64_t param_int(const ParamSet& s, std::string_view key, int64_t fallback);
double  param_float(const ParamSet& s, std::string_view key, double fallback);
bool    param_bool(const ParamSet& s, std::string_view key, bool fallback);
std::string param_str(const ParamSet& s, std::string_view key, std::string_view fallback);

// 校验：范围/枚举成员/类型；返回空串=通过，否则错误描述（英文）
std::string validate_params(const FormatDef& f, const std::string& backend_id,
                            const std::string& tech_id, bool lossless, const ParamSet& s);

// 查找：找不到返回 nullptr
const FormatDef* find_format(std::string_view id);
const BackendDef* find_backend(const FormatDef& f, std::string_view id);
const TechDef* find_tech(const BackendDef& b, std::string_view id);

// 旧预设迁移：缺失 key 用默认值补齐（返回补的 key 列表）
std::vector<std::string> fill_defaults(const FormatDef& f, const std::string& backend_id,
                                       const std::string& tech_id, bool lossless, ParamSet& s);

// 参数快照（日志用）："key=value key=value"（按 key 字典序，value 统一字符串化）
// **不含** "__" 前缀的保留键（lossless 在日志中另有独立字段）
std::string snapshot_params(const ParamSet& s);
```

> 谓词填充由 T2 完成：`format_tables.cpp` 中现有 78 条 `ParamDef` 的 `visible`/`locked` 字段当前为空（M0 留白），T2 **只允许填写这两个字段**（以及为它们服务的 lambda），**禁止改动 key/type/def/lo/hi/choices/advanced/tooltip**。填写的谓词规则见 §4.2。

**保留键约定（M1 冻结；T2 提出，主对话批准）**

谓词签名冻结为 `std::function<bool(const ParamSet&)>`，只能看到参数集，因此**"当前是否无损"通过参数集内的保留键传递**：

| 项 | 约定 |
|---|---|
| 键名 | `"__lossless"`（双下划线前缀 = 保留键；`ParamValue` 为 `bool`） |
| 注入点 | `default_params` / `apply_locks` / `validate_params` / `fill_defaults`（T2 已实现）；**T8 `run_one_file` 构造有效参数集时必须置为 `cfg.lossless`**；**T10 参数表单随无损开关同步写入**；T11 预设对话框经 `PresetData.lossless` 同步 |
| 排除点 | **不序列化**进预设 JSON（顶层 `lossless` 字段承载）；**不显示**在任何 UI 控件；**不计入** `snapshot_params` 输出（日志另有独立 lossless 字段）；`validate_params` 不校验其范围 |
| 冲突 | 参数表中不得出现同名 key（T2 已核对）；若未来新增以 `__` 开头的键，一律视为保留键 |

### 3.5 `src/decode/oiio_reader.h`（T3）

```cpp
// PP-FROZEN(file)
#pragma once
#include <OpenImageIO/imagebuf.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "core/types.h"

namespace pp {

struct ProbeOutcome {
    ImageInfo info;
    std::optional<OIIO::ImageSpec> first_spec;
    std::string error;          // 非空=失败
};

// 打开读 spec，不解码像素；多页/动图 → info.is_multipage=true
ProbeOutcome probe_file(const std::filesystem::path& p);

struct DecodeOutcome {
    OIIO::ImageBuf buf;         // float32；失败时 empty()
    std::vector<Warning> warnings;
    std::string error;
};

// 读为 float32；通道保持 {1,2,3,4}；CMYK(4ch separated) 或其它非 {1,2,3,4} 通道数 → error
// （冻结的 WarningKind 无合适枚举可表达"通道截断"，故不静默截断）
// 多页/动图取首页 + Warning{MultipageTruncated}
DecodeOutcome decode_float(const std::filesystem::path& p, const ImageInfo& info);

// 方向标签：从 OIIO spec 属性 "Orientation" 读取（1–8）；无 → 1
int orientation_from_spec(const OIIO::ImageSpec& spec);

// 内嵌 ICC：从 spec 的 "ICCProfile" 属性取字节；无 → 空
std::string icc_from_spec(const OIIO::ImageSpec& spec);

}  // namespace pp
```

> **落地修订（T3 实测，主对话批准；T6/T7/T8 必读）**：
> ① `ImageBuf::read(0,0,0,0,TypeFloat)` 在 OIIO 3.1.14 **会 SIGSEGV**（六参重载 `chend=0` 非法）→ 正确形式 `read(0,0,0,nch,true,TypeDesc::FLOAT)`（imagebuf.h:538 四参 / :560 六参，`chend` 必须 >0）；
> ② CMYK 检测：OIIO 把 separated(CMYK) **读成 3 通道**，仅留属性 `tiff:ColorSpace=="CMYK"`（string）或 `tiff:PhotometricInterpretation==5`（int）→ 两者任一命中即报错，错误串固定 `"CMYK input is not supported"`；
> ③ ICC 是**数组属性**：须无类型 `find_attribute("ICCProfile")` + `type().basetype==UINT8`（实测 `uint8[536]`），标量查询会 MISS；
> ④ 多页判定用 `ImageInput::seek_subimage(1,0)`（`ImageSpec` 无 `nsubimages` 成员）；
> ⑤ `ImageInfo.format` 取 `ImageInput::format_name()`（`ImageSpec::format` 是像素 `TypeDesc`）；
> ⑥ `has_alpha = alpha_channel >= 0 || channels ∈ {2,4}`（GIF 存在 `alpha_channel == 4` 越界索引的 quirk）——**T8 的 flatten / GrayToRgb 判定以此为准**；
> ⑦ ImageBuf 若存在未取走的 error，析构会向 stderr 打印告警 → 失败路径必须 `buf.geterror()`；
> ⑧ 非 {1,2,3,4} 通道数**只在 `decode_float` 报错**（probe 仍如实报告 channels），T8 无需在 probe 阶段拦截。

### 3.6 `src/core/colormanager.h`（T4）

```cpp
// PP-FROZEN(file)
#pragma once
#include <OpenImageIO/imagebuf.h>
#include <cstddef>
#include <string>
#include <vector>
#include "core/types.h"

namespace pp {

enum class ColorTarget { KeepOriginal, SRGB, DisplayP3, AdobeRGB };
std::string to_string(ColorTarget t);          // "keep" | "srgb" | "p3" | "adobergb"
bool parse_color_target(std::string_view s, ColorTarget& out);

struct ColorOutcome {
    std::vector<Warning> warnings;
    std::string error;
    std::string src_desc, dst_desc;   // 日志用："sRGB" / "ICC(Display P3)" / "assumed sRGB"
    std::string icc_to_embed;         // 应在输出中嵌入的 ICC 字节（空=不嵌）
};

class ColorManager {
public:
    static ColorManager& instance();

    // 变换 buf 的颜色通道为 target；alpha 通道原样保留（不使用 lcms2 的 RGBA 类型）
    // src_icc 为空时：src_is_gray → 灰度 sRGB 假定；否则 sRGB 假定 + Warning{NoIccAssumeSrgb}
    // target==KeepOriginal 时不改像素：src_icc 非空 → icc_to_embed=src_icc，否则空
    // 通道 1 → 彩色目标：lcms2 单调用升维（GRAY_FLT→RGB_FLT），无独立灰度管线（复审 S1）
    // 通道 2（灰+α）：变换灰度平面，α 复制
    // 输出目标 ICC：sRGB 用 lcms2 内建；P3/AdobeRGB 从 assets/icc/ 加载（缺失 → error）
    ColorOutcome transform(OIIO::ImageBuf& buf, const std::string& src_icc, bool src_is_gray,
                           ColorTarget target);

    void clear_cache();
    std::size_t cache_size() const;   // 测试用：应随 (src,target,channels,bitdepth) 复用

private:
    ColorManager();
    struct Impl;
    Impl* impl_;
};

// 目标 ICC 加载（assets/icc/ 下文件名固定）："DisplayP3.icc" / "AdobeRGB1998.icc"
std::string load_target_icc(ColorTarget t, std::string& err);  // sRGB → 空串（内建）

}  // namespace pp
```

> **ICC 获取方式（主对话裁定）**：P3 与 AdobeRGB 目标 profile **由 lcms2 在内存中生成**（`cmsCreateRGBProfile`：D65 白点 + 标准原色 + Display P3 用 sRGB TRC / AdobeRGB 用 gamma 2.19921875，并写 profile description tag），不下载、不随仓库分发二进制 ICC——彻底规避再分发许可问题且保持离线可构建。`assets/icc/README.md` 记录该决策与生成参数；若将来需要外部 ICC，再走"来源注明"路径。`load_target_icc` 签名不变（实现改为生成 + 缓存）。sRGB 目标仍用 lcms2 内建 `cmsCreate_sRGBProfile()`。
>
> **T4 落地口径（主对话确认，冻结）**：
> ① lcms2 2.19.1 **无 float 平面格式**（仅 8/16 位 PLANAR）→ 模块侧按通道平面读写、调用前交错为 `TYPE_GRAY_FLT`/`TYPE_RGB_FLT`（语义不变）；
> ② **规范化源规则**：源 ICC 与目标 ICC 字节相同时复用同一 lcms2 句柄 → sRGB→sRGB 恒等实测为**精确 0**（走字节往返副本会引入 ~1.8e-4 暗部串扰；§3.6 的 ≤1e-5 硬指标以此满足）；第三方 sRGB ICC（字节不同）无法塌缩，暗部 ≤1.8e-4 → 已记 TODO(M2)；
> ③ `load_target_icc` 对**三个彩色目标均返回嵌入用字节**（sRGB = 内建 profile 的序列化字节），符合共识 §3.5"始终嵌入"；KeepOriginal + 源有 ICC → 源 ICC 原样；源无 ICC → 空；
> ④ 1ch/2ch 源在 `target != KeepOriginal` 时**已由本模块升维**为 RGB/RGBA（灰+α → RGBA）——调用方**不得二次升维**；
> ⑤ 头注释里的 `assets/icc/*.icc` 文件名保留为未来外部 ICC 的路径说明，当前实现为内存生成（仅注释措辞，已裁定不改头文件）。

**数值契约**：sRGB→sRGB float 恒等 ≤1e-5；sRGB 白 → Lab D50：L∈[99.5,100.5]（M0 Spike E 已证）；意图固定 `INTENT_RELATIVE_COLORIMETRIC | cmsFLAGS_BLACKPOINTCOMPENSATION`。

### 3.7 `src/core/metadata.h`（T5，最大模块）

```cpp
// PP-FROZEN(file)
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <exiv2/exiv2.hpp>
#include "core/types.h"

namespace pp {

struct SourceMeta {
    Exiv2::ExifData exif;
    Exiv2::XmpData  xmp;
    std::string icc;                       // 嵌入 ICC（可空）
    bool has_time = false;
    bool has_gps  = false;
    int  orientation = 1;                  // EXIF Orientation 1–8
    std::string error;                     // 非空=读取失败（调用方决定是否致命）
};

SourceMeta read_metadata(const std::filesystem::path& p);

// —— 编辑模型 ——
struct TagEdit { std::string key; std::optional<std::string> value; bool remove = false; };

struct TimeShift {
    enum class Mode { Delta, TimezoneSemantic } mode = Mode::Delta;
    int years = 0, months = 0, days = 0, hours = 0, minutes = 0, seconds = 0;  // Delta
    int from_offset_min = 0, to_offset_min = 0;                                // Timezone
    bool is_noop() const;
};

struct GpsData {
    double lat = 0, lon = 0;               // 十进制度
    std::optional<double> altitude;
    std::optional<double> direction;       // 0–359.99
    std::optional<std::string> timestamp;  // "YYYY:MM:DD HH:MM:SS"
};

struct BatchRules {
    std::optional<TimeShift> time_shift;
    std::optional<GpsData>   gps;
    bool gps_clear = false;                // 一键清除 GPS（优先级高于 gps）
    std::vector<TagEdit> exif_edits, xmp_edits;
    bool strip_privacy = false;            // 删全部 EXIF+XMP，保留 ICC 与像素
    bool sync_mtime = false;
    bool is_noop() const;
};

struct MetadataOverride {                  // 单文件例外（三态：继承/覆盖/清除）
    bool ignore_batch = false;             // 整单忽略批量规则
    std::optional<TimeShift> time_shift;
    std::optional<GpsData>   gps;
    bool gps_clear = false;
    std::vector<TagEdit> exif_edits, xmp_edits;
    std::optional<bool> strip_privacy;
};

struct MetadataPlan {
    Exiv2::ExifData exif;
    Exiv2::XmpData  xmp;
    std::vector<Warning> warnings;
    bool has_time = false;
    std::string datetime_original;         // effective 值（mtime 同步用，可空）
};

// effective = 源 ⊕ BatchRules ⊕ 例外（例外逐项覆盖；ignore_batch 先清空批量规则）
MetadataPlan build_plan(const SourceMeta& src, const BatchRules& rules,
                        const std::optional<MetadataOverride>& ex);

// —— 载荷（G1）——
struct Payloads { std::string exif_blob; std::string xmp_rdf; };
// exif_blob = Exiv2::ExifParser::encode(blob, littleEndian, plan.exif)（M0 实测 API）
// xmp_rdf   = XmpParser::initialize() 后 XmpParser::encode(packet, plan.xmp)
Payloads make_payloads(const MetadataPlan& plan);

// —— 写入路径 ——
// 路径 A（JPEG/PNG/TIFF/WebP）：编码完成后 Exiv2 后写；返回空串=成功
// PNG 的 eXIf 若写入失败（R1）→ 关键 EXIF 字段镜像到 XMP + Warning{MetadataDropped}
std::string write_metadata_exiv2(const std::filesystem::path& out_file,
                                 const MetadataPlan& plan, const Payloads& payloads);

// 路径 B/C（HEIF/AVIF/JXL）：编码器内注入，本函数只负责给编码器提供载荷 → 见 §3.8

// 仅元数据模式（同格式零重编码）：JPEG/PNG/TIFF/WebP 走 Exiv2 无损重写
// 返回空串=成功；HEIF/AVIF 由调用方在 UI 层置灰
std::string rewrite_metadata_only(const std::filesystem::path& src,
                                  const std::filesystem::path& out,
                                  const MetadataPlan& plan, const Payloads& payloads);

// mtime 同步：exif_datetime 为空 → 不动；格式 "YYYY:MM:DD HH:MM:SS"（本地时区）
std::string sync_file_mtime(const std::filesystem::path& out, const std::string& exif_datetime);

// —— 可单测纯函数 ——
// 时间偏移：Delta（月/年按日历）与时区语义（改写墙钟 + 写 OffsetTime*）
// EXIF 日期格式 "YYYY:MM:DD HH:MM:SS"；非法 → ok=false 且返回原串
std::string shift_exif_datetime(const std::string& exif_dt, const TimeShift& s, bool& ok);
std::string offset_time_string(int offset_min);            // 480 → "+08:00"
// 时区语义：把"按时区 A 解释的时刻"改写为"时区 B 的墙钟时间"
std::string reinterpret_timezone(const std::string& exif_dt, int from_min, int to_min, bool& ok);

// GPS 有理数编码（G7：正值 Rational + 半球 ref）
// 写入 Exif.GPSInfo.GPSLatitude/GPSLatitudeRef/GPSLongitude/GPSLongitudeRef/GPSVersionID
// 以及可选的 GPSAltitude(Ref)/GPSImgDirection(Ref)/GPSTimeStamp+GPSDateStamp
void write_gps(Exiv2::ExifData& exif, const GpsData& gps);
void clear_gps(Exiv2::ExifData& exif);

// 隐私剥除：清空 exif/xmp（保留 ICC 与像素）
void strip_privacy(Exiv2::ExifData& exif, Exiv2::XmpData& xmp);

// 应用编辑列表（set/remove；key 为 Exiv2 全名，如 Exif.Image.Artist / Xmp.dc.title）
// 无效 key 或类型不符 → 记入 errors（不抛异常）
void apply_edits(Exiv2::ExifData& exif, Exiv2::XmpData& xmp,
                 const std::vector<TagEdit>& exif_edits,
                 const std::vector<TagEdit>& xmp_edits,
                 std::vector<std::string>& errors);

// 读取"拍摄时间"（DateTimeOriginal → DateTime → Xmp.xmp:CreateDate）
std::string effective_datetime(const Exiv2::ExifData& exif, const Exiv2::XmpData& xmp);

}  // namespace pp
```

### 3.8 `src/codecs/encoders.h`（T6/T7 工厂 + 内省）

```cpp
// PP-FROZEN(file)
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "codecs/encoder.h"
#include "core/params.h"

namespace pp {

// format_id ∈ {jpeg,jxl,png,tiff,webp,bmp,heif,avif}；backend_id 可空（用默认后端）
// 不支持 → nullptr
std::unique_ptr<IEncoder> make_encoder(std::string_view format_id, std::string_view backend_id);

// libheif 运行时内省（R4）：返回该格式可用后端及其参数（转成 ParamDef）
// heif → x265 后端；avif → svt-av1/libaom 后端（实际可用者，运行时枚举）
// 非 libheif 格式 → 空
std::vector<BackendDef> introspect_backends(std::string_view format_id);

// 10bit 能力探测（T7 交付）：返回 "8" / "8,10" / "8,10,12"（按编码器实际支持）
std::string probe_bitdepth_support(std::string_view format_id, std::string_view backend_id);

}  // namespace pp
```

**编码器共同契约**（每个 IEncoder 实现都必须满足，验收时逐条检查）：

| # | 契约 |
|---|---|
| E1 | 无状态、可跨线程共享（同一实例被多个 worker 并发调用）；不得持有可变成员 |
| E2 | 内部线程全关：libjxl `JxlEncoderSetParallelRunner(nullptr)`；libheif `heif_encoder_set_parameter(enc,"threads","1")`（或等价）；SVT-AV1/x265 单逻辑核；jpegli 天然串行 |
| E3 | 位深：`out_bitdepth` 不支持的组合 → error（不静默降档）；源位深高于输出 → 调用方已置 Warning，编码器不重复报 |
| E4 | alpha：目标 supports_alpha==false 时调用方已 flatten；编码器收到 4 通道即写 alpha，收到 1/2 通道写灰度（若无灰度能力 → 调用方已转 RGB） |
| E5 | ICC：`meta.icc_profile` 非空则嵌入（jpegli ICC marker / libjxl ICC / libheif nclx+ICC / WebP ICCP chunk / OIIO spec attribute） |
| E6 | 色彩：不得做任何额外像素处理（无抖动、无锐化、无自动白平衡）；float→整型标准舍入 |
| E7 | 元数据：JXL 用 box（Exif/xml，**顺序 UseBoxes → AddBox(4 字节 TIFF offset + blob) → CloseBoxes → CloseInput**）；HEIF/AVIF 用 `heif_context_add_exif_metadata`/`add_XMP_metadata`（必须在 `heif_context_write` 前）；JPEG/WebP/PNG/TIFF/BMP 不写 EXIF/XMP（由 §3.7 后写路径负责），但 ICC 由编码器写 |
| E8 | 失败返回 `EncodeResult`：**`error` 非空且 `bytes==0`**；不得抛异常穿透（内部 try/catch）〔**M1 加性修订（主对话裁定）**：`EncodeResult` 追加 `std::string error;`——由 T7 执行编辑并独立提交；警告列表**不得**用于表达"编码失败"〕 |
| E9 | 参数读取一律 `param_int/float/bool/str`，缺失用表内默认；**未识别参数忽略并记 warning**（不报错） |

**参数映射表（T6/T7 必读；键名与 M0 定稿 78 条参数表一致）**

jpegli（`enc_jpegli.cpp`）：`quality_mode` → distance/quality 二选一；`distance` → `jpegli_set_distance(&c, v, TRUE)`（M0 实测三参数；force_baseline=TRUE 与 cjpegli 一致，实现时核对头文件注释）；`quality` → `jpegli_set_quality`；`chroma` 444/440/422/420 → `comp_info[0].h_samp_factor/v_samp_factor`（1,1 / 1,2 / 2,1 / 2,2）；`progressive` → `jpegli_set_progressive_level(&c, 2/0)`，且 progressive>0 时必须 optimize_coding=true（表内 tooltip 已注明，编码器内直接设 cinfo.optimize_coding=true）；`optimize_coding` → `jpegli_set_optimize_coding`（不存在则 `cinfo.optimize_coding=v`）；`arith_code` → **恒 error**（jpegli 明确不支持：`encode.cc` 内 JPEGLI_ERROR），错误信息固定 `"arith_code is not supported by jpegli"`；`restart_in_rows` → `cinfo.restart_in_rows`；`dct_method` → `cinfo.dct_method`；`smoothing_factor` → `cinfo.smoothing_factor`；`xyb_mode` → `jpegli_set_xyb_mode`（核对）；`adaptive_quantization`/`std_quant_tables`/`psnr_target`/`cicp_transfer_function` → 同名 `jpegli_set_*`（std_quant_tables 为无参开关，M0 实测；cicp 仅 16/18 有意义）。输出恒 8 位；16 位源 → 上层 Warning。

libjxl（`enc_jxl.cpp`）：公共 `effort`→`JXL_ENC_FRAME_SETTING_EFFORT`、`decoding_speed`→`..._DECODING_SPEED`、`codestream_level`→`..._CODESTREAM_LEVEL`；VarDCT/Modular 各自 distance → `JxlEncoderSetFrameDistance`（lossless → `JxlEncoderSetFrameLossless(true)`）；其余参数按 M0 冻结表的 key 与 `JxlEncoderFrameSettingId` 一一对应（表内已核对过语义与范围；三态参数 Bool 化规则见 §7）。容器恒开（`JxlEncoderUseContainer(true)`，元数据需 box）。

libwebp（`enc_webp.cpp`）：`WebPConfigInit` → 应用参数 → `WebPValidateConfig` → `WebPEncode`/`WebPEncodeLossless`；`quality`/`method`/`preset`(WebPConfigPreset)/`sns_strength`/`filter_strength`/`filter_sharpness`/`filter_type`/`autofilter`/`pass`/`segments`/`partitions`/`partition_limit`/`preprocessing`/`qmin`/`qmax`/`emulate_jpeg_size`/`low_memory`/`alpha_*`（有损）；lossless 技术用 `lossless=1` + `exact`/`near_lossless`/`method`；alpha 用 `WebPPictureImportRGBA`；ICC 用 `WebPMuxSetChunk(mux,"ICCP",...)`。

libheif（`enc_heif.cpp`）：`heif_context_alloc` → `heif_context_get_encoder_for_format(ctx, heif_compression_HEVC|AV1)` → 参数经 `heif_encoder_set_parameter_*`（**键名以运行时内省为准**，不得硬编码猜测）→ `heif_image_create` + `heif_image_add_plane`（Y/Cb/Cr，含 alpha 时加 `heif_channel_Alpha`；位深 8/10 由 plane 位深决定）→ `heif_context_add_exif_metadata`/`add_XMP_metadata` → `heif_context_write`。后端选择：`svt-av1`（`heif_encoder_descriptor_get_id_name` 含 "svt"）与 `libaom`（含 "aom"）。**10bit 探测**：若编码器不支持 10bit plane（`heif_image_add_plane` 失败或编码报错）→ error 明确说明，UI 层据此裁剪位深选项。

OIIO（`enc_oiio.cpp`）：`ImageOutput::create(fmt)` + `ImageSpec`（`set_format` 目标位深类型 UINT8/UINT16；PNG 用 `attribute("compressionLevel", v)`；TIFF 用 `attribute("compression", name)`（M0 实测名表：none/lzw/zip/ccittrle/packbits）+ `attribute("tiff:predictor", int)` + `attribute("tiff:zipquality", v)` + tile 走 `spec.tile_width/tile_height`（**必须 16 的倍数且非 0**，否则上层校验拦截）；BMP 无参数）→ `write_image`。ICC → `spec.attribute("ICCProfile", TypeDesc::UINT8, ...)`。

> **T7 中期裁定（主对话，冻结）**：
> ① **10bit 能力（R19 定稿）**：`heif/x265 = "8,10,12"`（T7b `201c8a8` 按上游 multilib 方案恢复多比特深度：三次构建 + `EXTRA_LIB`/`LINKED_*_BIT` 粘合 + `ar -M` 合并）、`avif/svt-av1 = "8,10"`、`avif/libaom = "8,10,12"`。**静态 `FormatDef.bitdepths` 不改**——运行期以 `probe_bitdepth_support()` 与静态表**取交集**作为可选位深：T8 请求不支持位深 → 明确 error（不静默降档）；T10 UI 隐藏不可用项。另：主对话裁决把 heif/avif 的**静态允许集**扩为 `{8,10,12}`（T2b 执行；设计 §2.1 草图本就写 `HEIF{8,10,12}`），**默认值仍为 10**（共识 §3.4 高质量档），交集后 svt-av1 自动收敛为 {8,10}；
> ② **AVIF 10bit + alpha**：`svt-av1` 编码失败（插件错误）、`libaom` 成功 → 保留双后端，**不做自动后端切换**（用户显式选择优先）；错误串必须点名 `libaom`（如 `"svt-av1 does not support 10-bit with alpha; use backend libaom"`）；T10 在"10bit + 源含 alpha"时预选/提示 libaom。
> ③ **`introspect_backends` 形态**：每后端一个合成 `TechDef`（`id="runtime"`、`label=heif_encoder_descriptor_get_name()`、`lossless_capable` 取描述符能力、`params` = 运行时 ParamDef 转换、默认值取 live 值）——**批准**。实测参数数：x265=7、svt=10、aom=15。
> ④ **x265 无线程参数**：libheif 的 x265 插件未暴露 `threads` → E2"编码器内部线程全关"对 HEIF **不可实现**：记 api-delta + `TODO(M2)`，M1 接受 x265 默认线程池。
> ⑤ EXIF 注入用**裸 TIFF blob**（无需 `Exif\0\0` 前缀），必须在 `heif_context_write` 之前——已实测通过（.heic/.avif 均可读回）。

> **T6 落地口径（主对话确认，冻结）**：
> ① `src/codecs/encoders.h` 由 T6 逐字节创建（§3.8 代码块），**后续任务不得重写**（T7 只 include）；
> ② **`EncodeRequest` 追加 `std::string tech_id;`**（M1 第二次加性修订，**T6b 执行**）——此前 JXL 只能靠 `__lossless` + modular 专属键推断技术，既脆又无法表达"有损 modular"；编码器应**优先用 `tech_id`**，`__lossless` 仅作回退。T8 必须传 `cfg.tech_id`。**加性修订一律追加在结构体末尾**（保持位置式聚合初始化兼容；首版误插在第 3 位导致既有 `{buf, params, bitdepth, meta, out, {}}` 编译失败，已纠正）；
> ③ jpegli API 实测：`jpegli_enable_adaptive_quantization(cinfo,bool)`（**非** `set_*`）、`jpegli_use_standard_quant_tables(cinfo)` **无参**、`jpegli_write_icc_profile(cinfo,JOCTET*,uint)`、`jpegli_mem_dest(cinfo,uchar**,ulong*)`；**顺序约束**：`xyb_mode`/`cicp_transfer_function`/`std_quant_tables` 必须在 `jpegli_set_defaults` **之前**，`distance` 在其**之后**；无 `jpegli_set_optimize_coding` → 直接写 `cinfo.optimize_coding`；
> ④ libjxl：`codestream_level` 是**编码器级** `JxlEncoderSetCodestreamLevel(enc,int)`（非 frame setting，须在编码开始前）；`JxlExtraChannelInfo` 字段名 `alpha_premultiplied`（非 `alpha_associated`）；三态参数的"库默认" = **不调用该 setting**（显式传 −1 会被部分 setting 直接拒绝，实测 `channel_colors_global_percent`）；
> ⑤ **OIIO 的 WebP 读取对含 alpha 文件返回预乘 RGB**（实测文件内 `(4,0,142,253)` → OIIO 读出 `(4,0,141,253)`）→ WebP alpha 的**逐位断言必须走 libwebp 解码**，OIIO 只用于几何/平面断言（**T8 金样必读**）；
> ⑥ E9"未识别参数"用 `log_warn` 记录，**不扩展 `WarningKind`**：warnings 的语义是"影响输出的逐图像质量/语义偏差"，未知参数属**配置缺陷**（应由 T10 预设校验 + 运行日志暴露，M2 可加 preset 键校验）。

### 3.9 `src/core/pipeline.h`（T8）

```cpp
// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "codecs/encoder.h"
#include "core/colormanager.h"
#include "core/fsops.h"
#include "core/metadata.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "core/types.h"

namespace pp {

struct FileEntry {
    std::filesystem::path src;
    std::filesystem::path base_dir;         // 镜像路径基准
    std::optional<MetadataOverride> exception;
    ImageInfo info;                          // probe 后回填
    bool probe_done = false;
};

struct RunConfig {
    // 输出
    std::filesystem::path out_root;
    std::string format_id, backend_id, tech_id;
    bool lossless = false;
    ParamSet params;
    int out_bitdepth = 8;
    ColorTarget color_target = ColorTarget::KeepOriginal;
    ConflictPolicy conflict = ConflictPolicy::Rename;
    bool rotate_orientation = true;
    double flatten_gray = 1.0;               // alpha 合成底色（0=黑..1=白）
    // 元数据
    BatchRules rules;
    bool metadata_only = false;
    // 运行
    int workers = 0;                         // 0=物理核数
    uint64_t budget_bytes = 0;               // 0=default_capacity_bytes()
};

struct FileResult {
    std::filesystem::path src, out;
    ImageInfo info;
    Timing t;
    uint64_t out_bytes = 0;
    std::vector<Warning> warnings;
    std::string error;
    std::string color_src, color_dst;
    bool ok = false, skipped = false, cancelled = false;
};

enum class FileState { Queued, Probing, Decoding, Orienting, Coloring, Flattening,
                       Encoding, Writing, Done, Skipped, Failed, Cancelled };

struct FileEvent {
    std::size_t index = 0;
    FileState state = FileState::Queued;
    const FileResult* result = nullptr;      // 仅在终态（Done/Skipped/Failed/Cancelled）非空
};

// 单文件全流程（线程内串行）；budget 可为 nullptr（仅元数据模式不需要）
// reserved 为本批次已分配输出路径（批内冲突）；返回值带终态
FileResult run_one_file(FileEntry& fe, const RunConfig& cfg, IEncoder* enc,
                        PixelBudget* budget, const std::vector<std::filesystem::path>& reserved,
                        const std::function<bool()>& cancelled,
                        const std::function<void(FileState)>& on_stage);

// 仅元数据模式（零重编码）：仅 JPEG/PNG/TIFF/WebP；HEIF/AVIF/JXL 由上层拒绝
FileResult run_metadata_only(FileEntry& fe, const RunConfig& cfg,
                             const std::vector<std::filesystem::path>& reserved,
                             const std::function<bool()>& cancelled,
                             const std::function<void(FileState)>& on_stage);

// 输入白名单校验（唯一入口，UI 与 harness 共用）
bool format_supports_metadata_only(std::string_view format_id);

}  // namespace pp
```

### 3.10 `src/core/scheduler.h`（T8；**由 T8 创建**——T2 仅落地了 pipeline.h，`scheduler.h` 需按本节逐字节创建）

```cpp
// PP-FROZEN(file)
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include "core/pipeline.h"

namespace pp {

struct RunSummary {
    std::size_t total = 0, ok = 0, failed = 0, skipped = 0, cancelled = 0;
    uint64_t out_bytes = 0;
    double total_ms = 0, throughput_mb_s = 0, avg_file_ms = 0;
};

class Scheduler {
public:
    using EventCb = std::function<void(const FileEvent&)>;   // 任意线程调用；UI 负责 queued 转发

    Scheduler(RunConfig cfg, std::vector<FileEntry> files);
    ~Scheduler();

    void set_event_callback(EventCb cb);
    void start();          // 非阻塞；创建 worker 池（N=cfg.workers 或物理核数）
    void cancel();         // 置取消标志；已在编码中的文件跑完
    void wait();           // 阻塞至全部结束
    bool running() const;

    const std::vector<FileResult>& results() const;   // wait() 后有效（按输入顺序）
    RunSummary summary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp
```

**调度语义（冻结）**：worker 从队列按**输入顺序**取件（G14）；`probe → budget.acquire(2×frame) → run_one_file → release`；probe 失败 → 直接 Failed 事件；取消后剩余文件标 Cancelled 并逐个发事件；`results()` 与输入等长（未跑完的是 Cancelled 状态）。

> **T8 落地口径（主对话批准）**：预算的 acquire/release **由 `run_one_file` 内部 RAII 持有**（在 probe 之后、decode 之前取，析构时释放），而非字面写在本类里——语义等价且避免双重扣额/异常泄漏；映射说明见 `scheduler.cpp` 头部注释。probe 失败与仅元数据模式不取预算。

### 3.11 `src/core/presets.h`（T2）

```cpp
// PP-FROZEN(file)
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "core/metadata.h"
#include "core/pipeline.h"

namespace pp {

struct PresetData {
    int version = 1;
    std::string name;
    std::string format_id, backend_id, tech_id;
    bool lossless = false;
    int out_bitdepth = 8;
    ColorTarget color_target = ColorTarget::KeepOriginal;
    ConflictPolicy conflict = ConflictPolicy::Rename;
    ParamSet params;
    BatchRules rules;
};

// 校验：格式/后端/技术存在、参数合法、位深在该格式集合内、仅元数据模式格式受限
// 返回空串=通过
std::string validate_preset(const PresetData& p);

// 用当前格式表补齐缺失参数（返回补齐的 key 列表）
std::vector<std::string> normalize_preset(PresetData& p);

}  // namespace pp
```

### 3.12 `src/ui/preset_io.h`（T2，QtCore-only；实现编入 pp_core，见 §2.3）
> 说明：本文件是唯一允许进入 `pp_core` 的 Qt 适配层（JSON 读写），使 UI、`--dev` harness、单元测试三处复用同一份预设 I/O；`src/core/` 目录本身仍零 Qt。

```cpp
// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <string>
#include "core/presets.h"

namespace pp::ui {

// JSON 读写（QJsonDocument）；返回空串=成功
std::string save_preset(const std::filesystem::path& file, const PresetData& p);
std::string load_preset(const std::filesystem::path& file, PresetData& out);

// 目录扫描：*.json，返回按名排序的 (path, name) 列表
std::vector<std::pair<std::filesystem::path, std::string>>
list_presets(const std::filesystem::path& dir);

}  // namespace pp::ui
```

**Preset JSON schema（冻结）**：
```json
{ "version": 1, "name": "Web 高质量",
  "format": "jxl", "backend": "libjxl", "tech": "vardct", "lossless": false,
  "bitdepth": 8, "color_target": "srgb", "conflict": "rename",
  "params": { "distance": 1.0, "effort": 7 },
  "rules": {
    "time_shift": { "mode": "delta", "years":0,"months":0,"days":0,"hours":0,"minutes":0,"seconds":0 },
    "gps": { "lat": 31.2304, "lon": 121.4737, "altitude": null, "direction": null, "timestamp": null },
    "gps_clear": false,
    "exif_edits": [ { "key": "Exif.Image.Artist", "value": "Zhang" } ],
    "xmp_edits":  [ { "key": "Xmp.dc.title", "value": "demo" } ],
    "strip_privacy": false, "sync_mtime": false } }
```

### 3.13 `src/platform/paths.h`（T12）

```cpp
// PP-FROZEN(file)
#pragma once
#include <filesystem>

namespace pp::platform {

// exe 目录可写 → 便携模式：<exe>/settings.ini、<exe>/presets/、<exe>/logs/
// 只读 → 回退：Linux $XDG_DATA_HOME/PhotoPipeline（默认 ~/.local/share/PhotoPipeline）
//         Windows %APPDATA%/PhotoPipeline
// 结果缓存；返回值为已确保存在的目录
std::filesystem::path data_dir();
std::filesystem::path settings_file();     // data_dir()/settings.ini
std::filesystem::path presets_dir();       // data_dir()/presets
std::filesystem::path logs_dir();          // data_dir()/logs
std::filesystem::path executable_dir();

}  // namespace pp::platform
```

### 3.14 `src/core/settings.h`（T12；core 无 Qt → 自带极简 INI 读写）

```cpp
// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace pp {

struct AppSettings {
    int workers = 0;                 // 0=物理核数
    int budget_gb = 0;               // 0=自动 min(RAM×50%, 8GB)
    double flatten_gray = 1.0;       // alpha 合成底色
    std::string log_level = "info";
    std::string map_provider = "osm";    // "osm" | "amap"
    std::string amap_key;
    int tile_cache_mb = 64;
    bool rotate_orientation = true;
    // 记住上次会话（共识 §3.9）
    std::string last_format = "jxl";
    std::string last_preset;             // 预设文件路径（可空）
    std::string last_out_root;           // 上次输出根目录（可空）
};

// 极简 INI：`key=value` 单层，`#` 注释；未知键保留原样写回（向前兼容）
AppSettings load_settings(const std::filesystem::path& file);
std::string save_settings(const std::filesystem::path& file, const AppSettings& s);
std::string settings_to_string(const AppSettings& s);   // 日志快照用

}  // namespace pp
```

### 3.15 dev harness CLI（T8；冻结）

```
photopipeline --dev <input...> --out <dir> [options]
  --out DIR              输出根目录（必填）
  --format ID            jpeg|jxl|png|tiff|webp|bmp|heif|avif（默认 jxl）
  --backend ID           后端（avif: svt-av1|libaom；默认空=首选）
  --tech ID              技术（jxl: vardct|modular；webp: lossy|lossless；默认空=首选）
  --lossless             无损开关
  --bitdepth N           输出位深；默认=高质量档（共识 §3.4）：jpeg 8 / jxl 16 / png 16 / tiff 16 / webp 8 / bmp 24 / heif 10 / avif 10（10bit 若 T7 探测不支持 → 该格式默认回退 8 并记 warning）
  --color TARGET         keep|srgb|p3|adobergb（默认 keep）
  --conflict POLICY      skip|overwrite|rename（dev 默认 overwrite，保证幂等重跑）
  --metadata-only        仅元数据模式
  --preset FILE          载入预设 JSON（命令行选项优先）
  --param KEY=VALUE      覆盖单个参数（可多次；KEY 为参数表 key，VALUE 按该参数类型解析）
  --meta KEY=VALUE       写入/覆盖元数据标签（可多次；KEY 为 Exiv2 全名，如 Exif.Image.Artist；等价于批量规则的一条 exif_edits 项）
  --workers N            并发 worker 数（默认物理核数；dev 用 1 便于日志对照）
  --base DIR             镜像路径基准目录（可多次；默认各输入文件所在目录）
  --log-level LVL        trace|debug|info|warn|error
退出码 = 失败文件数（0=全部成功）；stdout 末尾打印汇总表（成功/失败/跳过/取消、总耗时、吞吐 MB/s）
```

> **构建开关与鼓点（主对话裁定，T8 执行）**：`--dev` 分支由 `PP_BUILD_DEV` 宏保护（设计 §8.6：**发布构建不含该代码路径**）。M0 遗留缺口：`option(PP_BUILD_DEV …)` 存在但**无任何 target 消费**（`grep -rn PP_BUILD_DEV` 只命中 CMakePresets 与 option 行），故必须补接线——在 §2.3 允许的 CMake 改动内加入：
> ```cmake
> if(PP_BUILD_DEV)
>   target_compile_definitions(photopipeline PRIVATE PP_BUILD_DEV)
> endif()
> ```
> **鼓点 / 27×8 矩阵 / 规模验证**一律用 `cmake --preset release -B build/m1-t8 -DPP_BUILD_DEV=ON`（优化 + 含 harness）；**T14 另需验证不带该覆盖的纯 release 构建中 `--dev` 不可用**（证发布路径确实不含 dev 代码）。

### 3.16 `tools/pp_verify`（T8；冻结）

```
pp_verify <expected.json> <actual_output> [--selftest]
  按 tests/golden/SCHEMA.md 断言：pixel.mode(exact|psnr+threshold_db)、metadata[]、warnings_contain[]
  --selftest：内置自检（对自身合成的内存样本做 exact/psnr/metadata 三态断言），无需语料
输出：`VERIFY <case> OK|FAIL <detail>`；退出码 = FAIL 数
```

### 3.17 `src/codecs/encoder_registry.h`（内部头，T6 创建；T7 只使用，不得修改）

> 目的：消除 T6/T7 对 `encoders.cpp` 的并行写冲突——**每个编码器在自己的 .cpp 里静态自注册**，工厂只查表。

```cpp
// PP-FROZEN(interface): 内部注册表（T6 创建；T7 只 include + 用宏，不得改动本文件）
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "codecs/encoder.h"

namespace pp {

using EncoderFactory = std::unique_ptr<IEncoder> (*)();

// 注册；同一 (format, backend) 重复注册 → 返回 false 且不覆盖先注册者
bool register_encoder(std::string_view format_id, std::string_view backend_id, EncoderFactory f);

// backend_id 为空 → 返回该格式首个注册项；未注册 → nullptr
std::unique_ptr<IEncoder> create_registered_encoder(std::string_view format_id,
                                                    std::string_view backend_id);
std::vector<std::string> registered_backends(std::string_view format_id);

// 静态注册助手（.cpp 文件作用域使用；fn 形如 std::unique_ptr<IEncoder> make_xxx()）
#define PP_REGISTER_ENCODER(format_id, backend_id, fn) \
    namespace { const bool pp_reg_##fn = ::pp::register_encoder((format_id), (backend_id), &(fn)); }

}  // namespace pp
```

**文件归属（冻结，避免并行冲突）**：T6 创建 `encoder_registry.h` + `encoders.cpp`（`make_encoder` = 查表）+ `enc_jpegli.cpp`/`enc_jxl.cpp`/`enc_webp.cpp`；T7 创建 `enc_heif.cpp`/`enc_oiio.cpp`（各自用宏注册）并**独家实现** `introspect_backends` / `probe_bitdepth_support`（定义在 `enc_heif.cpp`）。**T7 不得编辑 T6 的任何文件**；若 T7 编译时 `encoder_registry.h` 尚不存在 → 等待（每 60s 重试，上限 25 分钟），先写实现。

**静态库链接约束（T7 实测，T7c 修复——对 T8/T10 至关重要）**：`pp_core` 是 STATIC 档案，链接器**按需选取档案成员** → 自注册 TU 若不参与链接，其静态初始化不执行，`make_encoder()` 对所有格式返回 **nullptr**（实测：只有被符号直接引用的 TU 被拉入，如 `enc_heif.cpp` 因 `introspect_backends` 被引用而侥幸进链接）。**修复 = 所有消费 `pp_core` 的可执行目标使用 `$<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>`**（CMake ≥3.24）；不要用"每文件加锚点符号"的替代方案（T6 已按 T7 建议预留锚点，最终以 whole-archive 为准，锚点可留作冗余但不得作为唯一机制）。

---

## 4. 各任务详细规格

### 4.1 T1 core 基础设施（logger / fsops / pixelbudget）

- **文件**：`src/core/logger.{h,cpp}`、`src/core/fsops.{h,cpp}`、`src/core/pixelbudget.{h,cpp}`、`tests/unit/test_fsops.cpp`、`tests/unit/test_pixelbudget.cpp`、`tests/unit/test_logger.cpp`
- **实现要点**：spdlog 用 `basic_file_sink`（非 rotating，自己管 20 份保留：列目录按 mtime 排序删旧）；`log_write` 格式化 `k=v`（value 含空格 → `"..."`）；logger 未初始化时写 stderr 不崩。
- **单测**：mirror_path 5 例（嵌套/非同根/换扩展/空扩展/unicode 路径）；resolve_conflict 4 例（skip/overwrite/rename 序列/批内 reserved 冲突）；is_inside 3 例；collect_inputs 用 `tests/golden` 递归（预期 27 个 + 扩展名过滤）；pixelbudget（容量/超限立即 false/多线程 acquire-release 不超发/取消返回 false/peak 正确）；logger（级别过滤、PP_LOG_LEVEL 覆盖、20 份保留、k=v 格式正则）。
- **验收**：`ctest -R "fsops|pixelbudget|logger"` 全绿；`--dev` 鼓点从 T8 起。

### 4.2 T2 参数引擎 + 预设

- **文件**：`src/core/params.cpp`（新增实现，不改头）、`src/core/presets.{h,cpp}`、`src/ui/preset_io.{h,cpp}`、`tests/unit/test_params.cpp`、`tests/unit/test_presets.cpp`；**只允许**在 `src/core/format_tables.cpp` 中填写 `visible`/`locked` 字段。
- **谓词规则（冻结）**：
  - JPEG：`distance` visible ⇔ `quality_mode=="distance"`；`quality` visible ⇔ `=="quality"`；`progressive>0` ⇒ `optimize_coding` locked=true。
  - JXL：`lossless==true` ⇒ vardct 组整体不可选（技术上切 modular，由 UI 展示）；modular：`distance` locked=0.0；`modular_lossy_palette` locked=false；vardct：`photon_noise`/`epf` visible ⇔ !lossless。
  - WebP lossless：`exact` 可见；lossy：`alpha_*` 仅当源含 alpha（谓词只看参数集，源相关判断放 UI，故此处不设）。
  - TIFF：`deflate_level` visible ⇔ `compression=="zip"`；`predictor` visible ⇔ compression∈{lzw,zip}；tile 两参数 >0 时必须 16 的倍数（validate_params 内校验）。
  - HEIF/AVIF：内省参数交由 `introspect_backends` 生成（T7），**本任务不填**。
- **单测**：默认值全集与表一致；apply_locks 在 lossless 下锁定 distance=0/modular_lossy_palette=false；validate_params 捕获越界/非法枚举/类型不符/TIFF tile 非 16 倍数；param_* 取值 fallback；find_* 命中与未命中；fill_defaults 补全缺失 key；snapshot_params 字典序稳定；preset 往返（save→load→字段全等，含 unicode name 与 rules 各字段）；list_presets 排序。
- **验收**：`ctest -R "params|presets"` 全绿。

> **T2 落地口径（主对话确认，冻结）**：
> ① `param_float` 与 Float 校验**接受 int64**（JSON 整数→浮点参数；Int/Bool/String 仍严格，double 不给 param_int、不静默截断）；
> ② `validate_preset` 增加 `version == 1` 检查（≠1 → 报错，为 M2 迁移预留）；
> ③ `validate_params` 对 `runtime_introspected && techs.empty()`（heif/avif）跳过校验（参数由 T7 运行时内省生成）；
> ④ TIFF tile 附加约束：`tiff_tile_width`/`tiff_tile_height` 必须**同时 >0 或同时为 0**（且 >0 时为 16 的倍数）；
> ⑤ `find_backend("")`/`find_tech("")` → 返回首个（"默认空=首选"）；空 tech_id 且 `lossless==true` → 选首个 `lossless_capable` 技术（webp→lossless、jxl→modular）；
> ⑥ `validate_params` **不**校验锁定值（锁定由 `apply_locks`/`normalize_preset` 强制）；`snapshot_params` 排除 `__` 前缀键；
> ⑦ 仅元数据模式的格式限制**不在** `PresetData`（冻结结构无该字段）——由 `format_supports_metadata_only()`（pipeline.h）+ UI 置灰承担。

### 4.3 T3 解码层

- **文件**：`src/decode/oiio_reader.{h,cpp}`、`tests/unit/test_decode.cpp`
- **要点**：`probe_file` 只 `ImageInput::open` + spec（不读像素）；`decode_float` 用 `ImageBuf::read(0,0,0,0, TypeFloat)`；通道 2（灰+α）保持；>4 通道取前 4 + warning；CMYK tiff（`spec.nchannels==4 && spec.get_string_attribute("tiff:photometric")=="separated"` 或 OIIO 报为 4 通道非 RGBA）→ error `"CMYK input is not supported"`；多页取首页 + Warning。
- **单测**：对语料 27 文件逐个 probe（含 2 个损坏文件预期 error、CMYK 预期 error、multi.tif 预期 is_multipage、anim.gif 预期 is_multipage）；decode_float 对 base/rgb8.png、graya8.png、rgba16.png 校验通道数与 `TypeFloat`；ICC 提取对 meta/exif_full.jpg（无 ICC → 空）与任一 P3/带 ICC 文件（若无则跳过并报告）。
- **鼓点**：`--dev` 未就绪前 = 单测全绿 + `pp_verify --selftest` 无回归。

### 4.4 T4 色彩层

- **文件**：`src/core/colormanager.{h,cpp}`、`assets/icc/DisplayP3.icc`、`assets/icc/AdobeRGB1998.icc`（**来源须在文件头注释或 `assets/icc/README.md` 注明可再分发来源**）、`tests/unit/test_color.cpp`
- **要点**：变换仅作用于颜色通道（构造 3 平面临时缓冲）；GRAY(1ch)→RGB 用 `cmsCreateTransform(gray_profile, TYPE_GRAY_FLT, dst, TYPE_RGB_FLT, ...)` 单调用；sRGB→sRGB 走同一个 transform（保数值一致）；缓存 key = (源 ICC 的 SHA1, target, in_channels)；缓存 LRU 16。
- **ICC 嵌入策略**（共识 §3.5"始终嵌入"由设计 §4 细化，以实现为准）：转换时嵌目标 ICC；保持原样且源有 ICC → 原样拷贝；源无 ICC → 不嵌（不无谓增大文件）+ 日志 info。
- **单测**：sRGB→sRGB float 恒等 ≤1e-5；sRGB 白→P3/AdobeRGB 数值单调合理（R≈G≈B 且 >0.9）；sRGB→Lab D50 金值（L≈100）；灰度→sRGB 升维值正确（灰 0.5 → RGB 三通道相等）；alpha 保持不变（4 通道输入 alpha 逐字节相等）；缓存命中（同参数两次调用 cache_size 不增）；无 ICC 时产生 Warning{NoIccAssumeSrgb}。
- **验收**：`ctest -R color` 全绿。

### 4.5 T5 元数据层（最大模块）

- **文件**：`src/core/metadata.{h,cpp}`、`tests/unit/test_metadata.cpp`、`tests/unit/test_timeshift.cpp`
- **要点**：
  - `XmpParser::initialize()` 只在首次调用（静态 once）+ `XmpParser::terminate()` 在退出（用静态析构）。
  - 时间偏移 Delta：按日历加（月/年溢出 clamp：1/31 +1 月 → 2/28 或 2/29）；时区语义：值视为 from 时区 → 换算到 to 时区墙钟，同时写 `Exif.Photo.OffsetTime/OffsetTimeOriginal/OffsetTimeDigitized`。
  - `effective_datetime` 优先级：`Exif.Photo.DateTimeOriginal` → `Exif.Image.DateTime` → `Xmp.xmp:CreateDate`。
  - 作用字段（时间偏移，冻结）：上述三个 EXIF 时间字段（存在才改）+ `Xmp.xmp:CreateDate`/`xmp:ModifyDate`（存在才改）；某文件无任何时间字段 → Warning{TimeFieldMissing} 且跳过。
  - 隐私剥除优先级最高（先于编辑/GPS 应用；剥除后不再写任何 EXIF/XMP 字段）。
  - GPS 编码：`GPSLatitude` = 3×Rational(deg, min, sec×1000?) — 约定：度/分/秒三 Rational，符号由 Ref 表达（G7）；`GPSVersionID` = 2 2 0 0；海拔 `GPSAltitude` Rational + `GPSAltitudeRef`（0=海平面以上）；方位 `GPSImgDirection` Rational + Ref "T"。
  - PNG eXIf（R1 验证点）：`write_metadata_exiv2` 对 PNG 输出后读回验证；若 EXIF 未落盘 → 关键字段（DateTimeOriginal/Artist/GPS 三项）镜像到 XMP + Warning{MetadataDropped, detail="png exif mirrored to xmp"}。
  - 仅元数据模式：`Exiv2::ImageFactory::open(src)` → 应用 plan → `writeMetadata()` 到 out；**验证压缩数据不变**（与 Spike F 同法：SOS 后字节比对 + 像素 hash）。
  - **MakerNote：原样字节保留、不解析不修改**（共识 §3.6）；跨容器拷贝（如 TIFF→JPEG）由 Exiv2 处理时字节不动，仅在日志 info 记录"Makernote present, N bytes"。
- **单测**：时间偏移 12 例（含跨月/跨年/闰年/负偏移/非法串）；时区语义 4 例（+08→+09 等 + OffsetTime 字符串）；GPS 写入读回（经纬度 DMS 换算误差 <1e-6 度）；隐私剥除后 exif/xmp 为空；编辑 set/del/非法 key；build_plan 合成矩阵（规则⊕例外三态：继承/覆盖/清除）；strip → has_time=false；payloads 非空且 EXIF blob 以 "II*\0" 开头；仅元数据模式往返（压缩尾字节一致）。
- **验收**：`ctest -R "metadata|timeshift"` 全绿。

> **T5 落地口径（主对话确认，冻结）**：
> ① **PNG 元数据（R1 已闭合，T5b 修复 `6e98ca7`）**：真因是 exiv2 未编译 zlib；`vcpkg.json` 的 exiv2 features 加 `png`（port 自带 `png → zlib` feature，portfile:15 映射 `EXIV2_ENABLE_PNG`；实测 `exv_conf.h:52` 由 `/* #undef EXV_HAVE_LIBZ */` 变为 `#define EXV_HAVE_LIBZ`）→ PNG 已注册为图像类型（探针：写 `Exif.Image.Artist` 后读回一致，文件 461→473 B）。**故 PNG 现在走正常 Exiv2 后写路径**；降级分支（`Warning{MetadataDropped, "png metadata dropped..."}` + 关键字段镜像 XMP）保留作兜底，仅在能力缺失时可达。`format_supports_metadata_only("png")` 应为 **true**（T5c 复核）；exiv2 静态闭包新增 `-lz`，且其 CMake config 自带 `find_dependency(ZLIB)` → 下游无需改 CMake；
> ② XMP key 归一化：`XmpKey` 只接受**点号**形式（`Xmp.xmp.CreateDate`），冒号形式抛 `Invalid key` → 实现须归一化（两种写法都可输入）；
> ③ `Exifdatum::setValue` 类型不符**不抛异常**（返回非 0 且清空值）→ 必须检查返回码，失败恢复旧值并记 `errors`；
> ④ TIFF `clearExifData()` 仍保留 12 个结构标签（否则文件损坏）→ "剥除后 exif/xmp 为空"的断言只在 JPEG/WebP 上成立；
> ⑤ WebP 重写会新增必需的 `VP8X` chunk，`VP8L` 载荷字节完全一致（元数据模式 WebP 的保真依据）；
> ⑥ 时区语义模式下，XMP 日期若**自带显式偏移**则以该偏移为准（无偏移才用 `from_offset_min`），改写墙钟并把偏移写为 `to_offset_min`；Delta 模式保留小数秒与时区后缀不变；
> ⑦ `write_metadata_exiv2` / `rewrite_metadata_only` 的 `plan` 参数须传**非 const 对象**（warnings 经 `plan.warnings` 回传；冻结签名所致，已记 TODO(M2)）；
> ⑧ 实测通过项：仅元数据模式 JPEG（SOS 后字节一致 + 像素 hash 相等）/ WebP（VP8L 字节一致）/ TIFF（像素 hash 相等）/ **PNG（IDAT 拼接逐字节一致 + 像素 hash 相等，T5c 新增）**；BMFF（HEIF/AVIF/JXL）写入抛 "not supported"（印证 §5.2 路径 B/C）。
> ⑨ **PNG 正常路径现在零 warning**（`MetadataDropped` 仅在能力缺失/异常降级时出现）——T8/T14 的出口准则 7 的 PNG 例可直接断言 IDAT + 像素 hash。

### 4.6 T6 编码器（jpegli / libjxl / libwebp）

- **文件**：`src/codecs/enc_jpegli.cpp`、`src/codecs/enc_jxl.cpp`、`src/codecs/enc_webp.cpp`、`src/codecs/encoders.cpp`（工厂，先只注册这三个）、`tests/unit/test_enc_smoke.cpp`
- **要点**：严格按 §3.8 的共同契约与参数映射表；E2 线程全关；E7 元数据（JXL box 顺序）；E9 未识别参数 warning（`log_warn`，不进 `EncodeResult.warnings`）；**技术选择优先读 `EncodeRequest::tech_id`**（T6b 起可用，`__lossless` 仅回退）。
- **单测**（用合成 ImageBuf，不经文件）：RGB float 64×64 → JPEG/JXL/WebP 编码成功且字节数 >0；JXL 无损（lossless=true, modular）→ 回读逐位相等（用 OIIO 读回比对，容差 0）；JPEG distance=1.0 → 回读 PSNR ≥ 35dB；WebP lossless → 逐位相等；4 通道（alpha）→ JXL/WebP 保留 alpha；gray 1 通道 → JPEG 成功、WebP 走 RGB 转换由上层负责（此处传 3 通道）；arith_code=true → JPEG 返回错误串包含 "arith_code"；未识别参数 key → 仍成功且有 warning。
- **验收**：`ctest -R enc_smoke` 全绿。

### 4.7 T7 编码器（HEIF/AVIF + OIIO 三格式 + 10bit 探测）

- **文件**：`src/codecs/enc_heif.cpp`、`src/codecs/enc_oiio.cpp`、`src/codecs/encoders.cpp` 追加注册、`tests/unit/test_enc_oiio.cpp`
- **要点**：
  - HEIF/AVIF 参数经 `introspect_backends` 生成，**编码器内部也用同一内省结果把 ParamSet 映射为 `heif_encoder_set_parameter_*` 调用**（键名不硬编码）。
  - 10bit：`probe_bitdepth_support("heif"|"avif", backend)` 实测（对 10bit plane 编码一次 64×64 内存图并回读）；结果写日志；若仅 8bit → 返回 "8" 并把 Warning 策略交给上层。
  - OIIO：PNG/TIFF/BMP 三格式；TIFF tile 参数校验（16 倍数、非 0，非法 → error）；compression 名映射（`zip` 而非 `deflate`）。
- **单测**：PNG/TIFF(uint8/uint16)/BMP 编码 + OIIO 回读尺寸通道位深正确；TIFF lzw/zip/none 三种；TIFF tile 128×128 生效（回读 spec.tile_width==128）；非法 tile（100）→ error；HEIF/AVIF 各编码 1 张（8bit 与可用时的 10bit），Exiv2 读回 DateTimeOriginal（需先注入 EXIF）；内省参数数量 >0 且含 quality 键。
- **验收**：`ctest -R enc_oiio` + 上述 HEIF/AVIF 用例全绿；报告 10bit 探测结论（R19 登记）。

### 4.8 T8 pipeline + scheduler + harness + 首次端到端鼓点

- **文件**：`src/core/pipeline.{h,cpp}`、`src/core/scheduler.{h,cpp}`、`tools/pp_verify.cpp`、`tests/unit/test_pipeline_contract.cpp`、`tests/unit/test_scheduler_contract.cpp`、`tests/golden/smoke/*.json`（8 对断言）、`tests/golden/smoke.sh`
- **要点**：按 §3.9/§3.10 冻结语义；`--dev` 按 §3.15；`pp_verify` 按 §3.16；`main.cpp` 增加 `--dev` 分支（`PP_BUILD_DEV` 保护）；**构造有效参数集时注入保留键 `__lossless = cfg.lossless`**（§3.4 保留键约定）；日志中 lossless 写独立字段、参数快照不含 `__` 键；**并在 `main()` 中调用 `pp::set_qt_version_string(qVersion())`**（§3.1 落地修订，否则运行头日志缺 `qt` 项）。
- **灰度 / ICC 编排规则（主对话冻结，T8 必读）**：
  - `src_is_gray = channels ∈ {1,2}`；
  - 若 `src_is_gray && !format.supports_gray` → 追加 `Warning{GrayToRgbEncoded}`，**且当 `color_target == KeepOriginal` 时以 `SRGB` 作为有效目标调用 `ColorManager::transform`**（灰度像素不能进 WebP/HEIF/AVIF）；
  - `format.supports_gray && color_target == KeepOriginal` → **不调用** `transform`（原生灰度保留，不产 warning）；
  - 嵌入用 ICC 一律取 `ColorOutcome::icc_to_embed`（三个彩色目标均非空；KeepOriginal 且源有 ICC = 源 ICC；源无 ICC = 空 → 不嵌）；
  - `alpha` 合成（flatten）判定用 `§3.5 落地修订 ⑥` 的 `has_alpha` 语义；目标 `supports_alpha == false` 且有 alpha → 合成底色 + `Warning{AlphaFlattened}`；
  - 源色彩描述回退链（R13 尾，T3 已给事实）：嵌入 ICC → OIIO `CICP`/`oiio:ColorSpace`（**M1 仅日志记录，不做 CICP⇄lcms2 映射**）→ 假定 sRGB（Warning）。
- **元数据集成约定（T5 交付，T8 必读）**：
  - `write_metadata_exiv2(out, plan, payloads)` 传**非 const** `plan`；调用后把 `plan.warnings` 并入 `FileResult.warnings`；元数据写失败**非致命**（返回空串 + `Warning{MetadataDropped}`）；
  - BMP 输出的 `Warning{MetadataDropped}` 由 **pipeline** 添加（T5 静默跳过，避免重复）；
  - PNG 输入：`read_metadata` 在本构建必返非空 `error`（**不致命**）→ 继续用空元数据走流程，ICC/Orientation 从 T3 的 OIIO spec 取（待 T5b 修复后此条自动失效）；
  - JXL/HEIF/AVIF 注入用 `make_payloads`（`exif_blob` = TIFF blob，JXL 自行加 4 字节 offset 头，§3.8 E7）；
  - `sync_file_mtime(out, plan.datetime_original)`（空串 = 不动，本地时区解释）；
  - **`format_supports_metadata_only()` 判定**：JPEG/TIFF/WebP = true；**PNG = true**（T5b 已修复 zlib/PNG，T5c 复核）；JXL/HEIF/AVIF = false（JXL 的 box 替换路径**M1 不实现**，记 TODO(M2)——设计 §5.5 列为支持，属 M1 有意收窄）。
  - **能力探测与符号安排（T5c 交付，T8 注意）**：判定由运行期探测函数 `pp::detail_metadata_only_supported(format_id)` 给出（用**结构完整的最小头部样本**探测 Exiv2 类型注册：PNG 需完整 IHDR+CRC、TIFF 需 1 个 IFD 条目、WebP 需 VP8X chunk、JPEG 需 SOI/APP0——仅魔数字节会返回 `none`）；该函数同时以 **weak 定义**补齐冻结谓词 `pp::format_supports_metadata_only()`，**T8 在 pipeline.cpp 给出强定义即自动覆盖**（T8 仍是所有者；建议直接 `return pp::detail_metadata_only_supported(id);`）。实测能力矩阵：`jpeg=1 png=1 tiff=1 webp=1 heif=0 avif=0 jxl=0 bmp=0`。
- **首次端到端鼓点（M1 第一个大关口）**：
  1. `--dev tests/golden/base/*.png --out .cache/out --format jxl --workers 1` 全绿；
  2. 27 fixture × 8 格式矩阵跑完（`.cache/out/<fmt>/`），**零崩溃**，逐格式统计成功/失败（损坏负例与 CMYK 预期失败）；
  3. 8 个 smoke 断言 `pp_verify` 全 OK；
  4. 日志含运行头版本 + 每文件分段耗时 + 汇总。
  5. **规模验证（共识 §3.8：10–1000 张/批、≤50MP）**：8000×6000（48MP）合成图跑一遍（验证像素预算 acquire/release 与 2× 峰值路径，`--workers 2`）；把 `base/rgb8.png` 复制 100 份到 `.cache/tmp/batch100/` 并发跑完，计数与输出数一致。
- **验收**：上述 4 条全绿 + `ctest` 全绿（含 verify_selftest）。

### 4.9 T9–T13 UI（**下一批次**，五任务并行；本批次不实施）

**共同纪律**：UI 只用成熟模式（`QStackedWidget` / `QAbstractListModel` + `QSortFilterProxyModel` / `QDialog` / `QFormLayout`）；**UI 不直接接触图像库**（只经 core 接口）；worker 事件经 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 转发到 GUI 线程；所有用户可见文本 `tr()` 包装（i18n 预留）；配色用 Qt 6.8 内建 Fluent 风格，不做自绘（Mica 属 M3）。

| 任务 | 文件 | 关键契约 |
|---|---|---|
| **T9** | `ui/mainwindow.{h,cpp}`、`ui/filelistmodel.{h,cpp}`、`ui/thumbnails.{h,cpp}` | 三区布局（顶部步骤导航 3 步 / 左侧文件常驻区 / 右侧步骤内容 / 底部状态栏+开始按钮）；文件列表：拖放、递归收集（`collect_inputs`）、移除/清空、不支持扩展计数提示；行 = 缩略图+文件名+尺寸+格式+状态徽标+例外徽标；`QSortFilterProxyModel` 搜索；双击 → 元数据编辑器（T11 的信号）；缩略图独立 ≤2 线程、内存 LRU、优先嵌入缩略图（OIIO/Exiv2 preview）否则降采样；运行期间（G5）步骤区禁用、仅运行页与取消可用；`开始` 启用条件 = 非空文件集 + 输出目录有效。**唯一负责 CMakeLists 登记的任务**（§2.3）。 |
| **T10** | `ui/paramform.{h,cpp}`、`ui/page_output.{h,cpp}`、`ui/settings_dialog.{h,cpp}` | 参数表单引擎按 §6.3 设计（后端/技术/无损选择器 → 核心区 → 高级折叠区带搜索 → 谓词重算 → `●` 标偏离 + 右键重置）；**无损开关必须同步写入保留键 `__lossless`**（§3.4 约定，否则谓词失效）；**JXL 开启无损时 vardct 整组 `visible=false` → 技术选择器自动切到 modular**（T2 口径 ⑤）；输出页：模式切换（转码/仅元数据）、8 格式选择、全局参数（输出根目录/冲突策略/色彩目标/位深联动）、格式参数区、预设管理入口；**启动时按 settings 恢复上次格式/预设/输出目录**（共识 §3.9）；设置对话框：workers/预算 GB/合成底色/日志级别/底图与高德 key/瓦片缓存/关于（版本+许可清单，数据来自 `library_versions()`）。 |
| **T11** | `ui/page_meta.{h,cpp}`、`ui/exif_editor.{h,cpp}`、`ui/presets_dialog.{h,cpp}` | 元数据页：批量规则卡片（时间偏移含前后对照预览/GPS/标签编辑/隐私剥除/mtime）+ 例外文件摘要 + GPS 卡片内嵌地图（T13 控件，未就绪时手动坐标降级）+ **"从选中文件读取坐标"按钮**（共识 §3.6）；单文件编辑器：EXIF 分组树（IFD0/Exif/GPS，MakerNote 只读）+ 值编辑（Exiv2 类型校验）+ 按编号添加任意标签 + XMP 页签 + 时间/GPS 三态例外页签；预设对话框：列表/保存/加载/删除/另存，走 `preset_io`。 |
| **T12** | `ui/page_run.{h,cpp}`、`platform/paths.{h,cpp}`、`core/settings.{h,cpp}` | 运行页：总进度 + 每文件状态（12 态映射）+ 实时吞吐 + 取消 + 完成后"打开输出目录/查看日志"；`paths` 与 `settings` 按 §3.13/§3.14（含**记住上次会话**三键读写）；设置对话框的持久化归本任务（T10 只做 UI，读写由本任务提供函数）。 |
| **T13** | `mapwidget/mapwidget.{h,cpp}`、`mapwidget/providers.{h,cpp}`、`mapwidget/coord.{h,cpp}`、`tests/unit/test_gcj02.cpp` | 自绘 slippy 地图（`QWidget::paintEvent` 贴瓦片）：滚轮缩放、拖拽平移、点击选点（十字标 + 坐标回调）；瓦片源：OSM `https://tile.openstreetmap.org/{z}/{x}/{y}.png`（**须设 User-Agent**，遵守 OSM 政策）/ 高德 `https://webrd0{1..4}.is.autonavi.com/appmaptile?...`（用户 key 可选，坐标 GCJ-02）；搜索：Nominatim `https://nominatim.openstreetmap.org/search?format=json&q=`（UA 必设）/ 高德 Web 服务（key）；内存 LRU（默认 64MB，设置可调）+ 断网降级为手动坐标输入；`coord`：WGS-84↔GCJ-02 公开迭代算法（正逆变换），单测：北京/上海/广州已知点往返误差 <1e-6 度（内部一致性）、GCJ→WGS 偏移量级 300–600m 合理。 |

### 4.10 T14 引擎收口（**本批次**）

- **内容**：① 全量复验：`ctest --test-dir build/m1-t8 --output-on-failure` 全绿、`bash tests/golden/smoke.sh`（8+1 对）全 OK、27×8 格式矩阵零崩溃（损坏负例/CMYK 预期失败）、48MP 与 100 文件批规模验证；② **`TODO(M2)` 全仓扫描汇总**（`grep -rn "TODO(M2)"`，按模块分组，附一句影响面）；③ `--dev` 全语料运行日志 + 参数快照归档到 `.cache/m1-baseline/`；④ **引擎侧出口准则逐项核验**（§6 批次 1 表）；⑤ `README.md` 更新（`--dev` 用法、`pp_verify`、测试命令、docs 链接；**UI 章节留待下一批次**）；⑥ 输出「**UI 批次启动前的接口冻结清单**」：列出 UI 将消费的全部 core 接口（文件 + 函数签名摘要）与两处加性修订（`EncodeResult.error`、`EncodeRequest.tech_id`）现状。
- **docs/ 只读**：报告正文写入 T14 的 §9 报告，由主对话落盘 `docs/m1-report.md`；T14 不得写 docs/。
- **验收**：§6 批次 1 表逐项 PASS。

### 4.11 T14b UI 收口（**下一批次**）

- **内容**：① `QT_QPA_PLATFORM=offscreen` 全 UI 冒烟脚本（`tests/ui_smoke.sh`：启动 → 收集文件 → 切三页 → 关闭，退出码 0）进 CI；② UI 手动走查逐项记录（三页流转、参数谓词联动、批量规则预览、例外徽标、仅元数据模式置灰项、运行页状态、取消响应、设置持久化与**上次会话恢复**、地图选点回填、断网降级）；③ §6 批次 2 表核验；④ 报告 → 主对话落盘 `docs/m1-walkthrough.md`。

---

## 5. 数据流与阶段细节（实现必读，冻结语义）

1. `probe` 用 OIIO 读 spec；同时读 EXIF 摘要（时间/GPS/ICC/Orientation）。
2. `decode` → float32；通道 {1,2,3,4}；CMYK → 错误；多页 → 首页 + warning。
3. `orient`：`rotate_orientation==true` 且 orientation∈2..8 → 旋转/镜像（用 `OIIO::ImageBufAlgo::rotate90/rotate180/rotate270/flip/flop` 组合），**清除** EXIF Orientation（写 1）；关闭时不旋转、保留标签。
4. `color`：`ColorManager::transform`；`KeepOriginal` 时不动像素（灰度原生保留）。
5. `flatten`：目标 `supports_alpha==false`（JPEG/BMP）且源有 alpha → 合成 `flatten_gray` 底色 + Warning{AlphaFlattened}。
6. 灰度目标能力：目标 `supports_gray==false`（WebP/HEIF/AVIF）且源 1 通道 → 转 RGB + Warning{GrayToRgbEncoded}（**在 color 阶段处理：即使 KeepOriginal 也要升维**）。
7. `encode`：位深 = `out_bitdepth`；`float→整型` 标准舍入（无抖动）。源位深 > 输出位深 → Warning{DepthDowngrade}。
8. `metawrite` 按 §5.2 矩阵；BMP → Warning{MetadataDropped}。
9. `mtime`：`sync_mtime` 时用 `effective_datetime`。
10. 失败：单文件 try/catch → `FileResult{error}`；不自动重试；后续文件继续。

---

## 6. M1 出口准则（T14 / T14b 逐项核验）

**批次 1 — 引擎侧（T14 核验，本轮）**

| # | 准则 | 证据 |
|---|---|---|
| 1 | 全部引擎模块落库（core / decode / codecs 无空实现、无 `TODO(M1)`；UI 除外） | 文件清单 + grep |
| 2 | 全语料 `--dev` 零崩溃、8 格式全部出图 | 27×8 矩阵结果表 |
| 3 | 8+1 对 smoke 金样断言全 OK | `tests/golden/smoke.sh` + `pp_verify` 输出 |
| 4 | 日志完整（运行头版本+参数快照 / 每文件分段耗时 / 警告 / 汇总） | 一份完整日志样本 + 字段核对 |
| 5 | 单测全绿（全部测试可执行；含 M0 五条无回归） | `ctest` 输出 |
| 6 | 仅元数据模式字节级保真（JPEG SOS / PNG IDAT / TIFF 像素 hash / WebP VP8L） | 压缩数据一致 + 像素 hash |
| 7 | 规模验证（48MP 预算路径 + 100 文件批并发计数） | `--dev` 输出表 |
| 8 | `TODO(M2)` 清单归档 + `--dev` 基线日志归档 | 报告清单 + `.cache/m1-baseline/` |

**批次 2 — UI 侧（T14b 核验，下一批次）**

| # | 准则 | 证据 |
|---|---|---|
| 9 | UI offscreen 冒烟绿 | `tests/ui_smoke.sh` |
| 10 | UI 手动走查清单全勾（含设置持久化与上次会话恢复、地图选点、断网降级） | `docs/m1-walkthrough.md` |
| 11 | 参数表单谓词联动正确（无损锁定 / 技术互斥 / 后端切换） | 走查记录 + 截图式描述 |
| 12 | 运行期锁定与取消可用（G5） | 走查记录 |

---

## 7. 事实速查（M0 实测，实现时不得重新怀疑）

- **jpegli 链接**：`find_package(libjpeg-turbo CONFIG REQUIRED)` → `libjpeg-turbo::jpegli-static`（自带 hwy 闭包）；`JPEG::JPEG` = libjpeg.so.62 兼容层。jpegli 头：`#include <jpegli/encode.h>`（复用 `jpeg_compress_struct`；`jpegli_set_distance(cinfo, float, boolean)`；`jpegli_create_compress` 是宏）。
- **OIIO**：JXL 插件注册名 **`jpegxl`**（扩展名 jxl）；**无独立 avif 插件**（heif 插件覆盖 avif/heic/heif）；TIFF compression 名表 = none/lzw/zip/ccittrle/packbits；TIFF tiling 走 `ImageSpec::tile_width/height`（16 倍数且非 0）；`--buildinfo` 可查依赖版本。
- **libheif 1.23.1**：`heif_get_encoder_descriptors(format, name, out, count)` **4 参数自由函数**（无 ctx）；运行时可用编码器 = x265 4.2（HEVC）/ aom 3.13.3 + SVT-AV1 4.1.0（AV1）；SVT 为 built-in（overlay 已设 `WITH_SvtEnc_PLUGIN=OFF`）。
- **libjxl 0.11.2**：box 顺序 `JxlEncoderUseBoxes → JxlEncoderAddBox("Exif", 4 字节 TIFF offset + TIFF blob) → JxlEncoderCloseBoxes → JxlEncoderCloseInput`；三态参数（-1=库默认）在 M0 表中已 Bool 化，tooltip 注明。
- **exiv2 0.28.8**：`ExifParser::encode(blob, littleEndian, exifData)`（无 `ExifData::copy`）；`XmpParser::initialize()` 必须先调；`enableBMFF()` 为 `[[deprecated]]`（仍可用）；`bmff`+`xmp` feature 已启用。
- **lcms2 2.19.1**：`cmsD50_xyY()` 是**函数**（要加括号）；意图固定 RELATIVE_COLORIMETRIC + BPC。
- **链接形态**：x64-linux 全静态（唯一 .so = libjpeg.so.62）；构建前必须 `source tools/env.sh`（否则 ccache 只读报错，R18）。
- **命令**：构建 `cmake --preset release && cmake --build --preset release`；测试 `ctest --preset release`；语料 `bash tools/gen_corpus.sh`（默认路径已修好，D1）。
- **OIIO 3.1.14 实测（T3）**：`read()` 六参 `chend=0` → 段错误（bt）；CMYK TIFF → 3 通道 + `tiff:ColorSpace="CMYK"`；格式名用 `ImageInput::format_name()`；多页用 `seek_subimage`；ICC 为 `uint8[n]` 数组属性；**JXL 色彩编码暴露 = `CICP int[4]` + `ICCProfile` + `oiio:ColorSpace="srgb_rec709_scene"`，无 `jxl:*` 属性**（R13 尾闭合，见 §8）；GIF 帧 `alpha_channel==4` 越界 quirk；未取 error 的 ImageBuf 析构会打印告警。
- **位深能力（R19 定稿，T7b `201c8a8` 后）**：`heif/x265 = 8,10,12`、`avif/svt-av1 = 8,10`、`avif/libaom = 8,10,12`；x265 多比特深度依赖 overlay `vcpkg-overlay/x265/`（port-version 1）——**这是唯一不可省的 overlay 依赖**，重建 binary cache 时必须带 `--overlay-ports=vcpkg-overlay`。

---

## 8. 遗留项（M1 顺手处理，纳入 T14 清单）

- D4：M0 遗留 10 处 `TODO(M0-CD)` 注释（`tools/pp_linkprobe.cpp` 4 处、`tools/pp_mkfixtures.cpp` 6 处）→ 核对后删除或改 `TODO(M2)`。
- D7/R18：`CMakeLists.txt` 的 ccache 探测加可用性回退（探测失败则不设 launcher）→ **归 T14**（§2.3 未授权 T1 做，T1 已确认未做）。
- app 版本字面量 `"0.1.0"`（logger.cpp）与 `project(... VERSION)` 的同步：M1 手工一致；M2 引入生成版本头（T1 已打 TODO(M2)）。
- R1：**已闭合（T5 → T5b）**——真因不是 eXIf chunk，而是 **exiv2 未编译 zlib** → PNG 未注册为图像类型。`vcpkg.json` exiv2 features 加 `png`（port 自带 feature，无需 overlay）→ 探针验证 PNG 元数据写入/读回成功、15/15 测试无回归。T5c 复核降级分支与仅元数据模式后，PNG 全路径恢复；exiv2 config 自带 `find_dependency(ZLIB)`。
- R11：float→int 双重转换（T5/T6 验收时确认只在 codecs 层转一次）。
- R13 尾：**已闭合（T3 实测）**——OIIO 3.1.14 对 JXL 源的色彩描述暴露为 `CICP int[4]`（实测 1,13,0,1 = BT.709/sRGB 传递）+ `ICCProfile uint8[536]` + `oiio:ColorSpace="srgb_rec709_scene"`，无 `jxl:*` 属性；M1 的"源 profile 优先级"（嵌入 ICC → 格式原生描述 → 假定 sRGB）据此可实现（CICP⇄lcms2 映射留 M2，M1 用 ICC 优先，无 ICC 时按 `oiio:ColorSpace` 提示 + 假定 sRGB）。
- R19（新增，T7 产出，**T7b 后定稿**）：位深能力 = `heif/x265 8,10,12` / `avif/svt-av1 8,10` / `avif/libaom 8,10,12`；x265 多比特深度由 overlay `vcpkg-overlay/x265/` 提供（唯一必需的 overlay 依赖）；运行期与静态集取交集，默认 10。

---

## 9. 报告格式（所有 subagent 最终输出，≤150 行）

```
## REPORT
status: SUCCESS|PARTIAL|FAILED|BLOCKED
task: M1-T<n> <名称>
tasks-done: [...]
tasks-skipped: [... (原因)]
api-deltas:
- <库/文件>: <任务书假设> → <事实> (来源: <命令/文件:行>)
artifacts:
- <路径> (一句话；标注是否含 PP-FROZEN 复制)
frozen-check: <是否逐字节复制了 §3 头文件；未复制则说明原因>
tests: <命令> → <退出码>（列出新增测试数）
drumbeat: <--dev / 单测 结果；T8 起必须含全语料矩阵结论>
todos: <新增 TODO(M2) 条数 + 一句摘要>
commits: <hash + message 列表>
next-needed:
- <需主对话决策事项>
```

---

## 10. 编排备注（主对话维护）

- W1：T1、T2 并行（T2 依赖 T1 头文件存在即可，不依赖其实现）。
- W2：T3、T4、T5 并行（三者互不依赖；T5 最大，可单独给最长时限）。
- W3：T6、T7 并行（都只依赖 §3.5/§3.6 头文件）。
- W4：T8 单任务——**M1 的关键集成点**，首次端到端；若红，主对话决策后派修复轮，不得跳过。
- **本批次（本轮）**：W1（T1/T2）→ W2（T3/T4/T5）→ W3（T6/T7）→ W4（T8）→ W6（T14 引擎收口）；修复任务 T5b/T6b/T7b/T7d 按需插入，不占波次。
- **下一批次**：W5（T9–T13 UI 并行；CMakeLists 由 T9 删 `PP_M0_SMOKE`，所有者序列见 §2.3）→ W7（T14b UI 收口）。
- 任一波 PARTIAL/BLOCKED → 主对话裁决后重派或修复，同类错误 3 次即 STOP。
- 每个 subagent 提示词必须包含：本任务 ID、必读章节（§1 纪律 + 本文档对应任务节 + §3 冻结接口 + §7 事实 + §9 报告格式）、禁止事项（docs/ 只读、PP-FROZEN 逐字节、禁 subagent、禁改依赖与构建基础设施、不碰他人文件域）。
