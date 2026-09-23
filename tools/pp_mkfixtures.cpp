// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M0 fixture generator/verifier for the golden corpus.
//
// Frozen contract: docs/m0-tasks.md §12.2.
//   usage: pp_mkfixtures --make <dir> | --verify <dir>
//   output: "MADE <name> OK|FAIL <detail>" / "FIXTURE <name> OK|FAIL|SKIP <detail>"
//   exit code = number of FAILs (missing verify directory => all SKIP, exit 77)

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/imageio.h>

#include <exiv2/exiv2.hpp>
#include <jxl/encode.h>
#include <libheif/heif.h>
#include <tiffio.h>
#include <webp/encode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// jpegli: the task book assumes <jpegli.h>; google/jpegli installs its public encoder
// API as jpegli/encode.h, built on libjpeg's jpeg_compress_struct.
#if __has_include(<jpegli/encode.h>)
#include <jpegli/encode.h>
#elif __has_include(<jpegli.h>)
#include <jpegli.h>
#else
#include <jpeglib.h>
extern "C" {
void jpegli_CreateCompress(j_compress_ptr cinfo, int version, size_t structsize);
void jpegli_set_defaults(j_compress_ptr cinfo);
void jpegli_set_colorspace(j_compress_ptr cinfo, J_COLOR_SPACE colorspace);
void jpegli_set_distance(j_compress_ptr cinfo, float distance, boolean force_baseline);
void jpegli_stdio_dest(j_compress_ptr cinfo, FILE* outfile);
void jpegli_start_compress(j_compress_ptr cinfo, boolean write_all_tables);
JDIMENSION jpegli_write_scanlines(j_compress_ptr cinfo, JSAMPARRAY scanlines,
                                  JDIMENSION num_lines);
void jpegli_finish_compress(j_compress_ptr cinfo);
void jpegli_destroy_compress(j_compress_ptr cinfo);
}
#define jpegli_create_compress(cinfo)                                                       \
    jpegli_CreateCompress((cinfo), JPEG_LIB_VERSION,                                        \
                          (size_t)sizeof(struct jpeg_compress_struct))
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kSize = 64;  // fixture edge length in pixels
constexpr const char* kDateTimeOriginal = "2024:03:01 10:00:00";
constexpr const char* kArtist = "M0";
constexpr double kLatDeg = 31.2304;
constexpr double kLonDeg = 121.4737;
constexpr double kGpsTolerance = 1e-4;

int g_fails = 0;

void emit(const char* prefix, const std::string& name, const char* status,
          const std::string& detail) {
    if (std::strcmp(status, "FAIL") == 0) {
        ++g_fails;
    }
    std::printf("%s %s %s %s\n", prefix, name.c_str(), status, detail.c_str());
}

std::string fmt_double(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return std::string(buf);
}

bool write_bytes(const fs::path& path, const void* data, size_t size, std::string& err) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot open for write";
        return false;
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) {
        err = "write failed";
        return false;
    }
    return true;
}

// 64x64 horizontal gradient, uint8 RGB: r=x*4, g=y*4, b=x+y.
std::vector<uint8_t> make_gradient_rgb() {
    std::vector<uint8_t> px(static_cast<size_t>(kSize) * kSize * 3, 0);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const size_t i = (static_cast<size_t>(y) * kSize + x) * 3;
            px[i + 0] = static_cast<uint8_t>(x * 4);
            px[i + 1] = static_cast<uint8_t>(y * 4);
            px[i + 2] = static_cast<uint8_t>(x + y);
        }
    }
    return px;
}

// --- metadata -------------------------------------------------------------------------

Exiv2::ExifData standard_exif() {
    Exiv2::ExifData exif;
    exif["Exif.Photo.DateTimeOriginal"] = kDateTimeOriginal;
    exif["Exif.Image.Artist"] = kArtist;
    exif["Exif.GPSInfo.GPSVersionID"] = "2 3 0 0";
    exif["Exif.GPSInfo.GPSLatitudeRef"] = "N";
    exif["Exif.GPSInfo.GPSLatitude"] = "31/1 13/1 4944/100";   // 31.2304 N
    exif["Exif.GPSInfo.GPSLongitudeRef"] = "E";
    exif["Exif.GPSInfo.GPSLongitude"] = "121/1 28/1 2532/100";  // 121.4737 E
    return exif;
}

// Contract §12.2 freezes `ExifData::copy(&buf, &size, Exiv2::littleEndian)`; Exiv2 0.28.x
// has no such overload (only Exifdatum::copy(byte*, ByteOrder)). Mechanically adapted to the
// installed serializer ExifParser::encode(Blob&, ByteOrder, ExifData&), which yields the same
// raw TIFF/Exif blob expected by libheif and by the JPEG XL "Exif" box.
std::string exif_blob(Exiv2::ExifData& exif) {
    Exiv2::Blob blob;
    Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, exif);
    return std::string(reinterpret_cast<const char*>(blob.data()), blob.size());
}

std::string xmp_packet() {
    Exiv2::XmpData xmp;
    xmp["Xmp.dc.description"] = "PhotoPipeline M0 jxl fixture";
    xmp["Xmp.xmp.CreatorTool"] = "PhotoPipeline-M0";
    std::string packet;
    Exiv2::XmpParser::initialize();
    // Exiv2 0.28 API: XmpParser::encode(packet, xmp) returns int (verified).
    Exiv2::XmpParser::encode(packet, xmp);
    return packet;
}

bool write_exif_metadata(const fs::path& path, const Exiv2::ExifData& exif, std::string& err) {
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(path.string());
        if (!img) {
            err = "Exiv2 could not open " + path.string();
            return false;
        }
        img->readMetadata();
        img->setExifData(exif);
        img->writeMetadata();
    } catch (const Exiv2::Error& e) {
        err = std::string("exiv2: ") + e.what();
        return false;
    }
    return true;
}

bool read_exif_value(const fs::path& path, const std::string& key, std::string& out,
                     std::string& err) {
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(path.string());
        if (!img) {
            err = "Exiv2 could not open";
            return false;
        }
        img->readMetadata();
        const Exiv2::ExifData& exif = img->exifData();
        const auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end()) {
            err = key + " absent";
            return false;
        }
        out = it->toString();
    } catch (const Exiv2::Error& e) {
        err = std::string("exiv2: ") + e.what();
        return false;
    }
    return true;
}

// "31/1 13/1 4944/100" -> decimal degrees.
bool gps_to_decimal(const std::string& s, double& out) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    if (parts.size() != 3) {
        return false;
    }
    double v[3] = {0.0, 0.0, 0.0};
    for (size_t i = 0; i < parts.size(); ++i) {
        const size_t slash = parts[i].find('/');
        const double num = std::stod(parts[i].substr(0, slash));
        const double den = (slash == std::string::npos) ? 1.0 : std::stod(parts[i].substr(slash + 1));
        if (den == 0.0) {
            return false;
        }
        v[i] = num / den;
    }
    out = v[0] + v[1] / 60.0 + v[2] / 3600.0;
    return true;
}

// --- encoders -------------------------------------------------------------------------

bool write_jpegli_jpeg(const fs::path& path, const std::vector<uint8_t>& rgb, int w, int h,
                       float distance, std::string& err) {
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (f == nullptr) {
        err = "cannot open for write";
        return false;
    }
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    std::memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr);
    jpegli_create_compress(&cinfo);
    jpegli_stdio_dest(&cinfo, f);
    cinfo.image_width = static_cast<JDIMENSION>(w);
    cinfo.image_height = static_cast<JDIMENSION>(h);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpegli_set_defaults(&cinfo);
    jpegli_set_colorspace(&cinfo, JCS_YCbCr);
    // Installed jpegli API: 3-argument jpegli_set_distance(cinfo, float, force_baseline).
    jpegli_set_distance(&cinfo, distance, FALSE);
    // 4:4:4 - no chroma downsampling.
    for (int c = 0; c < 3 && cinfo.comp_info != nullptr; ++c) {
        cinfo.comp_info[c].h_samp_factor = 1;
        cinfo.comp_info[c].v_samp_factor = 1;
    }
    jpegli_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(&rgb[static_cast<size_t>(cinfo.next_scanline) * w * 3]);
        jpegli_write_scanlines(&cinfo, &row, 1);
    }
    jpegli_finish_compress(&cinfo);
    jpegli_destroy_compress(&cinfo);
    std::fclose(f);
    return true;
}

bool write_webp(const fs::path& path, const std::vector<uint8_t>& rgb, int w, int h,
                bool lossless, std::string& err) {
    WebPConfig cfg;
    if (WebPConfigInit(&cfg) == 0) {
        err = "WebPConfigInit failed";
        return false;
    }
    if (lossless) {
        cfg.lossless = 1;
        cfg.exact = 1;
    } else {
        cfg.quality = 90;
        cfg.use_sharp_yuv = 1;
    }
    if (WebPValidateConfig(&cfg) == 0) {
        err = "WebPValidateConfig failed";
        return false;
    }
    WebPPicture pic;
    if (WebPPictureInit(&pic) == 0) {
        err = "WebPPictureInit failed";
        return false;
    }
    pic.use_argb = 1;
    pic.width = w;
    pic.height = h;
    if (WebPPictureImportRGB(&pic, rgb.data(), w * 3) == 0) {
        WebPPictureFree(&pic);
        err = "WebPPictureImportRGB failed";
        return false;
    }
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &writer;
    const int ok = WebPEncode(&cfg, &pic);
    bool written = false;
    if (ok != 0) {
        written = write_bytes(path, writer.mem, writer.size, err);
    } else {
        err = "WebPEncode failed";
    }
    WebPMemoryWriterClear(&writer);
    WebPPictureFree(&pic);
    return written;
}

struct HeifChoice {
    const heif_encoder_descriptor* desc = nullptr;
    std::string id;
    std::string name;
};

std::vector<HeifChoice> heif_choices(heif_compression_format fmt) {
    std::vector<HeifChoice> out;
    // libheif exposes the 4-argument free function
    // heif_get_encoder_descriptors(format, name, out, count), not the 6-argument context form
    // assumed by the task book (verified against the installed headers).
    int count = heif_get_encoder_descriptors(fmt, nullptr, nullptr, 0);
    if (count <= 0) {
        return out;
    }
    std::vector<const heif_encoder_descriptor*> descs(static_cast<size_t>(count), nullptr);
    count = heif_get_encoder_descriptors(fmt, nullptr, descs.data(), count);
    for (int i = 0; i < count; ++i) {
        HeifChoice c;
        c.desc = descs[static_cast<size_t>(i)];
        const char* id = heif_encoder_descriptor_get_id_name(c.desc);
        const char* nm = heif_encoder_descriptor_get_name(c.desc);
        c.id = (id != nullptr) ? id : "";
        c.name = (nm != nullptr) ? nm : "";
        out.push_back(c);
    }
    return out;
}

bool write_heif(const fs::path& path, const std::vector<uint8_t>& rgb, int w, int h,
                heif_compression_format fmt, const std::string& prefer_id,
                const std::string& exif, std::string& used, std::string& err) {
    heif_context* ctx = heif_context_alloc();
    if (ctx == nullptr) {
        err = "heif_context_alloc failed";
        return false;
    }
    const std::vector<HeifChoice> choices = heif_choices(fmt);
    const HeifChoice* chosen = nullptr;
    if (!prefer_id.empty()) {
        for (const HeifChoice& c : choices) {
            if (c.id.find(prefer_id) != std::string::npos) {
                chosen = &c;
                break;
            }
        }
    }
    if (chosen == nullptr && !choices.empty()) {
        chosen = &choices.front();  // libheif sorts descriptors by priority
    }
    heif_encoder* enc = nullptr;
    heif_error e = (chosen != nullptr)
                       ? heif_context_get_encoder(ctx, chosen->desc, &enc)
                       : heif_context_get_encoder_for_format(ctx, fmt, &enc);
    if (e.code != heif_error_Ok || enc == nullptr) {
        err = std::string("no encoder: ") + ((e.message != nullptr) ? e.message : "unknown");
        heif_context_free(ctx);
        return false;
    }
    used = (chosen != nullptr) ? (chosen->id.empty() ? chosen->name : chosen->id)
                              : std::string("libheif default");
    std::string note;
    e = heif_encoder_set_lossy_quality(enc, 50);
    if (e.code != heif_error_Ok) {
        note += " quality:";
        note += (e.message != nullptr) ? e.message : "?";
    }
    e = heif_encoder_set_parameter_string(enc, "chroma", "444");
    if (e.code != heif_error_Ok) {
        note += " chroma444:";
        note += (e.message != nullptr) ? e.message : "?";
    }

    heif_image* img = nullptr;
    e = heif_image_create(w, h, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img);
    if (e.code != heif_error_Ok || img == nullptr) {
        err = "heif_image_create failed";
        heif_encoder_release(enc);
        heif_context_free(ctx);
        return false;
    }
    e = heif_image_add_plane(img, heif_channel_interleaved, w, h, 8);
    if (e.code == heif_error_Ok) {
        int stride = 0;
        uint8_t* plane = heif_image_get_plane(img, heif_channel_interleaved, &stride);
        if (plane == nullptr) {
            e.code = heif_error_Memory_allocation_error;
            e.message = "heif_image_get_plane returned null";
        } else {
            for (int y = 0; y < h; ++y) {
                std::memcpy(plane + static_cast<size_t>(y) * stride,
                            rgb.data() + static_cast<size_t>(y) * w * 3,
                            static_cast<size_t>(w) * 3);
            }
        }
    }
    heif_image_handle* handle = nullptr;
    if (e.code == heif_error_Ok) {
        e = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
    }
    if (e.code == heif_error_Ok && !exif.empty()) {
        e = heif_context_add_exif_metadata(ctx, handle, exif.data(), static_cast<int>(exif.size()));
    }
    bool ok = false;
    if (e.code == heif_error_Ok) {
        e = heif_context_write_to_file(ctx, path.string().c_str());
        ok = (e.code == heif_error_Ok);
    }
    if (!ok) {
        err = std::string("libheif: ") + ((e.message != nullptr) ? e.message : "unknown error");
    }
    if (!note.empty()) {
        used += " (warnings:" + note + ")";
    }
    if (handle != nullptr) {
        heif_image_handle_release(handle);
    }
    heif_image_release(img);
    heif_encoder_release(enc);
    heif_context_free(ctx);
    return ok;
}

bool write_jxl(const fs::path& path, const std::vector<uint8_t>& rgb, int w, int h,
               const std::string& exif, const std::string& xmp, std::string& err) {
    JxlEncoder* enc = JxlEncoderCreate(nullptr);
    if (enc == nullptr) {
        err = "JxlEncoderCreate failed";
        return false;
    }
    JxlEncoderUseContainer(enc, JXL_TRUE);  // required before adding Exif/xml boxes
    // libjxl >= 0.11: the encoder assumes no metadata boxes by default; JxlEncoderUseBoxes
    // must be called (before encoding starts) or every JxlEncoderAddBox returns
    // JXL_ENC_ERROR, and JxlEncoderCloseBoxes is required at the end (jxl/encode.h:989-1075).
    JxlEncoderUseBoxes(enc);
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = static_cast<uint32_t>(w);
    info.ysize = static_cast<uint32_t>(h);
    info.bits_per_sample = 8;
    info.exponent_bits_per_sample = 0;
    info.num_color_channels = 3;
    info.num_extra_channels = 0;
    info.alpha_bits = 0;
    info.uses_original_profile = JXL_TRUE;
    JxlEncoderStatus st = JxlEncoderSetBasicInfo(enc, &info);
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    if (st == JXL_ENC_SUCCESS) {
        st = JxlEncoderSetColorEncoding(enc, &color);
    }
    JxlEncoderFrameSettings* fs = JxlEncoderFrameSettingsCreate(enc, nullptr);
    if (st == JXL_ENC_SUCCESS && fs == nullptr) {
        st = JXL_ENC_ERROR;
    }
    if (st == JXL_ENC_SUCCESS) {
        st = JxlEncoderFrameSettingsSetOption(fs, JXL_ENC_FRAME_SETTING_EFFORT, 7);
    }
    if (st == JXL_ENC_SUCCESS) {
        st = JxlEncoderSetFrameDistance(fs, 1.0f);
    }
    std::string exif_box;
    const JxlPixelFormat pf = {3, JXL_TYPE_UINT8, JXL_LITTLE_ENDIAN, 0};
    if (st == JXL_ENC_SUCCESS) {
        st = JxlEncoderAddImageFrame(fs, &pf, rgb.data(), rgb.size());
    }
    if (st == JXL_ENC_SUCCESS && !exif.empty()) {
        // jxl/encode.h:1024-1027: the "Exif" box contents must be prepended by a 4-byte
        // TIFF header offset (4 zero bytes = tiff header follows immediately).
        exif_box = std::string(4, '\0') + exif;
        st = JxlEncoderAddBox(enc, "Exif", reinterpret_cast<const uint8_t*>(exif_box.data()),
                              exif_box.size(), JXL_FALSE);
    }
    if (st == JXL_ENC_SUCCESS && !xmp.empty()) {
        st = JxlEncoderAddBox(enc, "xml ", reinterpret_cast<const uint8_t*>(xmp.data()),
                              xmp.size(), JXL_FALSE);
    }
    if (st != JXL_ENC_SUCCESS) {
        JxlEncoderDestroy(enc);
        err = "jxl encode setup failed";
        return false;
    }
    JxlEncoderCloseBoxes(enc);  // libjxl >= 0.11: required after the last AddBox call
    JxlEncoderCloseInput(enc);

    std::vector<uint8_t> out(1u << 16);
    uint8_t* next = out.data();
    size_t avail = out.size();
    for (;;) {
        st = JxlEncoderProcessOutput(enc, &next, &avail);
        if (st == JXL_ENC_SUCCESS) {
            break;
        }
        if (st != JXL_ENC_NEED_MORE_OUTPUT) {
            JxlEncoderDestroy(enc);
            err = "JxlEncoderProcessOutput failed";
            return false;
        }
        const size_t used = static_cast<size_t>(next - out.data());
        out.resize(out.size() * 2);
        next = out.data() + used;
        avail = out.size() - used;
    }
    const size_t total = static_cast<size_t>(next - out.data());
    JxlEncoderDestroy(enc);
    return write_bytes(path, out.data(), total, err);
}

bool write_cmyk_tiff(const fs::path& path, int w, int h, std::string& err) {
    TIFF* tif = TIFFOpen(path.string().c_str(), "w");
    if (tif == nullptr) {
        err = "TIFFOpen failed";
        return false;
    }
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(w));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(h));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);
    TIFFSetField(tif, TIFFTAG_INKSET, INKSET_CMYK);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
    std::vector<uint8_t> row(static_cast<size_t>(w) * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(x) * 4;
            row[i + 0] = static_cast<uint8_t>(255 - x * 4);  // C
            row[i + 1] = static_cast<uint8_t>(255 - y * 4);  // M
            row[i + 2] = static_cast<uint8_t>(255 - (x + y) % 256);  // Y
            row[i + 3] = 0;                                          // K
        }
        if (TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(y), 0) < 0) {
            TIFFClose(tif);
            err = "TIFFWriteScanline failed";
            return false;
        }
    }
    TIFFClose(tif);
    return true;
}

// --- OIIO readback --------------------------------------------------------------------

bool oiio_check(const fs::path& path, int want_w, int want_h, std::string& err) {
    auto in = OIIO::ImageInput::open(path.string());
    if (!in) {
        err = "OIIO open failed: " + OIIO::geterror();
        return false;
    }
    const OIIO::ImageSpec spec = in->spec();
    if (want_w > 0 && (spec.width != want_w || spec.height != want_h)) {
        err = "size=" + std::to_string(spec.width) + "x" + std::to_string(spec.height);
        return false;
    }
    err = std::to_string(spec.width) + "x" + std::to_string(spec.height) + " ch=" +
          std::to_string(spec.nchannels);
    return true;
}

bool check_metadata_field(const fs::path& path, const std::string& name,
                          const std::string& key, const std::string& expect) {
    std::string value;
    std::string err;
    if (!read_exif_value(path, key, value, err)) {
        emit("FIXTURE", name, "FAIL", key + ": " + err);
        return false;
    }
    if (value != expect) {
        emit("FIXTURE", name, "FAIL", key + "=" + value + " expected " + expect);
        return false;
    }
    return true;
}

int cmd_make(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "pp_mkfixtures: cannot create %s: %s\n", dir.string().c_str(),
                     ec.message().c_str());
        return 1;
    }
    const std::vector<uint8_t> rgb = make_gradient_rgb();
    Exiv2::ExifData exif = standard_exif();
    const std::string exif_payload = exif_blob(exif);
    const std::string xmp = xmp_packet();
    std::string err;

    // exif_full.jpg - jpegli, distance 1.0, 4:4:4, no downsampling, then Exiv2 EXIF.
    const fs::path jpg = dir / "exif_full.jpg";
    if (!write_jpegli_jpeg(jpg, rgb, kSize, kSize, 1.0f, err)) {
        emit("MADE", "exif_full.jpg", "FAIL", err);
    } else if (!write_exif_metadata(jpg, exif, err)) {
        emit("MADE", "exif_full.jpg", "FAIL", err);
    } else {
        emit("MADE", "exif_full.jpg", "OK", "jpegli distance=1.0 4:4:4 + exiv2 exif/gps");
    }

    // webp_lossy.webp - quality 90, sharp YUV.
    if (write_webp(dir / "webp_lossy.webp", rgb, kSize, kSize, false, err)) {
        emit("MADE", "webp_lossy.webp", "OK", "quality=90 use_sharp_yuv=1");
    } else {
        emit("MADE", "webp_lossy.webp", "FAIL", err);
    }

    // webp_lossless.webp - lossless, exact.
    if (write_webp(dir / "webp_lossless.webp", rgb, kSize, kSize, true, err)) {
        emit("MADE", "webp_lossless.webp", "OK", "lossless=1 exact=1");
    } else {
        emit("MADE", "webp_lossless.webp", "FAIL", err);
    }

    // heif_exif.heic - libheif x265, quality 50, chroma 444, EXIF blob.
    std::string used;
    if (write_heif(dir / "heif_exif.heic", rgb, kSize, kSize, heif_compression_HEVC, "x265",
                   exif_payload, used, err)) {
        emit("MADE", "heif_exif.heic", "OK", "encoder=" + used + " quality=50 chroma=444 exif");
    } else {
        emit("MADE", "heif_exif.heic", "FAIL", err);
    }

    // avif_exif.avif - libheif AV1 encoder, aom preferred, EXIF blob.
    used.clear();
    if (write_heif(dir / "avif_exif.avif", rgb, kSize, kSize, heif_compression_AV1, "aom",
                   exif_payload, used, err)) {
        emit("MADE", "avif_exif.avif", "OK", "encoder=" + used + " exif");
    } else {
        emit("MADE", "avif_exif.avif", "FAIL", err);
    }

    // jxl_exif.jxl - libjxl effort 7, distance 1.0, Exif box + XMP box.
    if (write_jxl(dir / "jxl_exif.jxl", rgb, kSize, kSize, exif_payload, xmp, err)) {
        emit("MADE", "jxl_exif.jxl", "OK", "effort=7 distance=1.0 exif-box xmp-box");
    } else {
        emit("MADE", "jxl_exif.jxl", "FAIL", err);
    }

    // cmyk.tif - libtiff PHOTOMETRIC_SEPARATED, 8bpc, 4 samples.
    if (write_cmyk_tiff(dir / "cmyk.tif", kSize, kSize, err)) {
        emit("MADE", "cmyk.tif", "OK", "libtiff separated 8bpc 4 samples");
    } else {
        emit("MADE", "cmyk.tif", "FAIL", err);
    }
    return g_fails;
}

int cmd_verify(const fs::path& dir) {
    const std::vector<std::string> names = {"exif_full.jpg", "webp_lossy.webp",
                                            "webp_lossless.webp", "heif_exif.heic",
                                            "avif_exif.avif",    "jxl_exif.jxl",
                                            "cmyk.tif"};
    if (!fs::is_directory(dir)) {
        for (const std::string& n : names) {
            emit("FIXTURE", n, "SKIP", "directory missing: " + dir.string());
        }
        return 77;
    }
    // Exiv2 >= 0.28 API (verified against the installed headers).

    // exif_full.jpg: DateTimeOriginal + Artist + GPS round-trip.
    {
        const fs::path p = dir / "exif_full.jpg";
        if (!fs::exists(p)) {
            emit("FIXTURE", "exif_full.jpg", "SKIP", "missing");
        } else if (!check_metadata_field(p, "exif_full.jpg", "Exif.Photo.DateTimeOriginal",
                                         kDateTimeOriginal) ||
                   !check_metadata_field(p, "exif_full.jpg", "Exif.Image.Artist", kArtist)) {
            // failures already reported
        } else {
            std::string lat_s;
            std::string lon_s;
            std::string err;
            double lat = 0.0;
            double lon = 0.0;
            if (!read_exif_value(p, "Exif.GPSInfo.GPSLatitude", lat_s, err)) {
                emit("FIXTURE", "exif_full.jpg", "FAIL", "Exif.GPSInfo.GPSLatitude: " + err);
            } else if (!read_exif_value(p, "Exif.GPSInfo.GPSLongitude", lon_s, err)) {
                emit("FIXTURE", "exif_full.jpg", "FAIL", "Exif.GPSInfo.GPSLongitude: " + err);
            } else if (!gps_to_decimal(lat_s, lat) || !gps_to_decimal(lon_s, lon)) {
                emit("FIXTURE", "exif_full.jpg", "FAIL",
                     "GPS parse failed: " + lat_s + " / " + lon_s);
            } else if (std::fabs(lat - kLatDeg) > kGpsTolerance ||
                       std::fabs(lon - kLonDeg) > kGpsTolerance) {
                emit("FIXTURE", "exif_full.jpg", "FAIL",
                     "GPS=" + fmt_double(lat) + "," + fmt_double(lon));
            } else {
                emit("FIXTURE", "exif_full.jpg", "OK",
                     "DateTimeOriginal+Artist+GPS(" + fmt_double(lat) + "," + fmt_double(lon) +
                         ") round-trip");
            }
        }
    }

    // webp fixtures: OIIO must open both.
    for (const char* n : {"webp_lossy.webp", "webp_lossless.webp"}) {
        const fs::path p = dir / n;
        std::string err;
        if (!fs::exists(p)) {
            emit("FIXTURE", n, "SKIP", "missing");
        } else if (!oiio_check(p, kSize, kSize, err)) {
            emit("FIXTURE", n, "FAIL", err);
        } else {
            emit("FIXTURE", n, "OK", "oiio " + err);
        }
    }

    // container fixtures: DateTimeOriginal readback through Exiv2.
    for (const char* n : {"heif_exif.heic", "avif_exif.avif", "jxl_exif.jxl"}) {
        const fs::path p = dir / n;
        if (!fs::exists(p)) {
            emit("FIXTURE", n, "SKIP", "missing");
            continue;
        }
        if (!check_metadata_field(p, n, "Exif.Photo.DateTimeOriginal", kDateTimeOriginal)) {
            continue;
        }
        if (std::strcmp(n, "jxl_exif.jxl") == 0) {
            std::string err;
            if (!oiio_check(p, kSize, kSize, err)) {
                emit("FIXTURE", n, "FAIL", "exif ok but " + err);
                continue;
            }
            emit("FIXTURE", n, "OK", "exif DateTimeOriginal + oiio " + err);
        } else {
            emit("FIXTURE", n, "OK", "exif DateTimeOriginal round-trip");
        }
    }

    // cmyk.tif must open in OIIO.
    {
        const fs::path p = dir / "cmyk.tif";
        std::string err;
        if (!fs::exists(p)) {
            emit("FIXTURE", "cmyk.tif", "SKIP", "missing");
        } else if (!oiio_check(p, kSize, kSize, err)) {
            emit("FIXTURE", "cmyk.tif", "FAIL", err);
        } else {
            emit("FIXTURE", "cmyk.tif", "OK", "oiio " + err);
        }
    }
    return g_fails;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    if (args.size() == 2 && args[0] == "--make") {
        return cmd_make(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "--verify") {
        return cmd_verify(fs::path(args[1]));
    }
    std::fprintf(stderr, "usage: pp_mkfixtures --make <dir> | --verify <dir>\n");
    return 2;
}
