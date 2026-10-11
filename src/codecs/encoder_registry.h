// PP-FROZEN(interface): 内部注册表（T6 创建；T7 只 include + 用宏，不得改动本文件）
//   M7-WP2 加性追认（R-110/R-299，登记 build/m7-wp2/handoff.md 裁定登记）：
//   ① `register_encoder_or_report` + 宏内失败分支（旧宏丢弃 register_encoder 返回值，
//      重复注册/空参静默）；② backend_id 空串 tie-break 语义在 encoders.cpp 显式化。
//   既有声明签名逐字不变。
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "codecs/encoder.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

using EncoderFactory = std::unique_ptr<IEncoder> (*)();

// 注册；同一 (format, backend) 重复注册 → 返回 false 且不覆盖先注册者
bool register_encoder(std::string_view format_id, std::string_view backend_id, EncoderFactory f);

// backend_id 为空 → 按 encoders.cpp 的显式 tie-break 解析（静态表首选后端优先）；
// 未注册 → nullptr
std::unique_ptr<IEncoder> create_registered_encoder(std::string_view format_id,
                                                    std::string_view backend_id);
std::vector<std::string> registered_backends(std::string_view format_id);

// R-110/R-299（M7-WP2 加性）：静态注册的失败不再静默 —— 失败（重复注册 / 空 format_id /
// 空工厂）时打 stderr 并 assert（release 下 assert 为 no-op，但 stderr 行恒在）。
// 头文件内 inline 承载（本波禁新增 .cpp 编译单元）。
inline bool register_encoder_or_report(std::string_view format_id, std::string_view backend_id,
                                       EncoderFactory f) {
    const bool ok = register_encoder(format_id, backend_id, f);
    if (!ok) {
        std::fprintf(stderr, "pp: encoder registration FAILED for '%.*s'/'%.*s'\n",
                     static_cast<int>(format_id.size()), format_id.data(),
                     static_cast<int>(backend_id.size()), backend_id.data());
        assert(ok && "PP_REGISTER_ENCODER: duplicate or invalid registration");
    }
    return ok;
}

// 静态注册助手（.cpp 文件作用域使用；fn 形如 std::unique_ptr<IEncoder> make_xxx()）
// （R-110 修正：旧宏把 register_encoder 的返回值丢弃 → 失败分支静默；现捕获并上报。）
#define PP_REGISTER_ENCODER(format_id, backend_id, fn)                                             \
    namespace {                                                                                    \
    const bool pp_reg_##fn = ::pp::register_encoder_or_report((format_id), (backend_id), &(fn));   \
    }

} // namespace pp
