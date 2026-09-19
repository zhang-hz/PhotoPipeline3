# PhotoPipeline M0 任务书 v1.0

> **文档地位**：M0 阶段唯一执行依据，由主对话（架构者）维护。执行者是 subagent，**只执行本文档明确写出的动作**。
> 环境基线（已由主对话探明，执行者不必重复审计）：Ubuntu 26.04.1 LTS，16 核，30GB RAM，磁盘余量 65GB，**无 sudo**，已装 git 2.53 / cmake 4.2.3 / ninja 1.13 / g++ 15.2 / clang 21.1 / python3.14 / pip3 / ccache 4.12 / pkg-config。
> **DSH 文件沙箱 = workspace-write**：一切工具链路径必须落在仓库内（`vcpkg/`、`.toolchain/`、`.cache/`，均已 gitignore）。**禁止向 $HOME / 系统目录写任何文件**；被沙箱拒绝属预期，不绕过、不重试，报告即可。

---

## 1. 开发纪律（全部执行者强制，逐条生效）

1. **只做本文档明确列出的动作**。未覆盖的情况 = 停止该项，写入报告 `next-needed`，禁止自行决策。
2. **禁止修改 docs/ 目录任何文件**（本任务书、design.md、param-catalog.md、brainstorm-consensus.md），只读。
3. 标记 `PP-FROZEN` 的文件/代码段**逐字节复制，不得改动**（含注释与空行）。
4. **禁止再下发 subagent**——你是最底层执行者。
5. 禁止引入本文档之外的依赖、工具、编译选项；**禁止改动依赖版本**（vcpkg baseline 钉死）。
6. 设计/结构/接口/参数语义的一切疑问 → 报告，不实现、不绕过。
7. 网络仅限：克隆 vcpkg、下载依赖源码、aqt 下载 Qt、查询官方仓库 tag/README/头文件、下载 GPL-3.0 许可证文本。不得访问其他内容。
8. 文件写入仅限仓库内（/tmp 可作临时区）；所有命令从仓库根执行。
9. Git：只允许 `git add <明确路径>` + `git commit -m "M0-<task-id>: <摘要>"`；禁止 push/rebase/reset/改历史。
10. 幂等：任何步骤重跑不得破坏已完成产物。
11. 一切事实（版本号/tag/SHA/路径）必须来自命令输出，禁止凭记忆填写。
12. 单条命令预计 >3 分钟时用后台方式执行并轮询，不要反复空转。
13. 最终报告 ≤150 行；错误日志每段 ≤40 行。

## 2. 任务分组与编排

| 组 | 任务 | 执行波次 |
|---|---|---|
| ENV-1..4（环境引导） | §3 | Wave1 · SUB-A |
| SK-1..2（仓库骨架） | §4 | Wave1 · SUB-B（与 SUB-A 并行） |
| VP-1..3 + DB-1..2（依赖构建） | §5 | Wave2 · SUB-CD |
| BV-1..5（构建与验证） | §6 | Wave3 · SUB-E |
| PT-1..2（参数冻结） | §7 | Wave3 · SUB-F（与 SUB-E 并行） |
| EX-1（出口审计） | §8 | Wave4 · SUB-G |

## 3. ENV 组（SUB-A）

### ENV-1 快速复核
重跑环境探针确认 cmake≥3.28 / ninja / g++≥14 / python3 / git 存在（已知满足，仅确认）；输出到报告。**不安装任何 apt 包**（无 sudo，也不需要）。

### ENV-2 工具（aqtinstall，全部装进仓库内 `.toolchain/`）
> 修订（SUB-A 实测）：PyPI 包名是 **`aqtinstall`**（`aqt` 是 Anki 的 GUI 程序，无 CLI）；aqtinstall 3.3.0 硬编码写 `~/.local/share/aqt`，每次调用必须重定向 HOME。

1. 首选：`python3 -m venv .toolchain/venv && .toolchain/venv/bin/pip --no-cache-dir install aqtinstall`
2. 若 venv/ensurepip 不可用 → 备选：`pip3 --no-cache-dir install --target .toolchain/pylibs aqtinstall`，此后以 `PYTHONPATH="$PWD/.toolchain/pylibs" HOME="$PWD/.cache/aqt-home" python3 -m aqt` 调用（选了哪条写进报告）
3. 校验：`mkdir -p .cache/aqt-home && HOME="$PWD/.cache/aqt-home" .toolchain/venv/bin/aqt version` 有输出（子命令是 `version`，`--version` 不识别）。

### ENV-3 vcpkg（钉死）
1. `git clone https://github.com/microsoft/vcpkg vcpkg`（仓库根下，已 gitignore）
2. 钉死最新 release tag：`git -C vcpkg tag --list '[0-9]*' | sort -V | tail -1` → `git -C vcpkg checkout <tag>`；记录 tag 与 commit SHA（`git -C vcpkg rev-parse HEAD`）。
3. `vcpkg/bootstrap-vcpkg.sh -disableMetrics`
4. `mkdir -p .cache/vcpkg-binary`（仓库内 binary cache）
5. 写两个文件（模板见 §10.1、§10.2）：
   - `tools/versions.env`（**入库**，值=上面实测的 tag/SHA/Qt 版本）
   - `tools/env.sh`（**不入库**，机器路径）
6. `git add tools/versions.env && git commit -m "M0-ENV-3: pin vcpkg baseline"`（若仓库尚未 git init 则跳过提交，报告说明）。

### ENV-4 Qt（aqt）
1. 用 ENV-2 装好的 aqt（venv 可执行或 PYTHONPATH 方式）。**arch 名是 `linux_gcc_64`（不是 gcc_64）**，HOME 必须重定向：
   `mkdir -p .cache/aqt-home && HOME="$PWD/.cache/aqt-home" .toolchain/venv/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O "$PWD/.toolchain/Qt"`
2. 版本探测（仅在需要时）：`HOME=同上 aqt list-qt linux desktop`（完整 X.Y.Z 列表；aqtinstall 3.3.0 的 `--arch` 只接受完整 X.Y.Z，不接受 6.8 这种短版本）。6.8.3 已确认存在；若换版本，**同步改写 tools/versions.env 的 QT_VERSION** 并在报告注明。
3. 校验：`.toolchain/Qt/<ver>/gcc_64/bin/qmake -v` 且 `ls .toolchain/Qt/<ver>/gcc_64/lib/cmake/Qt6` 成功。**注意：arch 参数是 `linux_gcc_64`，但落地目录名被 aqt 规范化为 `<out>/<ver>/gcc_64`**（SUB-A 实测确认）——QT_DIR 一律写 gcc_64，仅 install-qt 的 arch 参数用 linux_gcc_64。
4. `tools/env.sh` 由 §10.2 模板生成（QT_DIR 按 versions.env 推导）。

## 4. SKEL 组（SUB-B）

按下列清单创建文件。**不执行 cmake、不构建、不创建 vcpkg 相关文件**（vcpkg.json/vcpkg-overlay/ 归 SUB-CD）。

| # | 文件 | 内容来源 |
|---|---|---|
| 1 | `src/core/types.h` | §10.3 PP-FROZEN 逐字节 |
| 2 | `src/core/params.h` | §10.4 PP-FROZEN 逐字节 |
| 3 | `src/core/format_tables.cpp` | §10.5 PP-FROZEN 结构（PP-PLACEHOLDER 段留空） |
| 4 | `src/codecs/encoder.h` | §10.6 PP-FROZEN 逐字节 |
| 5 | `src/main.cpp`、`src/ui/mainwindow.h`、`src/ui/mainwindow.cpp` | §10.7 |
| 6 | `CMakeLists.txt` | §11.1 |
| 7 | `CMakePresets.json` | §11.2 PP-FROZEN |
| 8 | `.clang-format`、`.clang-tidy`、`.gitignore`、`.editorconfig` | §11.3 |
| 9 | `.github/workflows/build-test.yml` | §11.4（含两个占位符，见说明） |
| 10 | `tools/pp_linkprobe.cpp` | 行为契约 §12.1（你写实现，契约冻结） |
| 11 | `tools/pp_mkfixtures.cpp` | 行为契约 §12.2 |
| 12 | `tools/pp_spikes.cpp` | 行为契约 §12.3 |
| 13 | `tools/gen_corpus.sh` | 行为契约 §12.4 |
| 14 | `tools/bootstrap.sh`、`tools/env.sh.example` | §11.5 |
| 15 | `tests/unit/test_smoke.cpp` | §12.5 |
| 16 | `tests/golden/SCHEMA.md` | §13 PP-FROZEN |
| 17 | `tests/golden/base/`、`edge/`、`meta/`、`real/` | 目录落盘；real/.gitkeep **不入库**（§11.3 忽略 real/ 属预期——主对话裁决 (a)：real/ 永远是用户本机样本，不入库） |
| 18 | `README.md` | §11.6 |
| 19 | `LICENSE` | 从 https://www.gnu.org/licenses/gpl-3.0.txt 下载全文 |
| 20 | `assets/icc/.gitkeep`、`assets/i18n/.gitkeep` | 空目录占位 |

SK-2：`git init -b main`（若已存在则复用），分三次提交：
- `M0-SK: frozen interfaces + build system`（表 1–9）
- `M0-SK: tools + tests`（表 10–17）
- `M0-SK: docs-meta + license`（表 18–20 + LICENSE）

## 5. DEPS 组（SUB-CD）

### VP-1 vcpkg.json（仓库根，内容=§14.1，其中 `"builtin-baseline"` 的值从 `tools/versions.env` 的 `VCPKG_SHA` 填入）
feature 名须对照 `$VCPKG_ROOT/ports/<name>/vcpkg.json` 实际清单核对：不存在者删除并在 api-deltas 记录；**禁止添加任务书没有的 feature**。已知需要核对的重点：openimageio 的 libjxl/libheif/webp/gif/targa/tools；libheif 的 x265/aom/libde265（以及**是否存在 svt-av1**——存在与否都要写进报告，这是 R4/R2 关键事实）。

### VP-2 jpegli 拦截 port（§14.2 模板）
- 目录：`vcpkg-overlay/libjpeg-turbo/`（**目录名必须是 libjpeg-turbo**——按名遮蔽官方 port 是唯一批准的拦截方式）
- REF = google/jpegli 最新 stable tag——**实测修订（SUB-CD）**：上游从未打 tag/release（ls-remote/tags API 均空），改为 pin main HEAD commit SHA（已钉 `031a0077f5799a6041004267fc12b956c1f52a20`，2026-06-01；vcpkg_from_github 支持 commit SHA，可复现；tarball URL 形如 `https://github.com/google/jpegli/archive/<sha>.tar.gz`）
- SHA512 = 对该 tag 的 GitHub tarball（`https://github.com/google/jpegli/archive/refs/tags/<tag>.tar.gz`）执行 `curl -L <url> | sha512sum`
- port manifest 的 `version`/`port-version` 字段：**逐字复制** baseline 处官方 port 的值：`git -C $VCPKG_ROOT show <VCPKG_TAG>:ports/libjpeg-turbo/vcpkg.json`
- portfile 的 CMake 选项名以 jpegli 仓库实际 CMakeLists/README 为准核对修正（逐条记录 api-deltas）；目标：安装出 libjpeg（libjpeg.so + jpeglib.h 等），与官方 port 用法兼容；**必须同时安装 jpegli 扩展头**（上游路径 `lib/jpegli/encode.h` 等——SUB-B 实测：公开头是它而非 `<jpegli.h>`，jpegli 复用 `jpeg_compress_struct`，`jpegli_set_distance` 三参数，`jpegli_create_compress` 为宏；tools/*.cpp 已按 `__has_include` 三级降级引用）

### VP-3 预授权备用（仅当触发）
若 libheif port **没有** svt-av1 feature：把 `$VCPKG_ROOT/ports/libheif` 整目录复制为 `vcpkg-overlay/libheif/`，在其基础上新增 feature `svt-av1`（依赖 `svt-av1`，CMake 选项 `-DLIBHEIF_USE_SVT_AV1=ON`）。除此之外**不得** overlay 其他任何 port；需要别的改动 = STOP 报告。

### DB-1 安装
```
source tools/env.sh
vcpkg install --overlay-ports=vcpkg-overlay --clean-after-build
# 在仓库根执行（manifest 模式 → vcpkg_installed/）；--clean-after-build 控制磁盘占用
# （若当前 vcpkg 版本无此 flag 则去掉并记录；磁盘余量 65GB 充足）
```

### DB-2 修复循环（授权边界）
- 允许：portfile 选项名/安装路径机械修正；SHA512 重算；tag 重选（仅最新 stable）；vcpkg.json feature 名对齐 port 实际；VP-3；某 port 在 gcc-15 下编译失败时以 `CC=clang-21 CXX=clang++-21` 重试整个安装（两者均 libstdc++，ABI 兼容）。
- 禁止：改 overlay 之外的 port；换依赖版本；绕过 jpegli 拦截（如关 OIIO 的 jpeg）；改 CMakePresets。
- 上限：同一错误第 3 次失败 = STOP，报告完整日志与你的分析。
- 成功标准：`vcpkg install` 退出码 0；`vcpkg list` 包含全部目标包。提交：`M0-DEPS: vcpkg manifest + jpegli overlay port`。

## 6. BUILD 组（SUB-E）
前置：ENV/SKEL/DEPS 全部完成。

- **BV-1**：`source tools/env.sh`；`cmake --preset release && cmake --build --preset release`；`cmake --preset dev && cmake --build --preset dev`；`cmake --preset tsan`（仅 configure）。find_package 目标名与实际 config 不符 → 机械修正 CMakeLists 并记录；接口头文件问题 → 报告不改。
- **BV-2**：`./build/release/pp_linkprobe`，全绿（退出码 0）；报告**全文输出**（OIIO 插件清单与 libheif 编码器名单是 R4 关键事实，必须完整抄录）。
- **BV-3**：`bash tools/gen_corpus.sh`；校验 `tests/golden/CHECKSUMS` 生成且 fixture 数 ≥26。
- **BV-4**：`./build/release/pp_mkfixtures --verify tests/golden/meta` 与 `./build/release/pp_spikes e`、`./build/release/pp_spikes f --golden-root tests/golden`；逐项 PASS/FAIL 报告（Spike A=linkprobe 的 oiio-plugins 行 + corpus 各文件 `oiiotool --info` 通过；Spike B=linkprobe 的 jpegli 行；C/D=mkfixtures --verify）。
- **BV-5**：`QT_QPA_PLATFORM=offscreen ./build/release/photopipeline`，2 秒自动退出码 0。
- 提交：`M0-BV: build green + corpus + spikes`（含 CHECKSUMS；tests/golden/real/ 不提交）。

## 7. PARAMS 组（SUB-F，与 SUB-E 并行，但须在 SUB-E 的 BV-1 之后才开始写 format_tables）
前置：vcpkg_installed 里已有全部头文件（即 DEPS 完成）。

- **PT-1**：逐库核对 `docs/param-catalog.md`（只读）与 `vcpkg_installed/x64-linux/include/` 下真实头文件（jpegli/encode.h、jxl/encode.h、webp/encode.h、OpenImageIO 的 png/tiff 输出参数文档/源码头）。然后**只替换** `src/core/format_tables.cpp` 中 `/* PP-PLACEHOLDER */` 段的数据（结构 PP-FROZEN 不动）。**designator 顺序强制**：ParamDef 初始化必须按声明序 key→label→type→def→lo→hi→step→choices→advanced→tooltip（visible/locked 不设）；BackendDef 用 .runtime_introspected→.techs 顺序（§10.4 字段已前置——主对话修复）。允许的修正：范围/默认值/枚举选项与头文件不符 → 以头文件为准。禁止：增删参数（目录里没有而你认为该有的 → 写入 next-needed）；改 key；决定参数去留。libheif/avif 两个格式的 techs 留空、`runtime_introspected=true`。产出 `api-deltas` 完整清单（任务书假设 vs 头文件事实）。
- **PT-2**：核对 `tests/golden/SCHEMA.md` 描述与 §13 一致（你不改 SCHEMA.md，发现不一致 → 报告）。
- 提交：`M0-PT: format tables frozen against headers`。

## 8. AUDIT 组（SUB-G）
逐项核验 §15 出口准则，每项给 PASS/FAIL + 证据（命令与输出关键行）。另执行：
1. `rm -rf build vcpkg_installed && source tools/env.sh && time (cmake --preset release && cmake --build --preset release)` 记录耗时（binary cache 命中，目标 <5 分钟构建；不含 vcpkg install 时间）。
2. CI yaml：`python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/build-test.yml'))"` 校验 + 确认占位符已被 SUB-CD 替换为 versions.env 的实际值。
3. 汇总全部波次 api-deltas → 遗留风险建议清单（供主对话更新 design.md §10）。
4. `git log --oneline` 全history 附报告。

## 9. 报告格式（所有 subagent 最终输出）

```
## REPORT
status: SUCCESS|PARTIAL|FAILED|BLOCKED
tasks-done: [...]
tasks-skipped: [... (原因)]
api-deltas:
- <库/文件>: <任务书假设> → <事实> (来源: <命令/文件:行>)
artifacts:
- <路径> (一句话)
key-commands:
- <命令> → <退出码>
next-needed:
- <需主对话决策事项>
```

---

## 10. 冻结接口与环境文件

### 10.1 `tools/versions.env`（PP-FROZEN 键名，值由 SUB-A 实测填入）
```bash
# PP-FROZEN(keys): 值必须来自命令输出
VCPKG_TAG=
VCPKG_SHA=
QT_VERSION=6.8.3
```

### 10.2 `tools/env.sh`（机器本地，不入库；由 ENV/bootstrap 生成）
```bash
#!/usr/bin/env bash
# PP-FROZEN(structure)
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tools/versions.env"
export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/vcpkg}"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$ROOT/.cache/vcpkg-binary}"
export QT_DIR="${QT_DIR:-$ROOT/.toolchain/Qt/${QT_VERSION}/gcc_64}"
export CCACHE_DIR="${CCACHE_DIR:-$ROOT/.cache/ccache}"
export CC="${CC:-gcc}"
export CXX="${CXX:-g++}"
```

### 10.3 `src/core/types.h`（PP-FROZEN 全文件）
```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core types (M0 frozen)
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace pp {

enum class Stage { Probe, Decode, Orient, Color, Flatten, Encode, MetaWrite, Done };

enum class WarningKind {
    DepthDowngrade,      // bit-depth reduced for target codec
    LossyFromLossless,   // lossless source encoded lossy
    MultipageTruncated,  // multipage/animated input: first page only
    AlphaFlattened,      // alpha composited onto background
    NoIccAssumeSrgb,     // no ICC found, assumed sRGB
    MetadataDropped,     // target container has no metadata (BMP)
    TimeFieldMissing,    // time fields absent, shift skipped
    GrayToRgbEncoded,    // grayscale encoded as RGB (webp/heif/avif)
};

struct Warning {
    WarningKind kind;
    std::string detail;  // English, structured
};

struct Timing {
    double decode_ms = 0, orient_ms = 0, color_ms = 0, flatten_ms = 0,
           encode_ms = 0, metawrite_ms = 0, total_ms = 0;
};

struct ImageInfo {  // probe result
    int width = 0, height = 0;
    int channels = 0;        // {1,2,3,4}
    int src_bitdepth = 8;
    bool has_alpha = false;
    bool is_multipage = false;
    bool has_icc = false;
    std::string format;      // OIIO format name
};

using ParamValue = std::variant<std::monostate, bool, int64_t, double, std::string>;
using ParamSet  = std::map<std::string, ParamValue>;

}  // namespace pp
```

### 10.4 `src/core/params.h`（PP-FROZEN 全文件）
```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — ParamSchema (M0 frozen)
#pragma once
#include <functional>
#include <optional>
#include <utility>
#include "core/types.h"

namespace pp {

enum class ParamType { Int, Float, Bool, Enum };

struct ParamDef {
    std::string key;
    std::string label;      // Chinese label, UTF-8
    ParamType   type = ParamType::Int;
    ParamValue  def;                            // default (visual-transparent tier)
    double      lo = 0, hi = 0, step = 1;        // Int / Float
    std::vector<std::pair<std::string, ParamValue>> choices;  // Enum {name, value}
    bool        advanced = false;
    std::string tooltip;    // Chinese explanation + English term
    // Predicates (optional; filled in M1, empty in M0)
    std::function<bool(const ParamSet&)> visible;
    std::function<std::optional<ParamValue>(const ParamSet&)> locked;
};

struct TechDef {
    std::string id, label;
    bool lossless_capable = false;
    std::vector<ParamDef> params;
};

struct BackendDef {
    std::string id, label;  // e.g. "svt-av1" / "libaom"
    bool runtime_introspected = false;  // true for libheif-based formats
    std::vector<TechDef> techs;
};

struct FormatDef {
    std::string id, label, ext;  // e.g. "jxl"
    std::vector<BackendDef> backends;
    std::vector<int> bitdepths;  // {8} / {8,16} ...
    bool supports_alpha = false;
    bool supports_gray  = false;
    std::string meta_path;       // "exiv2" | "libheif" | "jxl-box" | "none"
};

// Static format tables. libheif-based formats (heif/avif) have empty techs and
// runtime_introspected=true; their params come from libheif at runtime (M1).
const std::vector<FormatDef>& static_formats();

}  // namespace pp
```

### 10.5 `src/core/format_tables.cpp`（结构 PP-FROZEN；`/* PP-PLACEHOLDER */` 段由 SUB-F 填数据）
```cpp
// PP-FROZEN(structure): 只允许替换 /* PP-PLACEHOLDER */ 注释处的内容
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/params.h"

namespace pp {

const std::vector<FormatDef>& static_formats() {
    static const std::vector<FormatDef> fmts = {
        FormatDef{
            .id = "jpeg", .label = "JPEG", .ext = "jpg",
            .backends = { BackendDef{
                .id = "jpegli", .label = "jpeg-li", .runtime_introspected = false,
                .techs = { TechDef{
                    .id = "dct", .label = "DCT", .lossless_capable = false,
                    .params = {
                        /* PP-PLACEHOLDER(jpeg): SUB-F fills from headers */
                    } } } } },
            .bitdepths = {8},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "jxl", .label = "JPEG XL", .ext = "jxl",
            .backends = { BackendDef{
                .id = "libjxl", .label = "libjxl", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "vardct", .label = "VarDCT", .lossless_capable = false,
                             .params = { /* PP-PLACEHOLDER(jxl-vardct) */ } },
                    TechDef{ .id = "modular", .label = "Modular", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(jxl-modular) */ } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "jxl-box" },
        FormatDef{
            .id = "png", .label = "PNG", .ext = "png",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "deflate", .label = "Deflate", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(png) */ } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "tiff", .label = "TIFF", .ext = "tif",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "codec", .label = "Compression", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(tiff) */ } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "webp", .label = "WebP", .ext = "webp",
            .backends = { BackendDef{
                .id = "libwebp", .label = "libwebp", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "lossy", .label = "Lossy", .lossless_capable = false,
                             .params = { /* PP-PLACEHOLDER(webp-lossy) */ } },
                    TechDef{ .id = "lossless", .label = "Lossless", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(webp-lossless) */ } } } } },
            .bitdepths = {8},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "bmp", .label = "BMP", .ext = "bmp",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "raw", .label = "Raw", .lossless_capable = true,
                             .params = {} } } } },
            .bitdepths = {24},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "none" },
        FormatDef{
            .id = "heif", .label = "HEIF", .ext = "heic",
            .backends = { BackendDef{
                .id = "x265", .label = "x265", .runtime_introspected = true,
                .techs = {} } },
            .bitdepths = {8, 10},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
        FormatDef{
            .id = "avif", .label = "AVIF", .ext = "avif",
            .backends = {
                BackendDef{ .id = "svt-av1", .label = "SVT-AV1", .runtime_introspected = true,
                            .techs = {} },
                BackendDef{ .id = "libaom", .label = "libaom", .runtime_introspected = true,
                            .techs = {} } },
            .bitdepths = {8, 10},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
    };
    return fmts;
}

}  // namespace pp
```

### 10.6 `src/codecs/encoder.h`（PP-FROZEN 全文件）
```cpp
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — encoder interface (M0 frozen)
#pragma once
#include <OpenImageIO/imagebuf.h>
#include <filesystem>
#include <functional>
#include "core/params.h"
#include "core/types.h"

namespace pp {

struct MetadataPayloads {
    std::string exif_blob;    // TIFF blob (ExifData::copy, littleEndian)
    std::string xmp_rdf;      // XMP RDF xml
    std::string icc_profile;  // target or as-is profile bytes
};

struct EncodeRequest {
    OIIO::ImageBuf& img;             // float32, channels {1,2,3,4}
    const ParamSet& params;
    int out_bitdepth = 8;
    const MetadataPayloads& meta;
    std::filesystem::path out_path;
    std::function<bool()> cancelled;
};

struct EncodeResult {
    uint64_t bytes = 0;
    std::vector<Warning> warnings;
    Timing t;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual const FormatDef& format() const = 0;
    virtual EncodeResult encode(const EncodeRequest&) = 0;
};

}  // namespace pp
```

### 10.7 UI 骨架
`src/main.cpp`：
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M0 skeleton
#include <QApplication>
#include <QTimer>
#include "ui/mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    pp::ui::MainWindow w;
    w.show();
#ifdef PP_M0_SMOKE
    QTimer::singleShot(2000, &app, &QApplication::quit);  // M1 removes this
#endif
    return app.exec();
}
```
`src/ui/mainwindow.h`：
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>

namespace pp::ui {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
};
}  // namespace pp::ui
```
`src/ui/mainwindow.cpp`：
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/mainwindow.h"

namespace pp::ui {
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("PhotoPipeline M0"));
    resize(1200, 760);
}
}  // namespace pp::ui
```

## 11. 构建/工程文件

### 11.1 `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.28)
project(PhotoPipeline VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PP_BUILD_DEV "Build dev-only tools" OFF)
set(CMAKE_AUTOMOC ON)  # Q_OBJECT (mainwindow) — wave3 template fix

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_program(PP_CCACHE ccache)
if(PP_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${PP_CCACHE})
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenImageIO REQUIRED CONFIG)
find_package(JPEG REQUIRED)
find_package(exiv2 REQUIRED CONFIG)
find_package(libheif REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
pkg_check_modules(LIBJXL REQUIRED IMPORTED_TARGET libjxl)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp libwebpencode)
pkg_check_modules(TIFF4 REQUIRED IMPORTED_TARGET libtiff-4)
# PP-NOTE: find_package/pkg-config 目标名允许按 vcpkg_installed 实际情况机械修正，逐条记录

add_library(pp_core STATIC src/core/format_tables.cpp)
target_include_directories(pp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(photopipeline src/main.cpp src/ui/mainwindow.cpp)
target_link_libraries(photopipeline PRIVATE pp_core Qt6::Widgets)
target_compile_definitions(photopipeline PRIVATE PP_M0_SMOKE)  # M1 removes

add_executable(pp_linkprobe tools/pp_linkprobe.cpp)
target_link_libraries(pp_linkprobe PRIVATE
    OpenImageIO::OpenImageIO JPEG::JPEG PkgConfig::LCMS2 exiv2::exiv2
    libheif::libheif PkgConfig::LIBJXL PkgConfig::WEBP)

add_executable(pp_mkfixtures tools/pp_mkfixtures.cpp)
target_link_libraries(pp_mkfixtures PRIVATE
    OpenImageIO::OpenImageIO JPEG::JPEG exiv2::exiv2
    libheif::libheif PkgConfig::LIBJXL PkgConfig::WEBP PkgConfig::TIFF4)

add_executable(pp_spikes tools/pp_spikes.cpp)
target_link_libraries(pp_spikes PRIVATE
    OpenImageIO::OpenImageIO PkgConfig::LCMS2 exiv2::exiv2)

add_executable(pp_unit tests/unit/test_smoke.cpp)
target_link_libraries(pp_unit PRIVATE pp_core)

enable_testing()
add_test(NAME linkprobe  COMMAND pp_linkprobe)
add_test(NAME unit       COMMAND pp_unit)
add_test(NAME fixtures   COMMAND pp_mkfixtures --verify ${CMAKE_SOURCE_DIR}/tests/golden/meta)
set_tests_properties(fixtures PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME spike_e    COMMAND pp_spikes e)
add_test(NAME spike_f    COMMAND pp_spikes f --golden-root ${CMAKE_SOURCE_DIR}/tests/golden)
set_tests_properties(spike_f PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME offscreen  COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen $<TARGET_FILE:photopipeline>)
set_tests_properties(offscreen PROPERTIES TIMEOUT 30)
```

### 11.2 `CMakePresets.json`（PP-FROZEN）
```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "base", "hidden": true, "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
      "cacheVariables": {
        "VCPKG_OVERLAY_PORTS": "${sourceDir}/vcpkg-overlay",
        "CMAKE_PREFIX_PATH": "$env{QT_DIR}",
        "VCPKG_INSTALLED_DIR": "${sourceDir}/vcpkg_installed"
      }
    },
    { "name": "release", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "dev", "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "PP_BUILD_DEV": "ON",
        "CMAKE_CXX_FLAGS_DEBUG": "-g -fsanitize=address,undefined -fno-omit-frame-pointer"
      } },
    { "name": "tsan", "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "PP_BUILD_DEV": "ON",
        "CMAKE_CXX_FLAGS_DEBUG": "-g -fsanitize=thread"
      } }
  ],
  "buildPresets": [
    { "name": "release", "configurePreset": "release" },
    { "name": "dev", "configurePreset": "dev" },
    { "name": "tsan", "configurePreset": "tsan" }
  ],
  "testPresets": [
    { "name": "release", "configurePreset": "release",
      "output": { "outputOnFailure": true } }
  ]
}
```

### 11.3 lint/git 配置
`.clang-format`：
```yaml
BasedOnStyle: LLVM
Standard: c++20
IndentWidth: 4
ColumnLimit: 100
AccessModifierOffset: -4
SortIncludes: CaseSensitive
```
`.clang-tidy`：
```yaml
Checks: 'bugprone-*,performance-*,modernize-*,readability-*,-modernize-use-trailing-return-type,-readability-identifier-length'
WarningsAsErrors: ''
HeaderFilterRegex: 'src/.*'
```
`.editorconfig`：
```
root = true
[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
indent_style = space
indent_size = 4
```
`.gitignore`：
```
build/
vcpkg/
vcpkg_installed/
.toolchain/
.cache/
tools/env.sh
aqtinstall.log
compile_commands.json
*.user
tests/golden/real/
tests/golden/CHECKSUMS
```
注：CHECKSUMS 不入库（跨机器字节差异），由 AUDIT/CI 本地生成。

### 11.4 `.github/workflows/build-test.yml`
```yaml
name: build-test
on: [push, pull_request]
jobs:
  linux:
    runs-on: ubuntu-24.04
    env:
      VCPKG_ROOT: ${{ github.workspace }}/vcpkg
      QT_DIR: ${{ github.workspace }}/Qt/6.8.3/gcc_64
      VCPKG_DEFAULT_BINARY_CACHE: ~/.cache/vcpkg-binary
    steps:
      - uses: actions/checkout@v4
      - name: apt deps
        run: sudo apt-get update && sudo apt-get install -y ninja-build ccache pkg-config python3-pip
      - name: Qt via aqtinstall
        run: |
          pip3 install aqtinstall
          aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O "$GITHUB_WORKSPACE/Qt"
      - name: vcpkg pinned
        run: |
          git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
          "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
          git -C "$VCPKG_ROOT" checkout <VCPKG_TAG_PLACEHOLDER>
      - name: vcpkg binary cache
        uses: actions/cache@v4
        with:
          path: ~/.cache/vcpkg-binary
          key: vcpkg-${{ hashFiles('vcpkg.json', 'vcpkg-overlay/**') }}
      - name: build + corpus + test
        run: |
          cmake --preset release
          cmake --build --preset release
          bash tools/gen_corpus.sh
          ctest --preset release
```
两个占位符 `<VCPKG_TAG_PLACEHOLDER>` 与 QT 版本号：由 SUB-CD 按 tools/versions.env 实际值替换（aqt 行与 QT_DIR 的 6.8.3 同步）。CI 中 vcpkg overlay 生效方式：CMakePresets 的 `VCPKG_OVERLAY_PORTS` 已覆盖（vcpkg install 由 CMake 配置时自动执行）。

### 11.5 `tools/bootstrap.sh`（新机器一键）与 `tools/env.sh.example`
```bash
#!/usr/bin/env bash
# PP-FROZEN(structure): fresh-machine bootstrap (everything repo-local)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tools/versions.env"
mkdir -p "$ROOT/.toolchain" "$ROOT/.cache/vcpkg-binary"
if python3 -m venv "$ROOT/.toolchain/venv" 2>/dev/null; then
    "$ROOT/.toolchain/venv/bin/pip" --no-cache-dir install aqtinstall
    AQT=("$ROOT/.toolchain/venv/bin/aqt")
else
    pip3 --no-cache-dir install --target "$ROOT/.toolchain/pylibs" aqtinstall
    AQT=(env "PYTHONPATH=$ROOT/.toolchain/pylibs" python3 -m aqt)
fi
mkdir -p "$ROOT/.cache/aqt-home"
HOME="$ROOT/.cache/aqt-home" "${AQT[@]}" install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O "$ROOT/.toolchain/Qt"
if [ ! -d "$ROOT/vcpkg" ]; then
    git clone https://github.com/microsoft/vcpkg "$ROOT/vcpkg"
fi
git -C "$ROOT/vcpkg" fetch --tags
git -C "$ROOT/vcpkg" checkout "${VCPKG_TAG}"
"$ROOT/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
cp -n "$ROOT/tools/env.sh.example" "$ROOT/tools/env.sh"
echo "OK — source tools/env.sh then: cmake --preset release && cmake --build --preset release && bash tools/gen_corpus.sh && ctest --preset release"
```
`tools/env.sh.example` = §10.2 原文。

### 11.6 `README.md`（骨架版）
内容包含：一句话项目说明（批量像素级转码器+元数据手术台，GPL-3.0+）、构建四步（bootstrap.sh → source tools/env.sh → cmake --preset release && cmake --build --preset release → bash tools/gen_corpus.sh && ctest --preset release）、M0 工具说明表（pp_linkprobe / pp_mkfixtures / pp_spikes / gen_corpus.sh）、指向 docs/ 三份文档的链接。

## 12. 工具行为契约（实现可写，契约冻结）

### 12.1 `pp_linkprobe`
- 输出行格式：`PROBE <name> OK|FAIL <detail>`，尾部两行 `PLUGINS: <逗号清单>`、`HEIF_ENCODERS: <逗号清单>`，退出码 = FAIL 数。
- 检查项（name → 判定）：
  - `lcms2`：`cmsCreate_sRGBProfile()` 非空；float RGB 往返单像素 |err|≤1e-5；`cmsCloseProfile` 成功。
  - `exiv2`：`Exiv2::versionString()` 非空。
  - `exiv2-bmff`：调用 Exiv2 启用 BMFF 的 API（0.28+ 为 `Exiv2::enableBMFF(true)`；不存在 → FAIL，detail="API missing"）。
  - `oiio-plugins`：`OIIO::get_string_attribute("format_list")` 必须包含 jpeg, png, tiff, jxl, heif, webp, gif, targa, bmp（**修订（SUB-E 实测）**：OIIO 无独立 avif 插件——heif 插件拥有 avif/heic/heif 扩展名，linkprobe 的 heif 项 detail 注明 "covers avif" 即可）；缺失名写入 detail。
  - `jpegli`：`jpegli_create_compress`/`jpegli_set_distance`/`jpegli_destroy_compress` 调用成功（include `<jpegli.h>`；头不存在则 extern "C" 声明同名符号）。
  - `libjxl`：`JxlEncoderVersion() > 0`。
  - `libheif-hevc`：`heif_get_encoder_descriptors(ctx, heif_compression_HEVC, NULL, NULL, 0, &count)` 后 count≥1；detail 打印编码器名。
  - `libheif-av1`：同上 AV1；detail 打印编码器名（**aom 与 svt-av1 是否存在是 R4 关键事实**）。
  - `libwebp`：`WebPGetEncoderVersion() > 0`。
- API 签名与实际头文件不符 → 按头文件机械适配，判定标准不变，记录 api-deltas。

### 12.2 `pp_mkfixtures`
- 用法：`pp_mkfixtures --make <dir> | --verify <dir>`。图像源：64×64 水平渐变 RGB（r=x*4, g=y*4, b=(x+y)，uint8）。
- `--make` 产出（均在 `<dir>` 内）：
  - `exif_full.jpg`：jpegli 编码（distance=1.0，444，无下采样）→ Exiv2 写 `Exif.Photo.DateTimeOriginal=2024:03:01 10:00:00`、`Exif.Image.Artist=M0`、GPS (31.2304N, 121.4737E)。
  - `webp_lossy.webp`：quality=90，`use_sharp_yuv=1`。
  - `webp_lossless.webp`：lossless，`exact=1`。
  - `heif_exif.heic`：libheif x265，quality=50，chroma 444，注入 EXIF blob。
  - `avif_exif.avif`：libheif 可用的 AV1 编码器（优先 aom；实际用的写进输出 detail）+ EXIF blob。
  - `jxl_exif.jxl`：libjxl（effort=7, distance=1.0）+ Exif box + XMP box（xml ）。
  - `cmyk.tif`：libtiff 直写，PHOTOMETRIC_SEPARATED、8bpc、4 samples。
- **EXIF blob 生成方式（冻结；按 Exiv2 0.28.5 实测修订）**：`Exiv2::ExifData` 填充 → `Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, exifData)`（0.28.5 无 `ExifData::copy` 成员，输出同为裸 TIFF 载荷）。XMP：先 `XmpParser::initialize()`，再 `Exiv2::XmpParser::encode(packet, xmpData)`。
- `--verify`：`exif_full.jpg` 读回三组值相等；heic/avif/jxl 经 Exiv2 读回 `DateTimeOriginal` 相等；`jxl_exif` 另加 OIIO 解码成功（64×64）；`cmyk.tif` 与 webp×2 能被 OIIO open。输出 `FIXTURE <name> OK|FAIL|SKIP <detail>`；目录不存在 → 全部 SKIP、退出码 77；退出码 = FAIL 数。

### 12.3 `pp_spikes`
- 用法：`pp_spikes e` 或 `pp_spikes f --golden-root <tests/golden>`。
- `e`（lcms2 数值金值）：① sRGB→sRGB float 变换（TYPE_RGB_FLT，INTENT_RELATIVE_COLORIMETRIC + `cmsFLAGS_BLACKPOINTCOMPENSATION`）恒等 ≤1e-5；② sRGB (1,1,1) → `cmsCreateLab4Profile(D50)` 的 Lab：L∈[99.5,100.5]，|a|,|b|≤1。
- `f`（Exiv2 无损重写保真，R10）：`meta/exif_full.jpg` 复制到 `.cache/tmp/pp_spike_f.jpg`（仓库内路径，沙箱安全；盘上代码若仍写 /tmp，SUB-E 授权机械替换）→ Exiv2 改 `Artist=M0-F` → writeImage。判定：① 两文件自 `0xFF 0xDA`（SOS）起的尾部字节完全一致；② OIIO 解码两者像素 hash 相等。文件缺失 → SKIP 77。
- 输出 `SPIKE e|f OK|FAIL <detail>`，退出码 = FAIL 数。

### 12.4 `tools/gen_corpus.sh`
```
用法: bash tools/gen_corpus.sh [GOLDEN_ROOT=tests/golden]
OIIOTOOL 默认 vcpkg_installed/x64-linux/tools/openimageio/oiiotool（可用环境变量覆盖）
PP_MKFIXTURES 默认 build/release/pp_mkfixtures
产出（fixture 全集，编号即验收清单）:
  base/: rgb8.png rgb16.png gray8.png gray16.png graya8.png rgba8.png rgba16.png
         rgb8.tif rgb16.tif gray16.tif multi.tif photo.jpg targa.tga bmp24.bmp anim.gif jxl8.jxl
  edge/: cmyk.tif corrupt_trunc.jpg corrupt_zero.png "测试📸unicode.png"
  meta/: (pp_mkfixtures --make meta/)
规则:
  - oiiotool 语法以 `--help` 实测为准，与本任务书示例不符时机械适配并记录
  - pattern 用 checker；通道数 1/2/3/4；uint16 系列覆盖 rgb/gray/rgba
  - multi.tif = 两个子图；anim.gif = 3 个子图
  - jxl8.jxl 由 oiiotool 写；若失败 → pp_mkfixtures 追加生成（记录用了哪条路径）
  - corrupt_trunc.jpg = head -c 300 base/photo.jpg；corrupt_zero.png = 空文件
  - 结尾: (cd GOLDEN_ROOT && find base edge meta -type f | sort | xargs sha256sum > CHECKSUMS)
  - 脚本必须 set -euo pipefail 且可重复执行（幂等覆盖）
  - **D2 修复（审计后）**：TIFF 生成固定 `--attrib "DateTime" "2024:01:01 00:00:00"`（TIFF writer 默认写当前时间且 `--eraseattrib` 无效）；`Software` 属性随输出路径/参数变化（同名同参跨次稳定）→ 同机字节稳定、跨机器不保证，CHECKSUMS 以本地生成为准
```

### 12.5 `tests/unit/test_smoke.cpp`
手写断言（无第三方测试框架），失败返回非零并打印：
1. `static_formats().size() == 8`；id 集合恰为 {jpeg,jxl,png,tiff,webp,bmp,heif,avif}。
2. 每个格式 bitdepths 非空、meta_path ∈ {exiv2,libheif,jxl-box,none}。
3. ParamSet 插入/读取 ParamValue 往返一致。

## 13. `tests/golden/SCHEMA.md`（PP-FROZEN）
```markdown
# expected.json 断言 schema（M0 冻结 v1）

- case: string（用例名）
- input: string（相对 tests/golden 的输入路径）
- output_format: string（格式 id）
- assert.pixel.mode: "exact" | "psnr"
  - exact: 解码回读逐位相等（无损路径）
  - psnr: 回读 PSNR ≥ assert.pixel.threshold_db 才 PASS
- assert.pixel.threshold_db: number（psnr 时必填）
- assert.metadata: [{ key: string, op: "eq"|"exists"|"absent", value: string }]
  - key 为 Exiv2 完整标签名（Exif.* / Xmp.*）
- assert.warnings_contain: [string]（WarningKind 名，输出警告必须包含所列项）

示例:
{
  "case": "smoke-jxl-lossless",
  "input": "base/rgb8.png",
  "output_format": "jxl",
  "assert": {
    "pixel": { "mode": "exact" },
    "metadata": [],
    "warnings_contain": []
  }
}
```

## 14. vcpkg 文件模板（SUB-CD）

### 14.1 `vcpkg.json`（仓库根）
```json
{
  "name": "photopipeline",
  "version": "0.1.0",
  "builtin-baseline": "<VCPKG_SHA from tools/versions.env>",
  "dependencies": [
    { "name": "openimageio", "features": ["libjxl", "libheif", "webp", "gif", "targa", "tools"] },
    { "name": "libheif", "features": ["x265", "aom", "libde265"] },
    "libjxl",
    "libwebp",
    "lcms",
    { "name": "exiv2", "features": ["bmff"] },
    "spdlog"
  ]
}
```
（feature 名按 §5 VP-1 规则核对增删；**禁止**添加新依赖项。exiv2 的 `bmff` 为主对话裁决加入：设计文档 §8.2 强制项（HEIF/AVIF 元数据读写，R2），Spike C 与 linkprobe exiv2-bmff 依赖它。）
（SUB-CD 实测 feature 事实：openimageio 用 `jpegxl`（无 libjxl）；`targa` 无 feature（targa 插件永远编译）；libheif 的 `x265`→`hevc`，`libde265` 为无条件依赖；libheif 的 SVT-AV1 CMake 选项为 `WITH_SvtEnc`（非 LIBHEIF_USE_SVT_AV1）。）

### 14.2 `vcpkg-overlay/libjpeg-turbo/portfile.cmake` + `vcpkg.json`
`portfile.cmake`：
```cmake
# PP-FROZEN(structure): 拦截官方 libjpeg-turbo → 构建 google/jpegli
# 选项名与安装目标以 jpegli 实际 CMake 为准核对（修正须逐条记录 api-deltas）
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO google/jpegli
    REF "<最新 stable tag>"
    SHA512 "<现场计算>"
)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        # 按实际 CMake 选项核对：关闭测试/工具，仅产出 libjpeg 兼容库
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURS "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
```
`vcpkg.json`（port manifest）：
```json
{
  "name": "libjpeg-turbo",
  "version": "<复制 baseline 官方 port 的 version>",
  "port-version": <复制>,
  "description": "Intercepted: builds google/jpegli as drop-in libjpeg62 replacement (full-featured).",
  "dependencies": [
    "hwy",
    "lcms",
    { "name": "vcpkg-cmake", "host": true },
    { "name": "vcpkg-cmake-config", "host": true }
  ]
}
```
（dependencies 按 jpegli 实际 CMake 需求机械修正并记录。）

## 15. M0 出口准则（SUB-G 逐项核验）

| # | 准则 | 证据 |
|---|---|---|
| 1 | 全新环境一条链路可复现：`tools/bootstrap.sh`（本机已满足前置）后 `cmake --preset release && cmake --build --preset release` 成功 | AUDIT 以 `rm -rf build vcpkg_installed` 后全链路重跑计时模拟 |
| 2 | Link Probe 全绿 | BV-2 输出全文（含 OIIO 插件清单、libheif 编码器名单） |
| 3 | 六 Spike 全绿（A=oiio-plugins+corpus info、B=jpegli、C/D=mkfixtures --verify、E=spike e、F=spike f） | BV-4 |
| 4 | 语料 ≥26 fixture + CHECKSUMS 生成 | BV-3 |
| 5 | 参数表已编译进库（PP-PLACEHOLDER 全部替换）且 api-deltas 记录在案 | PT-1 |
| 6 | expected.json schema 冻结 | SCHEMA.md 提交 |
| 7 | CI workflow 就绪（YAML 可解析、占位符已按 versions.env 替换） | AUDIT 第 2 步 |
| 8 | 空 Qt 窗口 offscreen 冒烟通过 | BV-5 |
| 9 | binary cache 命中后重建 <5 分钟 | AUDIT 第 1 步计时 |

## 16. 编排备注（主对话维护）

- 沙箱约束（主对话维护）：DSH 文件策略 = workspace-write，工具链全部仓库内（§10.2）；任何向仓库外写入被拒属预期，不绕过，报告即可。
- Wave1：SUB-A（ENV）与 SUB-B（SKEL）并行。SUB-A 不 git init；仓库由 SUB-B 初始化。SUB-A 的 versions.env 提交若遇仓库未初始化 → 跳过并报告（由 SUB-CD 补提交）。
- Wave2：SUB-CD（依赖构建，预计 40–120 分钟，gcc-15 失败时按授权切 clang-21 重试）。
- Wave3：SUB-E（BV）与 SUB-F（PT）并行；SUB-F 开始写 format_tables 前确认 DEPS 已完成（看 vcpkg_installed 存在与 git log）。
- Wave4：SUB-G（EX）。
- 任一波 PARTIAL/BLOCKED → 主对话决策后重派或修复，不自动重试超过 1 次。

## 17. Wave2 实测事实速查（SUB-CD 提供；SUB-E/SUB-F/SUB-G 必读）

**安装结果**：vcpkg install 退出码 0（attempt3，1m41s；binary cache 262MB 预热）；`vcpkg_installed/x64-linux/` 就绪。提交 `d7ccd76`。

**依赖版本**：OIIO 3.1.14.0#1 / libheif 1.23.1 / libjxl 0.11.2 / libwebp 1.6.0#2 / lcms 2.19.1#1 / exiv2 0.28.8 / spdlog 1.17.0#1 / x265 4.2 / aom 3.13.3 / svt-av1 4.1.0 / tiff 4.7.2 / highway 1.4.0。

**vcpkg.json 最终形态**：openimageio[jpegxl,libheif,webp,gif,tools] + libheif[hevc,aom,svt-av1] + exiv2[bmff] + libjxl/libwebp/lcms/spdlog。

**libheif 运行时编码器（R4 落地）**：HEVC → 'x265 HEVC encoder (4.2-vcpkg)'；AV1 → 'AOMedia Project AV1 Encoder v3.13.3' + 'SVT-AV1 encoder v4.1.0'（id=svt）。双后端成立。

**SUB-E 机械修正点（授权）**：
1. CMakeLists.txt：`libheif::libheif` → **`heif`**（libheif 1.23.1 导出的 target 名）。
2. CMakeLists.txt：pkg_check_modules(WEBP …) 去掉 **libwebpencode**（1.6.0 只装 libwebp/libwebpdecoder/libwebpdemux/libwebpmux/libsharpyuv.pc）。
3. pp_linkprobe 的 `heif_get_encoder_descriptors` 为 **4 参数自由函数**（SUB-B 已按此适配过，对照安装头核对即可）。
4. 其余 `TODO(M0-CD)` 注释点逐一对照 vcpkg_installed 头文件核实。

**jpegli 事实**：头文件在 `include/jpegli/{encode,decode,common,types}.h` + `include/lib/jpegli/*` + `include/lib/base/include_jpeglib.h`；libjpeg.so 的 version script 为 `jpeg*`（jpegli_* 符号已导出）；无 libjpeg.pc / CMake config（overlay 已提供 libjpeg-turbo-config.cmake + vcpkg-cmake-wrapper.cmake）；tiff 等消费者链接已验证。

**链接形态**：x64-linux triplet 为 **static**（全部 .a，唯一 .so 是 jpegli 的 libjpeg.so.62）；M1 若需全静态另行决策。

**并发纪律（Wave3）**：SUB-E 与 SUB-F 并行时——SUB-F **不得**运行 cmake/ninja（build/ 目录归 SUB-E），语法自检用 `g++ -std=c++23 -fsyntax-only -Isrc src/core/format_tables.cpp`；两者 git commit 只 add 自己的文件，遇 index.lock 等待 5s 重试。

**Wave3 裁决与实测（主对话维护）**：
- §10.4/§10.5 designator 矛盾已修复：params.h 的 BackendDef 将 runtime_introspected 前置于 techs（主对话亲自改文件并同步本文档；SUB-B 原样复制无责）。
- jpegli 链接形态定稿（SUB-CD 08a2ac0）：`find_package(libjpeg-turbo CONFIG REQUIRED)` → `libjpeg-turbo::jpegli-static`（STATIC IMPORTED，自带 libhwy.a 链接接口；裸链 libjpegli.a 会缺 24 个 hwy 符号）；`JPEG::JPEG` 继续用 libjpeg.so.62 兼容层（OIIO/tiff 消费者不变）。OIIO 的 JXL 走 module-mode（overlay openimageio 恢复 FindJXL.cmake + 补 hwy/brotli*/jxl_cms/lcms2 静态闭包）。exiv2 xmp → EXIV2_ENABLE_XMP + expat（内置 toolkit）。libheif config 追加确认已编码进 portfile（非手改 installed）。
- OIIO 构建为 JXL NONE：vcpkg openimageio port 删除了 OIIO 自带 FindJXL.cmake，而 libjxl port 无 CMake config（仅 pkgconfig），CONFIG-only 查找必然失败 → 裁决：overlay openimageio port 恢复 FindJXL.cmake（module-mode 走 libjxl.pc）；若 static triplet 下传递依赖失败，预授权备选：libjxl overlay 补最小 JXLConfig.cmake。
- OIIO 插件清单无独立 avif：heif 插件覆盖 avif 扩展名（§12.1 已修订）。
- 待 SUB-CD 复核：libheif-config.cmake 的 AOM find_dependency + fastfeat 链接接口追加**必须编码进 overlay libheif portfile**（若当前只是手改 vcpkg_installed，从零重建 binary cache 时会丢失）。
- SUB-E 已实测：spike e PASS（lcms2 金值 identity_max_err=0，Lab=(100, 7.57e-06, -7.63e-06)）；gen_corpus.sh 的 `--depth` 应为 `-d`（机械修正已做）；CMakeLists 需 `set(CMAKE_AUTOMOC ON)`（已修复入库）；libjxl 0.11 box API 顺序：UseBoxes → AddBox(Exif 内容前置 4 字节 TIFF offset) → CloseBoxes → CloseInput（mkfixtures 已修，R13 事实）；exiv2 port 的 xmp feature 非默认（默认无 XMP toolkit → XmpParser::encode 空 packet）→ vcpkg.json 增 exiv2[xmp]。
- 参数表裁决（主对话，SUB-F 报告后）：删无后端参数（use_dct4/8/16、TIFF quality/zstd_level、compression 枚举去 zstd/jpeg）；TIFF 枚举收敛 none/lzw/zip/ccittrle/packbits；webp filter_strength 默认 30（与默认 preset=photo 自洽）；responsive→jxl-modular；modular_palette 改 Int modular_palette_colors(-1..4096, def -1)；加 ycbcr/440 枚举与 tiff_tile_width/height；JXL 公共参数保持 vardct+modular 双份（M1 预设层去重）；其余默认值全部保留 catalog 视觉透明档。PT-1b 扩表任务：补齐头文件中有真实后端的参数（webp 13+、jpegli 4、libjxl 其余 frame settings；排除 thread_level——设计规定编码器内部线程全关）。
- PT-1b 完成（提交 43660be）：全表 **78 条 ParamDef / 68 唯一 key**（订正：SUB-G 实测 68 个不同 key 字符串，原记 55 口径错误；8 个 key 跨 tech/格式复用）（jpeg 15 | jxl-vardct 15 | jxl-modular 17 | png 1 | tiff 5 | webp-lossy 21 | webp-lossless 4 | bmp/heif/avif 0）。新实测事实：TIFF tiling 走 `ImageSpec::tile_width/height` 而非 tiff:* 属性（强制 16 倍数且非 0）；alpha_filtering 有效域 0-2（非 0-3）；near_lossless 属 lossless 路径（VP8L）；jpegli std_quant_tables 为无参开关、cicp_transfer_function 仅 16=PQ/18=HLG 有效；libjxl 排除项（JPEG 重压缩类、动图 frame_index、流式 buffering、测试用 heuristics）理由见 PT-1b 报告——M0 定稿不再扩。

**Wave4 审计结论（SUB-G 独立实跑，主对话复核）**：出口准则 **9/9 PASS**；fresh 链路 **8.12s**（vcpkg cache 恢复 3.38s + configure 0.86s + build 3.87s；`CCACHE_DISABLE=1` 冷编译 3.86s 排除缓存假象）；ctest 6/6（warm 与冷编译产物两次）；6 个冻结件与任务书逐字节一致（types.h/params.h/CMakePresets.json/bootstrap.sh/env.sh.example/SCHEMA.md）。
缺陷修复（主对话执行）：**D1** = CMakePresets 增 `VCPKG_INSTALLED_DIR=${sourceDir}/vcpkg_installed`（toolchain 默认装 `build/<preset>/vcpkg_installed`，与 gen_corpus.sh/CI/bootstrap 的根目录假设不符——fresh 链路末端与 CI 第 3 步必失败）；**D2** = gen_corpus.sh 的 4 个 TIFF 生成固定 `DateTime=2024:01:01 00:00:00`（writer 写当前时间、`--eraseattrib` 无效；`Software` 属性随路径变化 → 同机字节稳定、跨机器不保证）。
遗留（不阻塞）：10 处 `TODO(M0-CD)` 注释 M1 清理；CI 未实跑（ubuntu-24.04/gcc-13 组合待 GitHub 首跑）；exiv2 0.28.8 的 enableBMFF 已 deprecated（仅 2 条告警，无功能影响）。

**定向复审（SUB-G 复工，D1/D2 修复后）**：**D1 CLOSED、D2 CLOSED、出口准则 9/9 维持 PASS**——独立实跑 8 步全 rc=0（configure 4.26s 含 cache 恢复 2.9s / build 3.86s / 合计 8.11s）；根安装树 1.6G 且 `build/release/vcpkg_installed` 不存在；3 次 gen_corpus 的 CHECKSUMS 全等且 `git status --porcelain` 0 行（27/27 fixture 与提交版逐字节一致）；multi.tif 双子图 DateTime 四处一致且结构未损（2 subimages 64x64/32x32）。
新增轻微项：**D6**（presets 键序与 §11.2 代码块不一致——语义等价但违反冻结逐字节一致；主对话已按实际文件同步文档，已闭合）；**D7**（未 `source tools/env.sh` 直接构建时 ccache 回退到只读默认目录 → ninja 报 `ccache: error: Read-only file system`；操作前提是先 source，M1 加 ccache 可用性探测回退或 README 告警 → 登记 R18）。
复审事件披露：SUB-G 一条命令误含 `mv vcpkg_installed /tmp` 致根安装树被删，已从 binary cache 3.5s 完整重建并复验（38/38 + 6/6 + git 0 行）——仓库提交与工作树未受影响，附带再次证明 fresh 链路可完整复现。
