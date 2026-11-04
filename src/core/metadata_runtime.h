// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — Exiv2 运行时保护（0.4.2 修复批）。
//
// 背景（用户实测闪退 2026-11-04：导入含 XMP 的 HEIF 后 exiv2.dll 0xC0000005）：
//   * Exiv2 0.28.8 的 `XmpParser::initialize()`（vcpkg buildtrees exiv2 v0.28.8 src/xmp.cpp:512）
//     用**裸 static bool + 无锁**懒初始化；`XmpParser::decode()`（src/xmp.cpp:666/798）在未初始化
//     时自动调用它。Adobe XMP Toolkit 的 SXMPMeta::Initialize/Terminate 要求单线程调用。
//   * GUI 导入时缩略图池（2 线程）/ 预览池（2 线程）/ 元数据探针池（QThreadPool）会并发首次
//     读取含 XMP 的文件（HEIF/JPEG…）⇒ 多个线程可能同时进入 initialize() ⇒ Adobe XMP 内部
//     XML 解析器状态（std::map/XML_Node）未初始化完成 → 空指针崩溃（崩溃反汇编落在
//     `XML_Node`/`std::map<int,std::string>` 相关内部函数，与 Exiv2 源码行为吻合）。
//
// 本头提供**进程级单一防线**：
//   1) `ensure_exif_metadata_runtime()`：以 `std::call_once` 完成 XMP 运行时初始化（幂等、无锁
//      竞争；静态析构时 terminate，保证与 initialize 成对）。
//   2) `exif_metadata_mutex()`：Exiv2 调用的**串行化锁**（递归锁）。覆盖所有 Exiv2 I/O +
//      XMP/Exif 解析入口（metadata.cpp 读写路径、thumbs.cpp 内嵌预览、exif_editor 读取经
//      pp::read_metadata 的路径已天然覆盖）。串行化粒度 = 单次元数据读取/写入（毫秒级），
//      不涉及像素解码/编码热路径（缩略图/预览的内嵌预览尝试只在 Exiv2 段持锁）。
//
// 线程安全：两个函数均可从任意线程调用；锁为进程内递归互斥（同一线程可重入，便于
// read_metadata → HEIF fallback 等嵌套路径）。
#pragma once

#include <mutex>

namespace pp {

// 进程级 Exiv2/XMP 运行时初始化（幂等；首次调用后所有并发 Exiv2 调用安全）。
void ensure_exif_metadata_runtime();

// Exiv2 调用串行化锁（进程级单例；递归）。调用方在进入任何 Exiv2 I/O / XMP/Exif 解析之前
// `ensure_exif_metadata_runtime()` 再 `std::lock_guard<std::recursive_mutex>
// lock(exif_metadata_mutex());`。
std::recursive_mutex &exif_metadata_mutex();

} // namespace pp
