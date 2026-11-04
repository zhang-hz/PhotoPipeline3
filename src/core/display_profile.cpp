// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 0.4.2 需求 5 落地：显示面色彩管理（display colour management）。
//
// Contract: docs/m14-tasks.md C4（用户需求 5：预览窗格「显示照片真正的色彩」，修复「色彩很灰」）；
//           PP-FROZEN 相关：本文件是**新增承载面**，不改任何既有签名/行为；消费点全部是末尾加性
//           追加（core/thumbs.h 的 PreviewImage 字段 + PreviewCache::set_display_profile，
//           ui/preview_panel.cpp / ui/thumbnails.cpp 的显示变换接线）。引擎/金样/回归零影响。
//
// 实现要点：
//   1) 系统显示 profile：Windows GetDC(NULL) + GetICMProfileW（主显示器 ICC），读文件并用 lcms2
//      校验（可解析 / RGB / 3 通道）；失败、空、其它平台 → sRGB 回退并**如实记录原因**。
//   2) 像素变换：lcms2 8-bit RGBA（TYPE_RGBA_8）源 → 显示 profile，相对比色 + 黑点补偿
//      （INTENT_RELATIVE_COLORIMETRIC + cmsFLAGS_BLACKPOINTCOMPENSATION，与 ColorManager 档位 0
//      同口径）。cmsFLAGS_NOCACHE 是**并发安全前提**：lcms2 的 1-pixel cache 是变换句柄上的可变
//      状态（lcms2.h: "Inhibit 1-pixel cache"），预览池的 2 个 worker 共享同一句柄时必须关掉它；
//      cmsFLAGS_COPY_ALPHA 让 opaque alpha（本管线预览恒 255）原样通过。
//   3) 输出 QImage 挂显示 profile 的 QColorSpace（语义如实：字节 = 显示编码值）。Qt 6.11 Windows
//      raster 后端不使用该信息（见头文件根因），所以**像素已经是正确的显示编码**——这正是修复。
//      若将来 Qt/平台支持窗口色彩管理，该 tag 使二次转换退化为 identity，不会二次变换。
//   4) 启动卡死根治的防御面（task-4）：system_display_profile() 是阻塞调用（系统调用 + 文件
//      I/O + lcms2 解析），GUI 线程启动期**禁止**同步调用（异步获取在 preview_panel.cpp）；
//      本文件加 ICC 读取大小上限（kDisplayIccMaxBytes）、失败如实日志、PP_DISPLAY_ICC_DELAY_MS
//      诊断模拟开关，以及 GUI 侧异步轮询共用的纯函数状态机 classify_display_profile_async()。
//
// 线程安全：system_display_profile()（进程缓存 + 互斥）与 to_display_encoded()（transform 缓存
// 互斥 + 句柄 NOCACHE）均可被预览池并发调用。

#include "core/display_profile.h"

#include <QByteArray>
#include <QColorSpace>
#include <QGuiApplication>
#include <QString>

#include <lcms2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "core/logger.h"

namespace pp {
namespace {

constexpr const char *kLogFile = "display_profile.cpp";

// 与 ColorManager 档位 0 同源（docs/m1-tasks.md §3.6 数值契约）：相对比色 + BPC。
constexpr int kIntent = INTENT_RELATIVE_COLORIMETRIC;
// NOCACHE：lcms2 的 1-pixel cache 是每 transform 句柄的可变状态 → 并发共享必须关（见文件头）。
// COPY_ALPHA：8bit RGBA 的 alpha（预览恒 255）原样复制，不做色彩变换。
constexpr cmsUInt32Number kTransformFlags =
    cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE;

// 显示变换缓存容量：真正会命中的组合极少（源 ICC × 显示 profile；显示 profile 进程内稳定），
// 4 条足够（预览池 2 worker 通常只用 1-2 条）。
constexpr std::size_t kTransformCacheCapacity = 4;

// 诊断开关 PP_DISPLAY_ICC_DELAY_MS（默认不设置；A/B 取证用，见 display_profile.h）：
// 在获取路径上 sleep 等长毫秒，模拟「某些 Windows 版本/环境下系统 profile 获取很慢」。
// 这是**诊断模拟**，不引入任何真实系统调用；非法值只记日志、不影响正常获取。
void maybe_apply_diagnostic_delay() {
    const char *raw = std::getenv("PP_DISPLAY_ICC_DELAY_MS");
    if (raw == nullptr || raw[0] == '\0')
        return;
    char *end = nullptr;
    const long long requested = std::strtoll(raw, &end, 10);
    if (end == raw || requested <= 0) {
        log_warn("display", kLogFile, "PP_DISPLAY_ICC_DELAY_MS is not a positive integer; ignored",
                 {{"value", std::string(raw)}});
        return;
    }
    const long long capped = std::min<long long>(requested, 60'000);
    log_info("display", kLogFile,
             "diagnostic PP_DISPLAY_ICC_DELAY_MS active (simulated slow system)",
             {{"delay_ms", std::to_string(capped)}});
    std::this_thread::sleep_for(std::chrono::milliseconds(capped));
}

std::string profile_description(cmsHPROFILE profile) {
    if (profile == nullptr)
        return {};
    std::array<char, 256> buffer{};
    const cmsUInt32Number n =
        cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", buffer.data(),
                               static_cast<cmsUInt32Number>(buffer.size()));
    std::string name = n > 0 ? std::string(buffer.data()) : std::string();
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\0'))
        name.pop_back();
    return name;
}

// 显示 profile 能否作为 lcms2 的 RGB 目的地使用（可解析、RGB 色彩空间、3 通道）。
bool profile_usable_as_display(cmsHPROFILE profile) {
    if (profile == nullptr || cmsGetColorSpace(profile) != cmsSigRgbData)
        return false;
    return cmsChannelsOf(cmsGetColorSpace(profile)) == 3;
}

DisplayProfile fallback_or_log(std::string reason) {
    DisplayProfile p = make_srgb_display_fallback(std::move(reason));
    log_warn("display", kLogFile, "display profile unavailable, falling back to sRGB",
             {{"reason", p.fallback_reason},
              {"edid_note", "Windows monitor ICC via GetICMProfileW(GetDC(NULL))"}});
    return p;
}

// 字节 → DisplayProfile（from_system=true 语义）。不可用 → 返回带 fallback_reason 的回退。
DisplayProfile profile_from_icc_bytes(const std::string &bytes, bool from_system) {
    if (bytes.empty())
        return fallback_or_log("display ICC is empty");
    cmsHPROFILE profile =
        cmsOpenProfileFromMem(bytes.data(), static_cast<cmsUInt32Number>(bytes.size()));
    if (profile == nullptr)
        return fallback_or_log("lcms2 cannot parse the system display ICC");
    if (!profile_usable_as_display(profile)) {
        cmsCloseProfile(profile);
        return fallback_or_log("system display ICC is not an RGB profile");
    }
    DisplayProfile out;
    out.icc = bytes;
    out.name = profile_description(profile);
    if (out.name.empty())
        out.name = "ICC";
    out.from_system = from_system;
    cmsCloseProfile(profile);
    return out;
}

DisplayProfile acquire_uncached() {
    // 诊断/取证开关（正常运行不设置；见头文件）：强制 sRGB 回退优先于强制 ICC，
    // 便于同机 A/B 出「修复前（关闭显示管理）」截图。不进渲染逻辑，只有取值入口。
    if (const char *force_fb = std::getenv("PP_DISPLAY_SRGB_FALLBACK");
        force_fb != nullptr && force_fb[0] != '\0' && std::strcmp(force_fb, "0") != 0) {
        return make_srgb_display_fallback("forced by PP_DISPLAY_SRGB_FALLBACK (diagnostic)");
    }
    // 诊断模拟：在真正获取系统 profile 之前插入可控延迟（PP_DISPLAY_ICC_DELAY_MS；默认不设置）。
    // 用于验证 GUI 线程异步化：延迟只在后台获取路径上，GUI 线程不等待（见 preview_panel.cpp）。
    maybe_apply_diagnostic_delay();
    if (const char *force_icc = std::getenv("PP_DISPLAY_ICC");
        force_icc != nullptr && force_icc[0] != '\0') {
        const DisplayIccFile file = read_display_icc_file(std::filesystem::path(force_icc));
        if (file.ok) {
            DisplayProfile p = profile_from_icc_bytes(file.bytes, /*from_system=*/true);
            if (p.from_system) {
                log_info("display", kLogFile, "display profile forced by PP_DISPLAY_ICC",
                         {{"path", std::string(force_icc)}, {"name", p.name}});
                return p;
            }
        } else {
            log_warn("display", kLogFile, "PP_DISPLAY_ICC is not readable; ignored",
                     {{"path", std::string(force_icc)}, {"error", file.error}});
        }
    }

#if defined(_WIN32)
    // GetDC(NULL) = 主显示器（Lead 口径；多显示器按主屏处理，登记为限制）。
    HDC dc = GetDC(nullptr);
    if (dc == nullptr)
        return fallback_or_log("GetDC(NULL) failed");
    std::array<wchar_t, MAX_PATH * 4> path{};
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL ok = GetICMProfileW(dc, &length, path.data());
    ReleaseDC(nullptr, dc);
    if (ok == FALSE || length == 0)
        return fallback_or_log("GetICMProfileW(primary display) failed");
    const DisplayIccFile file = read_display_icc_file(std::filesystem::path(path.data()));
    if (!file.ok)
        return fallback_or_log("cannot read the system display ICC file: " + file.error);
    DisplayProfile p = profile_from_icc_bytes(file.bytes, /*from_system=*/true);
    if (p.from_system) {
        log_info("display", kLogFile, "system display profile acquired (primary monitor)",
                 {{"name", p.name},
                  {"icc_bytes", std::to_string(p.icc.size())},
                  {"path_ish", "GetICMProfileW"}});
    }
    return p;
#else
    // Linux/macOS：不引入 colord / ColorSync 依赖（CI 风险与依赖预算），回退 sRGB 并登记。
    return fallback_or_log(
        "no system display-profile reader on this platform "
        "(Linux/macOS: colord/ColorSync not integrated by design; sRGB fallback)");
#endif
}

// ---------------------------------------------------------------------------------------------
// 进程级显示 profile 缓存（互斥；invalidate 后下次重新获取）
// ---------------------------------------------------------------------------------------------

struct ProfileCache {
    std::mutex mu;
    bool has = false;
    DisplayProfile profile;
};

ProfileCache &profile_cache() {
    static ProfileCache cache;
    return cache;
}

// ---------------------------------------------------------------------------------------------
// lcms2 transform 缓存（(源 ICC, 显示 ICC) → 句柄池）
// ---------------------------------------------------------------------------------------------
// 句柄池（每线程独立 cmsHTRANSFORM + 原子位图租借）而非单句柄共享 —— 实测依据（报告登记）：
//   * lcms2 的 cmsDoTransform 是单线程设计；同一句柄被多线程并发调用会严重退化（内部 1-pixel
//     cache 等可变状态 + 内部锁竞争），实测「4 线程共享句柄」比单线程还慢；
//     cmsFLAGS_NOCACHE 只能关 1-pixel cache，并不把句柄变成并发安全的只读对象。
//   * 正确形态 = **每线程一个句柄**（lcms2 官方线程模型）；句柄创建后只读，可被任意线程使用。
//   * 池按需创建（≤kHandlePoolSize = 2 并发变换 × 4 线程；预览池并发 ≤2），
//     创建开销实测 ~1.8ms/句柄（仅首个 4096² 变换付一次）。
constexpr unsigned kMaxTransformThreads = 4;
constexpr unsigned kHandlePoolSize = 2 * kMaxTransformThreads;

struct XfEntry {
    std::string key_;                  // 缓存键（逐出时反查索引；创建后不变）
    cmsHPROFILE src_profile = nullptr; // 保活：按需建更多句柄
    cmsHPROFILE dst_profile = nullptr;
    cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE;
    bool alpha_copied = true; // false = COPY_ALPHA 建失败（退化路径：变换后补 alpha=255）
    mutable std::mutex mu;
    std::array<cmsHTRANSFORM, kHandlePoolSize> handles{};
    std::uint32_t leased = 0; // 位图：已租出的句柄下标

    ~XfEntry() {
        for (cmsHTRANSFORM xf : handles) {
            if (xf != nullptr)
                cmsDeleteTransform(xf);
        }
        if (src_profile != nullptr)
            cmsCloseProfile(src_profile);
        if (dst_profile != nullptr)
            cmsCloseProfile(dst_profile);
    }

    // 租借 n 个**已创建且空闲**的句柄（不足时按需创建）；池耗尽/创建失败 → false。
    bool lease(unsigned n, std::array<unsigned, kMaxTransformThreads> &out) {
        if (n == 0 || n > kMaxTransformThreads)
            return false;
        const std::lock_guard<std::mutex> lock(mu);
        const auto free_count = [this] {
            unsigned count = 0;
            for (unsigned i = 0; i < kHandlePoolSize; ++i) {
                if (handles[i] != nullptr && (leased & (1u << i)) == 0)
                    ++count;
            }
            return count;
        };
        while (free_count() < n) {
            unsigned slot = kHandlePoolSize;
            for (unsigned i = 0; i < kHandlePoolSize; ++i) {
                if (handles[i] == nullptr) {
                    slot = i;
                    break;
                }
            }
            if (slot == kHandlePoolSize)
                return false; // 池已满（全部已创建）且空闲不足
            cmsHTRANSFORM xf = cmsCreateTransform(src_profile, TYPE_RGBA_8, dst_profile,
                                                  TYPE_RGBA_8, kIntent, flags);
            if (xf == nullptr)
                return false;
            handles[slot] = xf;
        }
        unsigned count = 0;
        for (unsigned i = 0; i < kHandlePoolSize && count < n; ++i) {
            if (handles[i] != nullptr && (leased & (1u << i)) == 0) {
                out[count++] = i;
                leased |= (1u << i);
            }
        }
        return count == n;
    }

    void release(const std::array<unsigned, kMaxTransformThreads> &idx, unsigned n) {
        const std::lock_guard<std::mutex> lock(mu);
        for (unsigned i = 0; i < n; ++i)
            leased &= ~(1u << idx[i]);
    }
};

class TransformCache {
public:
    std::shared_ptr<XfEntry> get(const std::string &key) {
        const std::lock_guard<std::mutex> lock(mu_);
        const auto it = index_.find(key);
        if (it == index_.end())
            return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second);
        return *it->second;
    }

    void put(const std::string &key, std::shared_ptr<XfEntry> entry) {
        const std::lock_guard<std::mutex> lock(mu_);
        if (const auto it = index_.find(key); it != index_.end()) {
            lru_.erase(it->second);
            index_.erase(it);
        }
        entry->key_ = key;
        lru_.push_front(std::move(entry));
        index_[key] = lru_.begin();
        while (lru_.size() > kTransformCacheCapacity) {
            const auto last = std::prev(lru_.end());
            index_.erase((*last)->key_);
            lru_.erase(last);
        }
    }

private:
    mutable std::mutex mu_;
    std::list<std::shared_ptr<XfEntry>> lru_;
    std::unordered_map<std::string, std::list<std::shared_ptr<XfEntry>>::iterator> index_;
};

TransformCache &transform_cache() {
    static TransformCache cache;
    return cache;
}

cmsHPROFILE open_profile(const std::string &bytes) {
    return cmsOpenProfileFromMem(bytes.data(), static_cast<cmsUInt32Number>(bytes.size()));
}

// 源 profile 归一：空 → 内建 sRGB（假定 sRGB）；不可解析 / 非 RGB → 假定 sRGB（降级记录）。
struct SrcProfile {
    cmsHPROFILE profile = nullptr;
    std::string name;
    bool assumed = false;
};

SrcProfile open_source_profile(const std::string &src_icc, std::string *degrade_note) {
    SrcProfile src;
    if (!src_icc.empty()) {
        cmsHPROFILE profile = open_profile(src_icc);
        if (profile != nullptr && cmsGetColorSpace(profile) == cmsSigRgbData &&
            cmsChannelsOf(cmsGetColorSpace(profile)) == 3) {
            src.profile = profile;
            src.name = profile_description(profile);
            if (src.name.empty())
                src.name = "ICC";
            src.name = "ICC(" + src.name + ")";
            return src;
        }
        if (profile != nullptr)
            cmsCloseProfile(profile);
        if (degrade_note != nullptr) {
            *degrade_note =
                "source ICC is not a usable RGB profile; assumed sRGB for the display transform";
        }
    }
    src.profile = cmsCreate_sRGBProfile();
    src.name = "assumed sRGB";
    src.assumed = true;
    return src;
}

cmsHPROFILE open_display_profile(const DisplayProfile &display) {
    if (display.from_system && !display.icc.empty()) {
        cmsHPROFILE profile = open_profile(display.icc);
        if (profile != nullptr)
            return profile;
    }
    return cmsCreate_sRGBProfile();
}

std::shared_ptr<XfEntry> build_transform(const std::string &src_icc, const DisplayProfile &display,
                                         std::string &err) {
    std::string degrade;
    SrcProfile src = open_source_profile(src_icc, &degrade);
    cmsHPROFILE dst = open_display_profile(display);
    if (src.profile == nullptr || dst == nullptr) {
        if (src.profile != nullptr)
            cmsCloseProfile(src.profile);
        if (dst != nullptr)
            cmsCloseProfile(dst);
        err = "cannot create source/display profile for the display transform";
        return nullptr;
    }

    auto entry = std::make_shared<XfEntry>();
    entry->src_profile = src.profile;
    entry->dst_profile = dst;
    entry->flags = kTransformFlags;
    cmsHTRANSFORM first =
        cmsCreateTransform(src.profile, TYPE_RGBA_8, dst, TYPE_RGBA_8, kIntent, entry->flags);
    entry->alpha_copied = (first != nullptr);
    if (first == nullptr) {
        // 退化：去掉 COPY_ALPHA（个别 lcms2 构建/格式组合不支持 extra channels 时），
        // 变换后由调用方把 alpha 补回 255（本管线预览 alpha 恒 255，语义不变）。
        entry->flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE;
        first =
            cmsCreateTransform(src.profile, TYPE_RGBA_8, dst, TYPE_RGBA_8, kIntent, entry->flags);
    }
    if (first == nullptr) {
        err = "lcms2 rejected the display transform";
        return nullptr; // entry 析构关闭 src/dst profile
    }
    entry->handles[0] = first;
    if (!degrade.empty())
        log_warn("display", kLogFile, degrade, {{"src_icc_bytes", std::to_string(src_icc.size())}});
    return entry;
}

QColorSpace display_color_space(const DisplayProfile &display) {
    if (!display.from_system || display.icc.empty())
        return QColorSpace::SRgb;
    QColorSpace space = QColorSpace::fromIccProfile(
        QByteArray(display.icc.data(), static_cast<int>(display.icc.size())));
    if (space.isValid())
        return space;
    // Qt 不解析该 ICC：不挂错误色彩空间（像素已是显示编码，未管理窗口直出仍正确）。
    log_debug("display", kLogFile, "Qt cannot parse the display ICC; QImage left untagged");
    return {};
}

// 逐行把 alpha 补回 255（仅 COPY_ALPHA 建失败时的退化路径；RGBA8888 的交错布局）。
void force_opaque(QImage &image) {
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        std::uint8_t *row = image.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[static_cast<std::size_t>(x) * 4u + 3u] = 255;
    }
}

// ---------------------------------------------------------------------------------------------
// 4096² 级显示变换的行带并行（0.4.2 需求5 性能口径：单张 4096px 变换「<20ms 量级」）
// ---------------------------------------------------------------------------------------------
//   * lcms2 的 cmsDoTransform 是单线程的；4096×4096（16.8 MPx）8bit RGBA 变换单线程实测
//     ~70–190 ms（随本机负载波动），达不到目标 ⇒ 按**连续扫描行带**切分并行。
//   * 每个线程用**独立句柄**（XfEntry 句柄池，见上）；同一句柄并发调用实测严重退化。
//   * QImage::Format_RGBA8888 的 bytesPerLine == width*4（32bit 格式 4 字节对齐恒成立）⇒
//     行带 = 连续内存段，可直接整段交给 cmsDoTransform（不跨行填充；不满足时防御性回退单句柄）。
//   * 线程数 = min(4, hardware_concurrency, height)；像素 < kParallelMinPixels 时单线程
//     （小图起线程不划算）。预览池并发仍 ≤2（§6.1）；这是**单次变换内部的行带并行**，与
//     M4-W2-fix「多线程降采样」先例同口径（仅显示面、不落引擎）。
constexpr std::size_t kParallelMinPixels = 2'000'000;

// 返回 false = 无法执行变换（句柄池耗尽且临时句柄也建不出来）；调用方按失败处理，不改像素。
bool transform_image_in_place(XfEntry &entry, QImage &image) {
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return true;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const qsizetype row_bytes = qsizetype(width) * 4;
    if (pixels > 0xFFFFFFFFull || image.bytesPerLine() != row_bytes) {
        // 防御性回退：RGBA8888 恒满足 bytesPerLine == width*4；万一不满足，绝不能把行填充当
        // 像素送进 lcms2 —— 用单句柄整帧调用（语义不变；行填充会按像素处理属于错误输入）。
        std::array<unsigned, kMaxTransformThreads> leased{};
        if (!entry.lease(1, leased))
            return false;
        cmsDoTransform(entry.handles[leased[0]], image.bits(), image.bits(),
                       static_cast<cmsUInt32Number>(pixels));
        entry.release(leased, 1);
        return true;
    }
    unsigned threads = 1;
    if (pixels >= kParallelMinPixels && height > 1) {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        threads = std::min({kMaxTransformThreads, hw, static_cast<unsigned>(height)});
    }
    std::array<unsigned, kMaxTransformThreads> leased{};
    if (threads > 1 && !entry.lease(threads, leased)) {
        // 句柄池被并发变换占满（预览池 ≤2 × 4 = 8 槽，正常不该发生）：降并行重试。
        while (threads > 1 && !entry.lease(threads, leased))
            threads = threads > 2 ? threads / 2 : 1;
        if (threads == 1 && !entry.lease(1, leased)) {
            // 兜底：建一个临时句柄（不常驻），保证这个请求仍能出正确的显示编码像素。
            cmsHTRANSFORM temp =
                cmsCreateTransform(entry.src_profile, TYPE_RGBA_8, entry.dst_profile, TYPE_RGBA_8,
                                   kIntent, entry.flags);
            if (temp == nullptr)
                return false;
            cmsDoTransform(temp, image.bits(), image.bits(), static_cast<cmsUInt32Number>(pixels));
            cmsDeleteTransform(temp);
            return true;
        }
    }
    if (threads <= 1) {
        cmsDoTransform(entry.handles[leased[0]], image.bits(), image.bits(),
                       static_cast<cmsUInt32Number>(pixels));
        entry.release(leased, 1);
        return true;
    }
    std::uint8_t *const base = image.bits();
    const auto work = [&](unsigned t) {
        const int y0 = height * static_cast<int>(t) / static_cast<int>(threads);
        const int y1 = height * static_cast<int>(t + 1) / static_cast<int>(threads);
        if (y1 <= y0)
            return;
        std::uint8_t *const band =
            base + static_cast<std::size_t>(y0) * static_cast<std::size_t>(row_bytes);
        cmsDoTransform(entry.handles[leased[t]], band, band,
                       static_cast<cmsUInt32Number>(width) * static_cast<cmsUInt32Number>(y1 - y0));
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (unsigned t = 1; t < threads; ++t)
        pool.emplace_back(work, t);
    work(0); // 当前线程（预览 worker）承担第 0 带
    for (std::thread &worker : pool)
        worker.join();
    entry.release(leased, threads);
    return true;
}

} // namespace

// 启动卡死根治的防御面（task-4）：ICC 文件读取**先 stat 再读**，超过 kDisplayIccMaxBytes →
// 拒绝并如实记原因（异常/损坏的系统配置不应把超大文件整读进内存，更不该在启动期拖住调用
// 线程）；非普通文件（设备/管道/网络虚拟文件等 stat 失败）同样拒绝：ICC 必须是可 stat 的
// 常规文件。公开是为了让单测机械覆盖「正常/超限/缺失」三态（见 tests/unit/test_color.cpp）。
DisplayIccFile read_display_icc_file(const std::filesystem::path &path) {
    DisplayIccFile result;
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        result.error = "cannot stat the ICC file (not a regular readable file)";
        return result;
    }
    if (size > kDisplayIccMaxBytes) {
        result.error =
            "ICC file exceeds the " + std::to_string(kDisplayIccMaxBytes) + " byte safety limit";
        return result;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "cannot open the ICC file";
        return result;
    }
    result.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (result.bytes.size() >
        kDisplayIccMaxBytes) { // stat 与读取之间文件被替换/增长（TOCTOU 防御）
        result.bytes.clear();
        result.error = "ICC file grew past the " + std::to_string(kDisplayIccMaxBytes) +
                       " byte safety limit while reading";
        return result;
    }
    if (in.bad()) {
        result.bytes.clear();
        result.error = "ICC file read error";
        return result;
    }
    result.ok = true;
    return result;
}

DisplayProfile make_srgb_display_fallback(std::string reason) {
    DisplayProfile p;
    p.icc.clear();
    p.name = "sRGB";
    p.from_system = false;
    p.fallback_reason = std::move(reason);
    return p;
}

DisplayProfile system_display_profile() {
    ProfileCache &cache = profile_cache();
    {
        const std::lock_guard<std::mutex> lock(cache.mu);
        if (cache.has)
            return cache.profile;
    }
    // 获取在锁外（IO + lcms2 解析；并发首调可能重复获取一次，结果一致且无害）。
    const auto t0 = std::chrono::steady_clock::now();
    DisplayProfile acquired = acquire_uncached();
    const long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    // 启动期阻塞审计（PP_STARTUP_TIMING 诊断；默认不输出）：系统 display profile 获取是启动
    // 路径上唯一的阻塞系统调用之一（0.4.2 卡死反馈的根因面）。>50ms 记 warn，便于在用户
    // 环境取证；GUI 线程已改为异步获取（preview_panel.cpp），此日志只用于诊断。
    if (std::getenv("PP_STARTUP_TIMING") != nullptr && elapsed_ms > 50) {
        log_warn("display", kLogFile, "slow system display profile acquisition (>50ms)",
                 {{"elapsed_ms", std::to_string(elapsed_ms)},
                  {"from_system", acquired.from_system ? "1" : "0"}});
    } else {
        log_debug("display", kLogFile, "system display profile acquisition timing",
                  {{"elapsed_ms", std::to_string(elapsed_ms)},
                   {"from_system", acquired.from_system ? "1" : "0"}});
    }
    {
        const std::lock_guard<std::mutex> lock(cache.mu);
        cache.has = true;
        cache.profile = acquired;
    }
    return acquired;
}

// 纯状态机：5 个形参都是标量（done / 两个代次 / 已等毫秒 / 超时门限），头文件契约注释已逐项
// 说明顺序；`bugprone-easily-swappable-parameters` 对相邻可互转标量必然触发 —— 这是**有意的
// 位置式纯函数接口**（单测逐项钉住），不是可互换参数的误用。NOLINT 收敛理由，判据不计入棘轮。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
DisplayProfileAsyncState classify_display_profile_async(bool done, std::uint64_t result_generation,
                                                        std::uint64_t current_generation,
                                                        long long waited_ms, long long timeout_ms) {
    // 口径（见 display_profile.h）：结果已到就应用（GUI 线程从不等待，迟到只晚一点切到正确
    // 编码）；只有「还没结果且已过超时门限」才返回 Timeout —— 调用方记日志并保持当前
    // profile（软超时：仍可在硬上限内观察迟到结果）。
    if (done) {
        if (result_generation != current_generation)
            return DisplayProfileAsyncState::Stale;
        return DisplayProfileAsyncState::Ready;
    }
    if (timeout_ms > 0 && waited_ms >= timeout_ms)
        return DisplayProfileAsyncState::Timeout;
    return DisplayProfileAsyncState::Pending;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

void invalidate_display_profile_cache() {
    ProfileCache &cache = profile_cache();
    const std::lock_guard<std::mutex> lock(cache.mu);
    cache.has = false;
    cache.profile = DisplayProfile{};
}

DisplayProfile ui_display_profile() {
    if (QGuiApplication::platformName().compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) ==
        0) {
        return make_srgb_display_fallback(
            "offscreen platform has no physical display (CI/screenshot path)");
    }
    return system_display_profile();
}

QImage to_display_encoded(QImage source, const std::string &src_icc, const DisplayProfile &display,
                          DisplayTransformResult *result) {
    DisplayTransformResult local;
    DisplayTransformResult &res = result != nullptr ? *result : local;
    res = DisplayTransformResult{};
    if (source.isNull()) {
        res.error = "null source image";
        return source;
    }

    // 源描述名（诊断；不参与像素路径）
    std::string src_name;
    {
        cmsHPROFILE src_probe = nullptr;
        if (!src_icc.empty())
            src_probe = open_profile(src_icc);
        if (src_probe != nullptr) {
            src_name = profile_description(src_probe);
            if (src_name.empty())
                src_name = "ICC";
            src_name = "ICC(" + src_name + ")";
            cmsCloseProfile(src_probe);
        } else {
            src_name = "assumed sRGB";
        }
    }

    // 恒等快路径：源 = 假定 sRGB、显示 = sRGB 回退 → 像素逐字节不变（只如实挂 sRGB）。
    // 这是离线/CI 与普通 sRGB 素材的常态路径，也是「冻结截图零变化」的机械保证。
    if (src_icc.empty() && display.is_fallback()) {
        QImage out = source;
        out.setColorSpace(QColorSpace::SRgb);
        res.identity = true;
        res.applied = false;
        res.src_name = "assumed sRGB";
        res.dst_name = display.name.empty() ? "sRGB" : display.name;
        return out;
    }

    // lcms2 TYPE_RGBA_8 = 内存字节 R,G,B,A —— QImage::Format_RGBA8888 逐字节一致。
    // rvalue 传入时 std::move(source) 让 out 独占数据（原地变换不触发 COW detach 深拷贝）。
    // 非 RGBA8888 先转换；转换失败返回 null（错误已入 res.error，调用方按失败处理），
    // **不再**在 std::move(source) 之后使用 source（clang-tidy bugprone-use-after-move）。
    if (source.format() != QImage::Format_RGBA8888) {
        QImage converted = source.convertToFormat(QImage::Format_RGBA8888);
        if (converted.isNull()) {
            res.error = "cannot convert the source image to RGBA8888";
            res.src_name = std::move(src_name);
            res.dst_name = display.name;
            return {};
        }
        source = std::move(converted);
    }
    QImage out = std::move(source);
    if (out.isNull()) {
        res.error = "cannot convert the source image to RGBA8888";
        res.src_name = std::move(src_name);
        res.dst_name = display.name;
        return {};
    }
    const std::size_t pixels =
        static_cast<std::size_t>(out.width()) * static_cast<std::size_t>(out.height());
    if (pixels == 0 || pixels > 0xFFFFFFFFull) {
        res.error = "display transform does not support an image of this size";
        res.src_name = std::move(src_name);
        res.dst_name = display.name;
        // 返回 out（= 源像素，必要时已转 RGBA8888；source 已被 move，禁止再用 source）。
        return out;
    }

    const std::string key = "src:" + src_icc + "|dst:" +
                            (display.from_system ? display.icc : std::string("<srgb-fallback>"));
    std::shared_ptr<XfEntry> entry = transform_cache().get(key);
    if (entry == nullptr) {
        std::string build_err;
        entry = build_transform(src_icc, display, build_err);
        if (entry == nullptr) {
            res.error = build_err;
            res.src_name = std::move(src_name);
            res.dst_name = display.name;
            log_warn("display", kLogFile, "display transform unavailable; showing source pixels",
                     {{"error", build_err}});
            return out; // = 源像素（必要时已转 RGBA8888）；source 已被 move，禁止再用 source
        }
        transform_cache().put(key, entry);
    }

    // 8bit 变换按块读写：输入输出同缓冲是 lcms2 支持的就地用法（单测用独立参考变换对拍）；
    // 大图走行带并行 + 每线程独立句柄（见 transform_image_in_place 的线程模型说明）。
    if (!transform_image_in_place(*entry, out)) {
        res.error = "cannot acquire a display transform handle";
        res.src_name = std::move(src_name);
        res.dst_name = display.name;
        return out; // = 源像素；source 已被 move，禁止再用 source
    }
    if (!entry->alpha_copied)
        force_opaque(out);

    out.setColorSpace(display_color_space(display));
    res.applied = true;
    res.src_name = std::move(src_name);
    res.dst_name = display.name.empty() ? (display.is_fallback() ? "sRGB" : "ICC") : display.name;
    return out;
}

} // namespace pp
