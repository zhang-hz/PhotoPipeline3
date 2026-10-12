// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/error_text.h：ErrorKind → 中文文案单源表 + 引擎英文错误串的中文 lookup
// （M7-R2B 新增；消费方 = UI 模板预检/失败原因行，R2D2 接线）
//
// 契约（可检索）：docs/deep-review-0.3.2.md R-070（错误文案透传英文原文：模板预检直接返回
// validate_output_template 英文串、运行页失败原因行原样拼引擎英文 + 本地路径，与全中文 UI
// 矛盾；修复口径 =「错误分类映射中文，原始串进 tooltip/日志」）与 R-273（错误文案通道分裂）。
// 本头只提供**中文文案查表**，不改引擎错误串本身 —— 引擎串是日志/回归基线的事实面，逐字不动。
// 覆盖面（R-070/273 场景）：① 模板预检 = validate_output_template 全部 8 条；② 输出名/输出根
// 校验 = validate_output_filename / probe_output_root；③ 失败原因 = error_kind_of 的同一前缀族
// （probe/decode/orient/color/flatten/encode/metadata/io/cancel/internal/config）。
// 未命中 → 空串：调用方回退显示引擎原文，**不得编造文案**（诚实显示口径）。
#pragma once
#include "core/pipeline.h" // ErrorKind（M7-WP8 加性枚举）
#include <string>
#include <string_view>

namespace pp {

// —— ErrorKind → 中文分类名（单源表；UI 状态/失败原因行的分类前缀）——
inline std::string_view error_kind_text(ErrorKind k) {
    switch (k) {
    case ErrorKind::None:
        return "无错误";
    case ErrorKind::Config:
        return "配置错误";
    case ErrorKind::Probe:
        return "探测失败";
    case ErrorKind::Decode:
        return "解码失败";
    case ErrorKind::Orient:
        return "方向处理失败";
    case ErrorKind::Color:
        return "色彩转换失败";
    case ErrorKind::Flatten:
        return "透明合成失败";
    case ErrorKind::Encode:
        return "编码失败";
    case ErrorKind::MetaWrite:
        return "元数据写入失败";
    case ErrorKind::Io:
        return "输出 I/O 失败";
    case ErrorKind::Cancelled:
        return "已取消";
    case ErrorKind::Internal:
        return "内部错误";
    case ErrorKind::Unknown:
        return "未知错误";
    }
    return "未知错误";
}

// —— 引擎英文错误串 → 中文用户文案 ——
// 查找次序：① 动态行特例（保留参数段）→ ② 精确表 → ③ 前缀族（分类级中文）→ 空串（回退原文）。
inline std::string error_text_zh(std::string_view engine_error) {
    if (engine_error.empty())
        return {};

    // ① 动态行特例：unknown symbol '$x' 保留符号名（R-070 模板预检场景）。
    constexpr std::string_view kUnknownSym = "output template: unknown symbol ";
    if (engine_error.rfind(kUnknownSym, 0) == 0)
        return "路径模板：未知符号 " + std::string(engine_error.substr(kUnknownSym.size()));

    // ② 精确表（文案与 UI 现文案同口径：page_output 的「路径模板不能为空」等）。
    struct Exact {
        std::string_view en;
        std::string_view zh;
    };
    static constexpr Exact kExact[] = {
        // —— 模板预检（validate_output_template 全 8 条；R-070 场景 1）——
        {"output template: template is empty", "路径模板不能为空"},
        {"output template: absolute paths are not allowed", "路径模板不允许绝对路径"},
        {"output template: '\\' separators are not allowed (use '/')",
         "路径模板不允许反斜杠分隔符（请用 /）"},
        {"output template: drive-qualified paths are not allowed", "路径模板不允许带盘符的路径"},
        {"output template: '..' is not allowed", "路径模板不允许 .. 路径段"},
        {"output template: '$' is not followed by a symbol name", "路径模板：$ 后必须跟符号名"},
        {"output template: last segment must produce a file name (use $file/$name/$ext)",
         "路径模板末段必须产出文件名（用 $file/$name/$ext）"},
        // —— 输出名校验（validate_output_filename；长度硬线已由 R-007 解除）——
        {"output path is empty", "输出路径为空"},
        // —— 输出根预检（probe_output_root）——
        {"output root is empty", "输出根目录为空"},
        // —— 配置面（validate_run_config / validate_params / validate_preset）——
        {"preset has no outputs", "预设没有输出"},
    };
    for (const Exact &e : kExact)
        if (engine_error == e.en)
            return std::string(e.zh);

    // ③ 前缀族（分类级中文；动态段（路径/键名/数值）原文进 tooltip/日志 —— R-070 口径）。
    // 前缀集合与 pipeline.cpp `error_kind_of` 的判据同源（R-176 单源前缀），test_scheduler_contract
    // 钉住两侧覆盖一致。
    struct Pref {
        std::string_view en;
        std::string_view zh;
    };
    static constexpr Pref kPrefix[] = {
        // 失败原因族（R-070 场景 2：运行页「└ 原因：%1」）
        {"probe failed:", "探测失败"},
        {"decode failed:", "解码失败"},
        {"orient failed:", "方向处理失败"},
        {"color transform failed:", "色彩转换失败"},
        {"flatten failed:", "透明合成失败"},
        {"encode failed:", "编码失败"},
        {"metadata write failed:", "元数据写入失败"},
        {"metadata-only rewrite failed:", "仅元数据重写失败"},
        {"source metadata unavailable", "源元数据不可读"},
        {"output path", "输出路径失败"},
        {"cannot create output directory", "无法创建输出目录"},
        {"pixel budget exceeded", "像素预算超限"},
        {"encode cancelled", "编码已取消"},
        {"unexpected exception", "内部异常"},
        {"unexpected non-standard exception", "内部异常"},
        // 配置面族
        {"no output configured", "未配置输出"},
        {"run_one_file must not be called", "运行入口不匹配（仅元数据模式须走 run_metadata_only）"},
        {"metadata-only mode requires", "仅元数据模式要求恰好一个输出"},
        {"unknown output format", "未知输出格式"},
        {"unknown format", "未知格式"},
        {"unknown backend", "未知编码后端"},
        {"unknown tech", "未知编码技术"},
        {"no encoder registered", "未注册编码器"},
        {"output bit depth", "输出位深不受该格式支持"},
        {"bitdepth ", "位深不受该格式支持"},
        {"unsupported preset version", "预设版本不受支持"},
        // 参数/模板/文件名族（分类级）
        {"param '", "参数校验失败（类型或取值非法）"},
        {"output template:", "路径模板校验失败"},
        {"output path component", "输出路径段不合法"},
        {"output path has no file name", "输出路径缺文件名"},
        {"output root is not", "输出根目录不可用"},
        {"atomic write:", "输出写入失败"},
        {"atomic commit:", "输出写入失败"},
        {"exiv2:", "元数据读写失败"},
    };
    for (const Pref &p : kPrefix)
        if (engine_error.rfind(p.en, 0) == 0)
            return std::string(p.zh);

    return {}; // 未命中：调用方回退显示引擎原文（不编造文案）
}

} // namespace pp
