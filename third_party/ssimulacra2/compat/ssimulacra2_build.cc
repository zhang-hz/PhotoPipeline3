// SPDX-License-Identifier: BSD-3-Clause
// PhotoPipeline M8-C1 —— vendored 官方源码的包装 TU（CMake 只 glob compat/*.cc）。
//
// 官方文件本身逐字保留（仅 include 行适配，见 third_party/ssimulacra2/README.md）；
// 此处只做“第三方代码告警隔离”，把官方代码的告警与本仓库 /W4 门禁隔离，
// 不改变任何编译语义或数值行为。
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#include "../src/ssimulacra2.cc"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
