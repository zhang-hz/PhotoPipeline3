// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M3-W4a（v1.5）：单测侧 POSIX 环境/进程 API 薄垫层（单实现）。
// 语义对齐 POSIX：setenv(name, value) ≡ setenv(name, value, 1)、unsetenv(name)、getpid()。
// Windows/UCRT 注记：_putenv_s(name, "") 语义恰为删除变量（正合 unsetenv）；而 POSIX
// setenv(name, "", 1) 的"存在但为空"态经 UCRT 公开 API 不可表达——test_logger.cpp 的
// empty-value 用例按平台分派断言（见该处注释与探针记录）。logger 经 std::getenv（CRT）
// 读 PP_LOG_LEVEL，故垫层必须走 CRT 的 _putenv_s 才对被测代码可见。
#pragma once

#if defined(_WIN32)
#include <cstdlib>    // _putenv_s
#include <process.h>  // _getpid
#else
#include <unistd.h>
#endif

namespace pptest {

inline void setenv(const char* name, const char* value) {
#if defined(_WIN32)
    ::_putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

inline void unsetenv(const char* name) {
#if defined(_WIN32)
    ::_putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

inline long getpid() {
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

}  // namespace pptest
