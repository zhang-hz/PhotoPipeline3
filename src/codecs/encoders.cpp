// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T6 — encoder factory (registry lookup) + internal registry.
//
// Contract: docs/m1-tasks.md §3.8 (PP-FROZEN factory) / §3.17 (internal registry).
// File ownership (§3.17): T6 owns this file, encoder_registry.h and
// enc_jpegli/enc_jxl/enc_webp.cpp; T7 owns enc_heif.cpp / enc_oiio.cpp and
// exclusively defines introspect_backends() / probe_bitdepth_support().
//
// The registry lives in a function-local static so it is constructed on first
// use: register_encoder() is called from the dynamic initialisers of the
// encoder translation units, where a namespace-scope static container could
// still be uninitialised (static initialisation order fiasco). Every encoder
// self-registers with PP_REGISTER_ENCODER before main() runs, so the table is
// read-only afterwards and needs no locking (contract E1: encoders are
// stateless and shareable across worker threads).
//
// M7-WP2（R-051/R-299/R-300，裁定登记见 build/m7-wp2/handoff.md）：
//   * **anchor 轨已删**（旧 pp_link_encoder_* 三枚 + kEncoderMembersLinked）。M2-T13 起
//     唯一链接形态是 pp_core_registered 的 whole-archive（CMakeLists §2b），anchor 与之
//     双轨并存且注释互相矛盾（CMakeLists:204-210 称"不再需要锚点"，本文件旧注释仍教人
//     造锚点）；误链 pp_core 时注册丢失的诊断改由**启动期注册表期望键断言**承担（下述
//     `expected_registry_complete`：对照 static_formats() 逐 (format,backend) 核对，
//     缺失即 log_error）。
//   * backend_id 空串 tie-break **显式化**（旧实现依赖静态初始化序 = "首个注册项"）：
//     空串 → ① 静态表该格式的首选后端（FormatDef::backends.front().id）；② 无则按
//     backend_id 字典序最小的非空注册项；③ 再无则空串注册项。当前注册表下与旧行为
//     逐例一致（avif 首选 svt-av1 = 旧"先注册者"），但不再依赖 TU 初始化顺序。

#include "codecs/encoders.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codecs/encoder_registry.h"
#include "core/logger.h"
#include "core/params.h"

namespace pp {
namespace {

struct RegistryEntry {
    std::string format_id;
    std::string backend_id; // may be empty ("default backend")
    EncoderFactory factory = nullptr;
};

std::vector<RegistryEntry> &registry() {
    static std::vector<RegistryEntry> entries;
    return entries;
}

// R-299：backend_id 空串的显式 tie-break（规则见文件头注释）。
const RegistryEntry *resolve_entry(const std::vector<RegistryEntry> &entries,
                                   std::string_view format_id, std::string_view backend_id) {
    const RegistryEntry *empty_backend = nullptr;
    const RegistryEntry *lexicographic = nullptr;
    for (const RegistryEntry &e : entries) {
        if (e.format_id != format_id) {
            continue;
        }
        if (!backend_id.empty()) {
            if (e.backend_id == backend_id) {
                return &e;
            }
            continue;
        }
        if (e.backend_id.empty()) {
            if (empty_backend == nullptr) {
                empty_backend = &e;
            }
            continue;
        }
        // 空串请求：静态表首选后端优先，其次字典序最小（与注册序无关）。
        const FormatDef *f = find_format(format_id);
        if (f != nullptr && !f->backends.empty() && e.backend_id == f->backends.front().id) {
            return &e;
        }
        if (lexicographic == nullptr || e.backend_id < lexicographic->backend_id) {
            lexicographic = &e;
        }
    }
    return lexicographic != nullptr ? lexicographic : empty_backend;
}

// R-051：注册表期望键断言（启动期诊断）—— 静态格式表声明的每个 (format, backend) 都应有
// 自注册项；缺失 = whole-archive 链接形态被破坏（历史坑：误链 pp_core 导致 heif/oiio 注册
// 静默丢失）。仅在首次查表时核对一次，缺失逐条 log_error（不 abort：注册表仍尽力服务）。
void expected_registry_complete(const std::vector<RegistryEntry> &entries) {
    static const bool checked = [&entries] {
        for (const FormatDef &f : static_formats()) {
            for (const BackendDef &b : f.backends) {
                const bool present =
                    std::any_of(entries.begin(), entries.end(), [&](const RegistryEntry &e) {
                        return e.format_id == f.id && e.backend_id == b.id;
                    });
                if (!present) {
                    log_error("Encode", "encoders.cpp",
                              "encoder not registered (link form broken?)",
                              {{"format", f.id}, {"backend", b.id}});
                }
            }
        }
        return true;
    }();
    (void)checked;
}

} // namespace

bool register_encoder(std::string_view format_id, std::string_view backend_id, EncoderFactory f) {
    if (format_id.empty() || f == nullptr) {
        return false;
    }
    std::vector<RegistryEntry> &entries = registry();
    const bool duplicate = std::any_of(entries.begin(), entries.end(), [&](const RegistryEntry &e) {
        return e.format_id == format_id && e.backend_id == backend_id;
    });
    if (duplicate) {
        return false; // keep the first registration (§3.17)
    }
    entries.push_back(RegistryEntry{std::string(format_id), std::string(backend_id), f});
    return true;
}

std::unique_ptr<IEncoder> create_registered_encoder(std::string_view format_id,
                                                    std::string_view backend_id) {
    const std::vector<RegistryEntry> &entries = registry();
    expected_registry_complete(entries);
    const RegistryEntry *e = resolve_entry(entries, format_id, backend_id);
    return (e != nullptr && e->factory) ? e->factory() : nullptr;
}

std::vector<std::string> registered_backends(std::string_view format_id) {
    std::vector<std::string> out;
    for (const RegistryEntry &e : registry()) {
        if (e.format_id != format_id) {
            continue;
        }
        if (std::find(out.begin(), out.end(), e.backend_id) == out.end()) {
            out.push_back(e.backend_id);
        }
    }
    return out;
}

std::string resolved_backend(std::string_view format_id, std::string_view backend_id) {
    const std::vector<RegistryEntry> &entries = registry();
    const RegistryEntry *e = resolve_entry(entries, format_id, backend_id);
    return e != nullptr ? e->backend_id : std::string();
}

std::unique_ptr<IEncoder> make_encoder(std::string_view format_id, std::string_view backend_id) {
    // Registry lookup only: unregistered combinations yield nullptr (§3.8).
    return create_registered_encoder(format_id, backend_id);
}

} // namespace pp
