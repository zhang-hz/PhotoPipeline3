// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10-W1-T2c：评分色彩归一快速内核（目标空间编码值 → sRGB 线性）。
//
// 背景（docs/v0.4.0-design.md §16.1 D-1 / build/m10/w1-engine-a/report.md §3.3）：
// 评分色彩归一的真正成本 = 默认 keep 路径下**两次整帧 lcms**（22.8MP 每次≈3.34s，合计≈7s）；
// 按带化（band=64 行）只消每次调用的固定开销（row/band≈0.97，无提速）。本内核把
// "(目标空间 ICC 描述, 编码值) → sRGB 线性"换成**预计算 matrix-shaper 路线**
// （设计 §4.2.2(3) 选项 C：预计算变换矩阵 + 快速 TRC/EOTF 表）：
//   逐通道 TRC 解码（1D 表 4096 段 + 线性插值）→ 源线性 RGB → 3×3 色料矩阵（D50 PCS，
//   sRGB 色料矩阵之逆 × 源色料矩阵）→ clamp [0,1]（等价于精确路径 lcms 输出经 sRGB 编码
//   再精确 EOTF 的结果：sRGB 编码单调，EOTF(encode(x)) = x）。
//   内建 sRGB、AdobeRGB1998、本程序生成的 Display P3 全是 matrix-shaper 类型，实测分数差
//   ≤1e-4 量级（远低于 0.01 验收线）。
//   3D CLUT 三线性插值方案实测残差过大（色域边界 max|Δlinear|≈0.012/ΔE76≈1.2，常规有损
//   区间 SSIMULACRA2 分数差可达 0.07），已放弃——证据见 build/m10/w1-fastcolor/report.md §3。
//
// 契约（W1-T2c）：
//   * 输入 = 目标色彩空间的**编码值**（量化/反归一化后的 0..1 float）；输出 = sRGB **线性**；
//     越界输入 clamp 到 [0,1]（评分两侧的样本天然在 [0,1]）。
//   * 不支持/非法 ICC（LUT 型 A2B0/B2A0、非 RGB colorspace、缺 tag、解析失败）、构建失败、
//     被 `PP_FASTCOLOR=0` 禁用 → `get_shared()` 返回 nullptr，调用方（pipeline 评分行适配层）
//     回退精确 lcms；**回退不改评分参考/失真定义**。
//   * 进程级缓存按 ICC 字节 SHA1 复用（同批多文件/多输出只构建一次）；实例构建后不可变 →
//     多 worker 线程只读共享安全。
//   * 评分只读：本模块不触碰产物字节。
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pp {

// sRGB EOTF（IEC 61966-2-1 解扩展）：编码值 → 线性光。全仓库**单源**（评分精确路径/快速
// 路径、pipeline 的 sRGB 快路径共用同一公式）。入参按 [0,1] clamp。
float srgb_eotf(float v);

// 快速归一内核：目标空间（源 ICC 描述，编码值）→ sRGB 线性（matrix-shaper + TRC 表）。
class FastSrgbLinear {
public:
    // 进程级缓存入口：空 ICC（假定 sRGB，无需本内核）/不支持的 ICC/PP_FASTCOLOR=0
    // → nullptr（调用方回退精确 lcms）。构建失败也会被缓存（不反复重试）。
    static std::shared_ptr<const FastSrgbLinear> get_shared(const std::string &src_icc);

    bool ready() const { return ready_; }
    const char *engine() const { return "matrix-shaper-trc"; } // 日志/证据用
    double build_ms() const { return build_ms_; }              // 仅解析/预计算段

    // 原地变换 width×rows 交织 RGB：输入 = 目标空间编码值（越界 clamp），输出 = sRGB 线性。
    void transform_rows(float *rgb, int width, int rows) const;
    // 单像素版本（与 transform_rows 同一条实现；单测用）。
    void transform_pixel(float &r, float &g, float &b) const;

    // 直接构建（不经进程缓存；单测/证据用）：src_icc 必须是可解析的 matrix-shaper ICC。
    // 返回 nullptr + err（不抛异常）。grid 仅为签名兼容保留（本路线无网格）。
    static std::shared_ptr<const FastSrgbLinear> build(const std::string &src_icc, int grid,
                                                       std::string &err);

    struct Stats {
        std::uint64_t builds = 0; // 成功构建次数
        std::uint64_t build_fail = 0;
        std::uint64_t hits = 0;     // 缓存命中
        std::uint64_t disabled = 0; // PP_FASTCOLOR=0 或空 ICC 导致的未启用
    };
    static Stats stats();
    static void clear_cache(); // 单测用
    // PP_FASTCOLOR：0/off/false/no（大小写不敏感）→ true（强制精确路径；回退/A-B 证据用）。
    static bool forced_exact();

private:
    FastSrgbLinear() = default;
    // 源线性 RGB → sRGB 线性（行主序 3×3；源色料 → D50 PCS → sRGB 色料之逆）。
    std::array<double, 9> m_{};
    // 逐通道 TRC 解码表：编码值 [0,1] → 源线性值（kTrcSamples+1 点，线性插值）。
    // 存 double：float 存储会把 ULP 级量化噪声注入映射（分数对"相邻样本的映射差"敏感，
    // 平滑的映射误差无害、ULP 噪声有害；实测 double 表可显著减小分数抖动）。
    std::array<std::vector<double>, 3> trc_lut_;
    bool ready_ = false;
    double build_ms_ = 0.0;
};

} // namespace pp
