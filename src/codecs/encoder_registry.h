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
