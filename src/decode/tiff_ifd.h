// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — TIFF IFD 结构探针（0.3.6-alpha1 加性新文件；header-only）
//
// 目的：给 decode 层的多页判定补一条 **TIFF 规范**依据 —— IFD1 的 NewSubfileType
// （TIFFTAG_SUBFILETYPE = 254）bit 0 = 1 表示「reduced-resolution 缩略图」，不是第二页。
// 相机/Photoshop 产出的普通 TIFF 普遍带这样一条 107x160 的 IFD1；没有这条判定时
// probe_file 会把每条这类 TIFF 都报成 "multipage or animated input"（实测
// super/T1.tif = _DSF3043.tif：IFD0 3902x5854，IFD1 107x160, NewSubfileType=1）。
//
// 为什么不用尺寸启发式：真多页 TIFF 的页尺寸可以更小（tests/golden/base/multi.tif 是
// 64x64 + 32x32 的合法多页夹具），按「第二目录更小」判缩略图会把它误判。NewSubfileType
// 是 TIFF 6.0 为此定义的标准标记，尺寸不参与判断。
//
// 只读经典 TIFF（magic 42）；BigTIFF（magic 43）不解析 → false（保守：仍按多页处理）。
//
// header-only：本文件不新增编译单元（避免 CONFIGURE_DEPENDS 触发整树重配置），
// 消费点 = decode/oiio_reader.cpp 与 tests/unit/test_decode.cpp。
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace pp {
namespace tiff_ifd_detail {

constexpr int kTagSubfileType = 254; // TIFFTAG_SUBFILETYPE
constexpr std::uint16_t kTypeShort = 3;
constexpr std::uint16_t kTypeLong = 4;
// 单目录条目数上限：合法 TIFF 远小于此；超限视为非 TIFF/损坏（防病态 count 触发大分配）。
constexpr std::uint16_t kMaxEntries = 4096;

class Reader {
public:
    explicit Reader(const std::filesystem::path &p) : f_(p, std::ios::binary) {
        if (!f_) {
            return;
        }
        f_.seekg(0, std::ios::end);
        const std::streamoff n = f_.tellg();
        if (n < 0) {
            return;
        }
        size_ = static_cast<std::uint64_t>(n);
        open_ = true;
    }
    bool open() const { return open_; }
    std::uint64_t size() const { return size_; }

    bool read_at(std::uint64_t off, void *dst, std::size_t n) {
        if (off > size_ || n > size_ - off) {
            return false;
        }
        f_.clear();
        f_.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        f_.read(static_cast<char *>(dst), static_cast<std::streamsize>(n));
        return static_cast<std::size_t>(f_.gcount()) == n;
    }

private:
    std::ifstream f_;
    std::uint64_t size_ = 0;
    bool open_ = false;
};

inline std::uint16_t u16(const unsigned char *b, bool little) {
    return little ? static_cast<std::uint16_t>(b[0] | (b[1] << 8))
                  : static_cast<std::uint16_t>((b[0] << 8) | b[1]);
}

inline std::uint32_t u32(const unsigned char *b, bool little) {
    return little ? (static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
                     (static_cast<std::uint32_t>(b[2]) << 16) |
                     (static_cast<std::uint32_t>(b[3]) << 24))
                  : ((static_cast<std::uint32_t>(b[0]) << 24) |
                     (static_cast<std::uint32_t>(b[1]) << 16) |
                     (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]));
}

// 读一个 IFD 的 next 偏移；可选地输出「tag 254 的原始标量值」（未命中 has=false）。
inline bool read_ifd(Reader &r, std::uint64_t off, bool little, std::uint32_t &next,
                     bool *has_subfiletype, std::uint32_t *subfiletype) {
    unsigned char nbuf[2];
    if (!r.read_at(off, nbuf, sizeof(nbuf))) {
        return false;
    }
    const std::uint16_t n = u16(nbuf, little);
    if (n > kMaxEntries) {
        return false;
    }
    const std::uint64_t entries_end = off + 2 + static_cast<std::uint64_t>(n) * 12;
    if (entries_end + 4 > r.size()) {
        return false;
    }
    if (has_subfiletype != nullptr) {
        *has_subfiletype = false;
    }
    for (std::uint16_t i = 0; i < n; ++i) {
        unsigned char e[12];
        if (!r.read_at(off + 2 + static_cast<std::uint64_t>(i) * 12, e, sizeof(e))) {
            return false;
        }
        const std::uint16_t tag = u16(e, little);
        if (tag != kTagSubfileType || has_subfiletype == nullptr) {
            continue;
        }
        const std::uint16_t type = u16(e + 2, little);
        const std::uint32_t count = u32(e + 4, little);
        if (count < 1) {
            continue;
        }
        if (type == kTypeShort) {
            *subfiletype = u16(e + 8, little);
            *has_subfiletype = true;
        } else if (type == kTypeLong) {
            *subfiletype = u32(e + 8, little);
            *has_subfiletype = true;
        }
    }
    unsigned char obuf[4];
    if (!r.read_at(entries_end, obuf, sizeof(obuf))) {
        return false;
    }
    next = u32(obuf, little);
    return true;
}

} // namespace tiff_ifd_detail

// IFD1 的 NewSubfileType bit 0 == 1 → true。非 TIFF / 打不开 / 无 IFD1 / 结构越界 → false。
inline bool tiff_ifd1_is_reduced_thumbnail(const std::filesystem::path &path) {
    using namespace tiff_ifd_detail;
    Reader r(path);
    if (!r.open()) {
        return false;
    }
    unsigned char hdr[8];
    if (!r.read_at(0, hdr, sizeof(hdr))) {
        return false;
    }
    bool little = false;
    if (hdr[0] == 'I' && hdr[1] == 'I') {
        little = true;
    } else if (hdr[0] == 'M' && hdr[1] == 'M') {
        little = false;
    } else {
        return false;
    }
    if (u16(hdr + 2, little) != 42) { // 经典 TIFF；BigTIFF(43) 不解析
        return false;
    }
    const std::uint32_t ifd0 = u32(hdr + 4, little);
    if (ifd0 == 0) {
        return false;
    }
    std::uint32_t ifd1 = 0;
    if (!read_ifd(r, ifd0, little, ifd1, nullptr, nullptr) || ifd1 == 0) {
        return false;
    }
    bool has = false;
    std::uint32_t value = 0;
    std::uint32_t next = 0;
    if (!read_ifd(r, ifd1, little, next, &has, &value)) {
        return false;
    }
    return has && (value & 1u) != 0;
}

} // namespace pp
