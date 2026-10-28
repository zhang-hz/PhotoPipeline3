// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// M8-C1 兼容层：逐字复制自 libjxl v0.12.0 `tools/no_memory_manager.h`。

#ifndef TOOLS_NO_MEMORY_MANAGER_H_
#define TOOLS_NO_MEMORY_MANAGER_H_

#include <jxl/memory_manager.h>

namespace jpegxl {
namespace tools {

JxlMemoryManager* NoMemoryManager();

}  // namespace tools
}  // namespace jpegxl

#endif  // TOOLS_NO_MEMORY_MANAGER_H_
