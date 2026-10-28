// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// M8-C1 兼容层：逐字复制自 libjxl v0.12.0 `tools/no_memory_manager.cc`。

#include "no_memory_manager.h"

#include <jxl/memory_manager.h>

#include <cstdlib>

namespace jpegxl {
namespace tools {

namespace {
void* ToolsAlloc(void* /* opaque*/, size_t size) { return malloc(size); }
void ToolsFree(void* /* opaque*/, void* address) { free(address); }
JxlMemoryManager kNoMemoryManager{nullptr, &ToolsAlloc, &ToolsFree};
}  // namespace

JxlMemoryManager* NoMemoryManager() { return &kNoMemoryManager; };

}  // namespace tools
}  // namespace jpegxl
