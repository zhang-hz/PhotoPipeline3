// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — system_clock → file_time_type 转换（M3-T11 v2.1 裁定）
//
// C++20 的 file_clock 允许实现"只提供 to_sys/from_sys"或"只提供 to_utc/from_utc"
// 两套之一（[time.clock.file]），且 clock_cast 并非所有实现都受理 file_clock 目标：
//   * libstdc++（GCC 13）：提供 from_sys/to_sys（Linux CI 已验证）
//   * MSVC STL（LWG 3694）：只提供 from_utc/to_utc，clock_cast<file_time_type> 编译失败
//     （本机 14.51 探针实证 C2672：约束不满足）
// 这里用 requires 在**实例化期**择一，避免绑定某个 STL 版本（Windows CI runner 与
// 本机 MSVC 版本不同，硬编码任一侧都有构建失败风险）。
#pragma once

#include <chrono>
#include <filesystem>

namespace pp {

// FileClock / UtcClock 均作为模板参数，使两个分支的成员查找都依赖模板参数——非依赖
// 名字会在定义期即被查找，那样另一侧不存在的成员会直接编译失败（if constexpr 的
// 丢弃语义只在模板中生效）。
template <class FileClock, class UtcClock = std::chrono::utc_clock>
std::filesystem::file_time_type file_time_from_sys(std::chrono::system_clock::time_point sys) {
    if constexpr (requires { FileClock::from_sys(sys); }) {
        return FileClock::from_sys(sys);
    } else if constexpr (requires { FileClock::from_utc(UtcClock::from_sys(sys)); }) {
        return FileClock::from_utc(UtcClock::from_sys(sys));
    } else {
        static_assert(sizeof(FileClock) == 0,
                      "file_clock 既不提供 from_sys 也不提供 from_utc：无法转换");
    }
}

}  // namespace pp
