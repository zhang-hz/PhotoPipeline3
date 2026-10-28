# third_party/ssimulacra2

PhotoPipeline M8 内置的 **SSIMULACRA 2** 参考实现（libjxl v0.12.0 官方源码 + 兼容层）。
M8-C0 立项与 vendoring；M8-C1 完成兼容层接入、`pp::metric::compute()` 实现与数值/性能取证。

## 来源（pinned，逐字保留）

| 文件 | 来源 | 原始 SHA-256（vendored 时） | 适配后 SHA-256 |
|---|---|---|---|
| `src/ssimulacra2.cc` | libjxl v0.12.0 `tools/ssimulacra2.cc` | `5834E3B018ADE386121A76AC3D339850599FCC0BFF10AE70C04EAFE0C5705D82` | `3064BE144E7576E96FC4215B43B31DE27DC75443CD6C1F0A72DDA75F4B10BDC4` |
| `src/ssimulacra2.h` | libjxl v0.12.0 `tools/ssimulacra2.h` | `AEBF3AD747991FDEF0833394C5D429DB3A10DA5D1D11C2D36106EEC4EFD5988B` | 未改（同左） |
| `src/gauss_blur.cc` | libjxl v0.12.0 `tools/gauss_blur.cc` | `90C9D735482C7539C4CC992993DB3C001F02C745A30964462B723A555D796108` | `74C3FAF245A5E392821680513994350E3CCDBFF4241B26D2042761D40CFCD505` |
| `src/gauss_blur.h` | libjxl v0.12.0 `tools/gauss_blur.h` | `271FDB81C24D86AE2AE065E4671B033F30E9E74FCFE6202660F08BAD75E6881B` | 未改（同左） |
| `LICENSE` | libjxl `LICENSE`（BSD-3-Clause） | `8405932022A556380C2D8C272EFF154A923FEB197233F348CE5F7334FB0A5EDE` | 未改 |
| `PATENTS` | libjxl `PATENTS`（专利授权） | `91915F8AE056A68A3C5BDF05D9F6F78BB6903E27A8CA3A8434C9E4AC87300575` | 未改 |

上游源码 commit 路径：`vcpkg/buildtrees/libjxl/src/v0.12.0-85f4156902.clean/tools/`。
算法版本常量见 `src/ssimulacra2.cc`（`kMetricVersion = 1`，对应 SSIMULACRA 2 算法 2.1；权重与
Cloudinary `ssimulacra2` v2.1 逐字一致）。**不是自研算法**：算法/权重/常量逐字保留，见
`docs/m8-encoder-params-and-ssimulacra2-design.md` C14。

## vendored 源修改记录（仅 include/接口路径适配，共 5 行）

`src/ssimulacra2.cc`：

| 行 | 原 | 改 |
|---|---|---|
| 25 | `#include "tools/ssimulacra2.h"` | `#include "ssimulacra2.h"` |
| 43 | `#include "tools/gauss_blur.h"` | `#include "gauss_blur.h"` |
| 44 | `#include "tools/no_memory_manager.h"` | `#include "no_memory_manager.h"` |

`src/gauss_blur.cc`：

| 行 | 原 | 改 |
|---|---|---|
| 6 | `#include "tools/gauss_blur.h"` | `#include "gauss_blur.h"` |
| 22 | `#define HWY_TARGET_INCLUDE "tools/gauss_blur.cc"` | `#define HWY_TARGET_INCLUDE "gauss_blur.cc"` |

理由：主构建 `CMakeLists` 只 glob `compat/*.cc`，官方 `.cc` 由 `compat/*_build.cc` 包装 TU
include；仓库内 vendored 布局是 `src/`（不是上游的 `tools/`），且 `tools/` 前缀路径在仓库内
不存在。**除以上 5 行路径/宏字面量外，两个 `.cc` 的公式、权重、常量、尺度、边界处理逐字未改**
（可用 `git diff`/哈希对照核验：原始 SHA 与适配后 SHA 的差异仅限这 5 行）。

## 兼容层（`compat/`）清单

官方源码依赖 libjxl **内部头**（Image3F/ImageBundle/enc_xyb/Status/内存管理器/Highway
targets）。vcpkg 的 libjxl 不安装这些内部件，因此本目录提供**只读兼容层**：只替换容器/状态/
内存/颜色变换等外围依赖；SSIMULACRA 2 的公式、权重、尺度、边界处理逐字保留在 vendored 源码内。
数学/常量类文件从 libjxl 逐字复制（BSD-3-Clause，保留原版权头）。

| 文件 | 替换的 libjxl 内部件 | 说明 |
|---|---|---|
| `compat/lib/jxl/compat_core.h` | `base/status.h`、`base/common.h`、`base/compiler_specific.h`、`base/data_parallel.h`、`memory_manager_internal.h`、`image.h`、`image_ops.h`、`image_bundle.h`、`color_encoding_internal.h`、`image_metadata.h` | 核心替身：Status/StatusOr/JXL_* 宏、ImageF/Image3F（连续 float 平面，行尾 16 float pad 供 Highway 满向量读）、ImageBundle（RGB/线性 sRGB/无 alpha）、ColorEncoding（仅 sRGB 族）、ThreadPool/RunOnPool（`pool==nullptr` 时串行）、AlignedMemory、RoundUpTo、`kDefaultIntensityTarget=255`。语义对齐官方，实现裁剪。 |
| `compat/lib/jxl/base/matrix_ops.h` | 同名官方文件 | **逐字复制**（BSD-3，保留版权头），供 `gauss_blur.cc` 的 IIR 系数求解。 |
| `compat/lib/jxl/cms/opsin_params.h` | 同名官方文件的**常量子集** | 逐字保留 XYB opsin 吸光矩阵与偏置；未收录编码器色域脚手架常量（本实现用不到）。 |
| `compat/lib/jxl/enc_xyb.h` + `compat/enc_xyb_impl.cc` | `enc_xyb.h/.cc` 的 ToXYB | 实现**契约路径**：LinearSRGB→XYB。公式/常量逐字取自 `enc_xyb.cc` 的 `LinearRGBToXYB/LinearSRGBToXYB/ComputePremulAbsorb` 与 `base/fast_math-inl.h` 的 `CubeRootAndAdd`（6 ulp 快速 cbrt）；标量实现用 `std::fma` 复现 AVX2 上 `MulAdd/NegMulAdd` 的单次舍入，与官方 AVX2 路径逐位一致。sRGB gamma/任意 ICC 路径不在此实现（由 `ImageBundle::TransformTo` 精确 sRGB EOTF 先行线性化；其他编码返回错误，绝不静默错算）。 |
| `compat/jxl/cms.h` | `<jxl/cms.h>`（官方 `JxlGetDefaultCms` 属独立 `jxl_cms` 库，pp_core 不链接） | 返回空 `JxlCmsInterface` 占位；契约路径不做 CMS 变换，不使用其函数指针。 |
| `compat/no_memory_manager.{h,cc}` | `tools/no_memory_manager.h/.cc` | **逐字复制**（BSD-3）：`malloc/free` 的 `JxlMemoryManager`。 |
| `compat/lib/jxl/{base/*,image*,image_bundle,color_encoding_internal,memory_manager_internal}.h` | 对应官方头 | 每个 1–2 行转发到 `compat_core.h`（让官方 `#include "lib/jxl/..."` 原样解析）。 |
| `compat/ssimulacra2_build.cc` | — | 包装 TU：`#include "../src/ssimulacra2.cc"` + 第三方告警隔离（`#pragma warning(push,0)`）。 |
| `compat/gauss_blur_build.cc` | — | 包装 TU：`#include "../src/gauss_blur.cc"` + 告警隔离。Highway multi-target 机制要求独立 TU。 |

### 兼容层已知限制（如实）

- `ImageBundle::AlphaBlend`/`black`（CMYK）路径：兼容层 `HasAlpha()==false`、`black()==nullptr`；
  若未来传入带 alpha/black 的 bundle，会被忽略/返回错误而不是静默错算（当前契约只有 RGB）。
- `TransformTo` 只支持 sRGB gamma → linear sRGB（标准分段 EOTF）；其他编码返回失败。
- ToXYB 只支持线性 sRGB 输入（契约 `src/core/metric/metric.h` 明确要求输入已是 sRGB 线性）。
- `RunOnPool` 在 `pool==nullptr` 时串行执行（评分路径不建线程池；与官方默认单线程兜底一致）。
- 无全局可变状态（除 Highway 自身的运行期 target 选择，其为只读初始化）：`compute()` 线程安全。

## 构建

- 主构建 `CMakeLists`（C0 已接入）：`file(GLOB_RECURSE ... third_party/ssimulacra2/compat/*.cc)`
  收编到 `pp_core`；include 面 `third_party/ssimulacra2{,/src,/compat}`。
- 官方源**不直接**参与 glob，由 `compat/ssimulacra2_build.cc` / `compat/gauss_blur_build.cc`
  包装 TU 编译（否则 `tools/` 前缀 include 在 compat 就绪前会让全树构建失败）。
- 命令（经 `tools/env.py` 注入工具链）：

```bash
python tools/env.py run -- cmake --preset release-dev -B build/m8/c1 -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m8/c1 --target pp_test_quality_metric
python tools/env.py run -- ctest --test-dir build/m8/c1 -R test_quality_metric
```

- 不允许新增 vcpkg 依赖 / 不改 overlay / 不升级库（C14）。实现依赖的 Highway（`hwy.lib`）由
  既有 `libjpeg-turbo::jpegli-static` 的 INTERFACE 链接面带入；hwy 头由既有 libjxl
  pkg-config include 面提供，**未新增任何依赖**。

## 数值对照（M8-C1 实测）

独立 oracle = **libjxl v0.12.0 官方源码 + vcpkg buildtree 预编译内部件**（官方
`tools/ssimulacra2.cc`/`gauss_blur.cc` 编译进 oracle 程序，链接官方 `jxl-internal.lib` 与
`jxl_gauss_blur.lib`），输入与 `pp::metric::compute()` **同一份 sRGB 线性 float32 像素**：

| pair | 场景 | 官方部件 | compat 实现 | \|Δ\| |
|---|---|---|---|---|
| p1 | 8-bit RGB / 5-bit 量化 | 86.14186080 | 86.14186080 | 0 |
| p2 | 8-bit RGB / JPEG q35 解码像素 | 48.90683146 | 48.90683146 | 0 |
| p3 | 8-bit 灰度 / ±6 级噪声（复制 RGB） | 69.98137253 | 69.98137253 | 0 |
| p4 | 16-bit 灰度 / 12-bit 量化（复制 RGB） | 96.76655118 | 96.76655118 | 0 |
| p5 | 8-bit RGB / GaussianBlur r=1.8 | 40.24946500 | 40.24946500 | 0 |
| p6 | 8-bit RGB / JXL distance=1.5 | 73.58207822 | 73.58207822 | 0 |

6/6 对 \|Δ\|=0（打印 8 位小数逐位相同），远优于设计容差 0.1。命令/数据/日志见
`build/m8/c1/oracle.log`、`build/m8/c1/oracle/`。PyPI `ssimulacra2==0.3.0` 因离线不可得
（C0 安装挂起后由 Lead 取消），以“官方源码 + 官方预编译内部件”作为 fallback oracle（见
`docs/m8-report.md` 口径）。

## 性能与内存（M8-C1 实测，Windows/MSVC AVX2，单线程，本机 i7-14700K/32GiB）

| 尺寸 | 耗时 | ms/MP | compute 峰值 RSS 增量 | 每像素额外内存 |
|---|---|---|---|---|
| 1MP (1024×1024) | 140–143 ms | 134–136 | ≈150 MB | ≈143 B/px |
| 4MP (2048×2048) | 636–663 ms | 152–158 | ≈597 MB | ≈142 B/px |
| 12MP (4096×3072) | 2024–2052 ms | 161–163 | ≈1784 MB | ≈142 B/px |

- 以上为 3 次稳定复测区间；并行队友构建期间曾出现单次 175–217 ms/MP 的噪声样本（CPU 争用，
  峰值内存不变），复测已恢复稳定。
- ms/MP 落在设计预估 120–245 ms/MP 区间；单输出墙钟随并行 workers 摊薄。
- **内存未达 C10 目标**：官方算法在 1:1 尺度需物化多幅全分辨率临时图，实测 ≈142 B/px，约
  为 `2×frame` 预算（RGB float32 = 24 B/px）的 5.9 倍。C1 按任务卡“允许内部物化，但必须报告”
  如实记录；流式/分块（按行带 + blur halo、逐带累加 108 范数）列为 P2 优化建议，未在 C1 实施。
- 实测方法：流式行回调输入（不物化整幅测试图，峰值只归因 `compute()`），进程
  `PeakWorkingSetSize` 增量；详见 `build/m8/c1/perf.log`。

## 许可

- vendored 源码 + 兼容层复制文件：BSD-3-Clause（JPEG XL Project，见 `LICENSE`/`PATENTS`）。
- 关于页与发行包需登记 SSIMULACRA 2（libjxl BSD-3 + PATENTS 专利授权说明）。
