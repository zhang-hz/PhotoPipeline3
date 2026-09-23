// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <exiv2/exiv2.hpp>
#include "core/types.h"

namespace pp {

struct SourceMeta {
    Exiv2::ExifData exif;
    Exiv2::XmpData  xmp;
    std::string icc;                       // 嵌入 ICC（可空）
    bool has_time = false;
    bool has_gps  = false;
    int  orientation = 1;                  // EXIF Orientation 1–8
    std::string error;                     // 非空=读取失败（调用方决定是否致命）
};

SourceMeta read_metadata(const std::filesystem::path& p);

// —— 编辑模型 ——
struct TagEdit { std::string key; std::optional<std::string> value; bool remove = false; };

struct TimeShift {
    enum class Mode { Delta, TimezoneSemantic } mode = Mode::Delta;
    int years = 0, months = 0, days = 0, hours = 0, minutes = 0, seconds = 0;  // Delta
    int from_offset_min = 0, to_offset_min = 0;                                // Timezone
    bool is_noop() const;
};

struct GpsData {
    double lat = 0, lon = 0;               // 十进制度
    std::optional<double> altitude;
    std::optional<double> direction;       // 0–359.99
    std::optional<std::string> timestamp;  // "YYYY:MM:DD HH:MM:SS"
};

struct BatchRules {
    std::optional<TimeShift> time_shift;
    std::optional<GpsData>   gps;
    bool gps_clear = false;                // 一键清除 GPS（优先级高于 gps）
    std::vector<TagEdit> exif_edits, xmp_edits;
    bool strip_privacy = false;            // 删全部 EXIF+XMP，保留 ICC 与像素
    bool sync_mtime = false;
    bool is_noop() const;
};

struct MetadataOverride {                  // 单文件例外（三态：继承/覆盖/清除）
    bool ignore_batch = false;             // 整单忽略批量规则
    std::optional<TimeShift> time_shift;
    std::optional<GpsData>   gps;
    bool gps_clear = false;
    std::vector<TagEdit> exif_edits, xmp_edits;
    std::optional<bool> strip_privacy;
};

struct MetadataPlan {
    Exiv2::ExifData exif;
    Exiv2::XmpData  xmp;
    std::vector<Warning> warnings;
    bool has_time = false;
    std::string datetime_original;         // effective 值（mtime 同步用，可空）
};

// effective = 源 ⊕ BatchRules ⊕ 例外（例外逐项覆盖；ignore_batch 先清空批量规则）
MetadataPlan build_plan(const SourceMeta& src, const BatchRules& rules,
                        const std::optional<MetadataOverride>& ex);

// —— 载荷（G1）——
struct Payloads { std::string exif_blob; std::string xmp_rdf; };
// exif_blob = Exiv2::ExifParser::encode(blob, littleEndian, plan.exif)（M0 实测 API）
// xmp_rdf   = XmpParser::initialize() 后 XmpParser::encode(packet, plan.xmp)
Payloads make_payloads(const MetadataPlan& plan);

// —— 写入路径 ——
// 路径 A（JPEG/PNG/TIFF/WebP）：编码完成后 Exiv2 后写；返回空串=成功
// PNG 的 eXIf 若写入失败（R1）→ 关键 EXIF 字段镜像到 XMP + Warning{MetadataDropped}
// M2-T4（加性）：`out_warnings` 为可选出参（默认 nullptr），承载写路径的失败/降级消息，
// 文案与既有 plan.warnings 逐字节一致；既有 3 参调用点无需改动即可编译且行为不变。
std::string write_metadata_exiv2(const std::filesystem::path& out_file,
                                 const MetadataPlan& plan, const Payloads& payloads,
                                 std::vector<std::string>* out_warnings = nullptr);

// 路径 B/C（HEIF/AVIF/JXL）：编码器内注入，本函数只负责给编码器提供载荷 → 见 §3.8

// 仅元数据模式（同格式零重编码）：JPEG/PNG/TIFF/WebP 走 Exiv2 无损重写
// 返回空串=成功；HEIF/AVIF 由调用方在 UI 层置灰
std::string rewrite_metadata_only(const std::filesystem::path& src,
                                  const std::filesystem::path& out,
                                  const MetadataPlan& plan, const Payloads& payloads);

// mtime 同步：exif_datetime 为空 → 不动；格式 "YYYY:MM:DD HH:MM:SS"（本地时区）
std::string sync_file_mtime(const std::filesystem::path& out, const std::string& exif_datetime);

// —— 可单测纯函数 ——
// 时间偏移：Delta（月/年按日历）与时区语义（改写墙钟 + 写 OffsetTime*）
// EXIF 日期格式 "YYYY:MM:DD HH:MM:SS"；非法 → ok=false 且返回原串
std::string shift_exif_datetime(const std::string& exif_dt, const TimeShift& s, bool& ok);
std::string offset_time_string(int offset_min);            // 480 → "+08:00"
// 时区语义：把"按时区 A 解释的时刻"改写为"时区 B 的墙钟时间"
std::string reinterpret_timezone(const std::string& exif_dt, int from_min, int to_min, bool& ok);

// GPS 有理数编码（G7：正值 Rational + 半球 ref）
// 写入 Exif.GPSInfo.GPSLatitude/GPSLatitudeRef/GPSLongitude/GPSLongitudeRef/GPSVersionID
// 以及可选的 GPSAltitude(Ref)/GPSImgDirection(Ref)/GPSTimeStamp+GPSDateStamp
void write_gps(Exiv2::ExifData& exif, const GpsData& gps);
void clear_gps(Exiv2::ExifData& exif);

// 隐私剥除：清空 exif/xmp（保留 ICC 与像素）
void strip_privacy(Exiv2::ExifData& exif, Exiv2::XmpData& xmp);

// 应用编辑列表（set/remove；key 为 Exiv2 全名，如 Exif.Image.Artist / Xmp.dc.title）
// 无效 key 或类型不符 → 记入 errors（不抛异常）
void apply_edits(Exiv2::ExifData& exif, Exiv2::XmpData& xmp,
                 const std::vector<TagEdit>& exif_edits,
                 const std::vector<TagEdit>& xmp_edits,
                 std::vector<std::string>& errors);

// 读取"拍摄时间"（DateTimeOriginal → DateTime → Xmp.xmp:CreateDate）
std::string effective_datetime(const Exiv2::ExifData& exif, const Exiv2::XmpData& xmp);

}  // namespace pp
