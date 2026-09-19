// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M0 spikes: lcms2 numeric golden values (e) and Exiv2 lossless rewrite
// fidelity (f).
//
// Frozen contract: docs/m0-tasks.md §12.3.
//   usage : pp_spikes e | pp_spikes f --golden-root <tests/golden>
//   output: "SPIKE e|f OK|FAIL <detail>", exit code = number of FAILs
//   a missing meta/exif_full.jpg for spike f => SKIP, exit 77

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/imageio.h>

#include <exiv2/exiv2.hpp>
#include <lcms2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_fails = 0;

void emit(const std::string& name, const char* status, const std::string& detail) {
    if (std::strcmp(status, "FAIL") == 0) {
        ++g_fails;
    }
    std::printf("SPIKE %s %s %s\n", name.c_str(), status, detail.c_str());
}

std::string fmt_double(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

bool read_file(const fs::path& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot read " + path.string();
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// Offset of the SOS marker (0xFF 0xDA); JPEG entropy data is byte-stuffed, so the first
// occurrence is the scan header.
size_t find_sos(const std::vector<uint8_t>& data) {
    for (size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xDA) {
            return i;
        }
    }
    return std::string::npos;
}

// Spike e: lcms2 numeric golden values.
bool spike_e(std::string& err) {
    cmsHPROFILE srgb = cmsCreate_sRGBProfile();
    if (srgb == nullptr) {
        err = "cmsCreate_sRGBProfile returned null";
        return false;
    }
    // (1) sRGB -> sRGB float transform must be an identity within 1e-5.
    cmsHTRANSFORM xf = cmsCreateTransform(srgb, TYPE_RGB_FLT, srgb, TYPE_RGB_FLT,
                                          INTENT_RELATIVE_COLORIMETRIC,
                                          cmsFLAGS_BLACKPOINTCOMPENSATION);
    if (xf == nullptr) {
        cmsCloseProfile(srgb);
        err = "cmsCreateTransform(sRGB->sRGB) returned null";
        return false;
    }
    const float in[3] = {0.1f, 0.5f, 0.9f};
    float out[3] = {0.0f, 0.0f, 0.0f};
    cmsDoTransform(xf, in, out, 1);
    cmsDeleteTransform(xf);
    double max_err = 0.0;
    for (int i = 0; i < 3; ++i) {
        max_err = std::max(max_err, std::fabs(static_cast<double>(out[i]) - in[i]));
    }

    // (2) sRGB white -> Lab (D50) must be L in [99.5,100.5] and |a|,|b| <= 1.
    cmsHPROFILE lab = cmsCreateLab4Profile(cmsD50_xyY());
    if (lab == nullptr) {
        cmsCloseProfile(srgb);
        err = "cmsCreateLab4Profile(D50) returned null";
        return false;
    }
    cmsHTRANSFORM xf_lab = cmsCreateTransform(srgb, TYPE_RGB_FLT, lab, TYPE_Lab_DBL,
                                              INTENT_RELATIVE_COLORIMETRIC,
                                              cmsFLAGS_BLACKPOINTCOMPENSATION);
    double lab_out[3] = {0.0, 0.0, 0.0};
    if (xf_lab == nullptr) {
        cmsCloseProfile(lab);
        cmsCloseProfile(srgb);
        err = "cmsCreateTransform(sRGB->Lab) returned null";
        return false;
    }
    const float white[3] = {1.0f, 1.0f, 1.0f};
    cmsDoTransform(xf_lab, white, lab_out, 1);
    cmsDeleteTransform(xf_lab);
    cmsCloseProfile(lab);
    cmsCloseProfile(srgb);

    const bool identity_ok = max_err <= 1e-5;
    const bool lab_ok = lab_out[0] >= 99.5 && lab_out[0] <= 100.5 &&
                        std::fabs(lab_out[1]) <= 1.0 && std::fabs(lab_out[2]) <= 1.0;
    err = "identity_max_err=" + fmt_double(max_err) + " Lab=(" + fmt_double(lab_out[0]) + "," +
          fmt_double(lab_out[1]) + "," + fmt_double(lab_out[2]) + ")";
    return identity_ok && lab_ok;
}

// Spike f: Exiv2 lossless metadata rewrite fidelity (R10).
bool spike_f(const fs::path& golden_root, std::string& err) {
    const fs::path src = golden_root / "meta" / "exif_full.jpg";
    // Repo-local scratch path (task book §12.3 r2: replaced the former /tmp path).
    const fs::path tmp = ".cache/tmp/pp_spike_f.jpg";
    std::error_code ec;
    fs::create_directories(tmp.parent_path(), ec);
    ec.clear();
    fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = "copy to " + tmp.string() + " failed: " + ec.message();
        return false;
    }
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(tmp.string());
        if (!img) {
            err = "Exiv2 could not open the copy";
            return false;
        }
        img->readMetadata();
        Exiv2::ExifData exif = img->exifData();
        exif["Exif.Image.Artist"] = "M0-F";
        img->setExifData(exif);
        img->writeMetadata();
    } catch (const Exiv2::Error& e) {
        err = std::string("exiv2: ") + e.what();
        return false;
    }

    // (1) every byte from the SOS marker onwards must be identical.
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
    if (!read_file(src, before, err) || !read_file(tmp, after, err)) {
        return false;
    }
    const size_t sos_before = find_sos(before);
    const size_t sos_after = find_sos(after);
    if (sos_before == std::string::npos || sos_after == std::string::npos) {
        err = "SOS marker (0xFF 0xDA) not found";
        return false;
    }
    const size_t tail_before = before.size() - sos_before;
    const size_t tail_after = after.size() - sos_after;
    const bool tail_ok = tail_before == tail_after &&
                         std::memcmp(before.data() + sos_before, after.data() + sos_after,
                                     tail_before) == 0;

    // (2) decoded pixels must hash equally.
    const std::string hash_before =
        OIIO::ImageBufAlgo::computePixelHashSHA1(OIIO::ImageBuf(src.string()));
    const std::string hash_after =
        OIIO::ImageBufAlgo::computePixelHashSHA1(OIIO::ImageBuf(tmp.string()));
    const bool hash_ok = !hash_before.empty() && hash_before == hash_after;

    err = "tail_bytes_equal=" + std::string(tail_ok ? "1" : "0") + " (" +
          std::to_string(tail_before) + "B) pixel_hash=" + hash_before.substr(0, 12) +
          " match=" + std::string(hash_ok ? "1" : "0");
    return tail_ok && hash_ok;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    if (args.size() == 1 && args[0] == "e") {
        std::string err;
        if (spike_e(err)) {
            emit("e", "OK", err);
        } else {
            emit("e", "FAIL", err);
        }
        return g_fails;
    }
    if (args.size() == 3 && args[0] == "f" && args[1] == "--golden-root") {
        const fs::path src = fs::path(args[2]) / "meta" / "exif_full.jpg";
        if (!fs::exists(src)) {
            emit("f", "SKIP", "missing " + src.string());
            return 77;
        }
        std::string err;
        if (spike_f(fs::path(args[2]), err)) {
            emit("f", "OK", err);
        } else {
            emit("f", "FAIL", err);
        }
        return g_fails;
    }
    std::fprintf(stderr, "usage: pp_spikes e | pp_spikes f --golden-root <tests/golden>\n");
    return 2;
}
