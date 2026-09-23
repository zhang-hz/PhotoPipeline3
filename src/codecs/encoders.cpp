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

#include "codecs/encoders.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codecs/encoder_registry.h"

// Link anchors (M1-T6). pp_core is a STATIC library, so the linker only pulls in
// the archive members it needs: without an undefined symbol inside, an encoder
// translation unit would be dropped and its PP_REGISTER_ENCODER initialiser
// would never run (measured: make_encoder("jpeg","jpegli") == nullptr before
// these anchors existed). Each T6 encoder defines one empty anchor symbol; the
// references below force the three members into every consumer of pp_core.
// T7 must provide the equivalent anchors for enc_heif.cpp / enc_oiio.cpp before
// they can be referenced here (reported to the main dialogue).
extern "C" void pp_link_encoder_jpegli();
extern "C" void pp_link_encoder_jxl();
extern "C" void pp_link_encoder_webp();

namespace pp {
namespace {

// Executed at static-initialisation time; the calls only exist to keep the
// relocations (and therefore the archive members) alive.
const bool kEncoderMembersLinked = [] {
    pp_link_encoder_jpegli();
    pp_link_encoder_jxl();
    pp_link_encoder_webp();
    return true;
}();

struct RegistryEntry {
    std::string format_id;
    std::string backend_id; // may be empty ("default backend")
    EncoderFactory factory = nullptr;
};

std::vector<RegistryEntry> &registry() {
    static std::vector<RegistryEntry> entries;
    return entries;
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
    for (const RegistryEntry &e : registry()) {
        if (e.format_id != format_id) {
            continue;
        }
        if (backend_id.empty() || e.backend_id == backend_id) {
            return e.factory ? e.factory() : nullptr;
        }
    }
    return nullptr;
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

std::unique_ptr<IEncoder> make_encoder(std::string_view format_id, std::string_view backend_id) {
    // Registry lookup only: unregistered combinations yield nullptr (§3.8).
    (void)kEncoderMembersLinked; // guarantees the link anchors stay referenced
    return create_registered_encoder(format_id, backend_id);
}

} // namespace pp
