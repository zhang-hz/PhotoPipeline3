// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.5（依据 docs/v0.3.0-design.md §3.5，"加性"）**已落地**
//   本文件内 EffectiveField / EffectivePreview / preview_effective 为 0.3.0 一次性解冻（D20）
//   授权变更面（新增；纯函数、无副作用）；落地任务 = W1-T8（classify + 生效值），
//   原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记（冻结头 SPDX 延续）。
//   本文件其余声明（BatchRules / MetadataOverride / build_plan / 写路径矩阵 / SourceMeta /
//   单测纯函数族）签名与语义均不变 → 维持 PP-FROZEN 只读。
//   注：`build_plan()` 的函数体在 T8 内**内部重构**（抽共用纯函数 effective_rules/
//   shifted_exif_time，供 preview_effective 复用），签名、语义、警告与写路径矩阵零改动。
#pragma once
#include "core/types.h"
#include <cstdint>
#include <exiv2/exiv2.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct SourceMeta {
    Exiv2::ExifData exif;
    Exiv2::XmpData xmp;
    std::string icc; // 嵌入 ICC（可空）
    bool has_time = false;
    bool has_gps = false;
    int orientation = 1; // EXIF Orientation 1–8
    std::string error;   // 非空=读取失败（调用方决定是否致命）
};

SourceMeta read_metadata(const std::filesystem::path &p);

// —— 编辑模型 ——
struct TagEdit {
    std::string key;
    std::optional<std::string> value;
    bool remove = false;
};

struct TimeShift {
    enum class Mode { Delta, TimezoneSemantic } mode = Mode::Delta;
    int years = 0, months = 0, days = 0, hours = 0, minutes = 0, seconds = 0; // Delta
    int from_offset_min = 0, to_offset_min = 0;                               // Timezone
    bool is_noop() const;
};

struct GpsData {
    double lat = 0, lon = 0; // 十进制度
    std::optional<double> altitude;
    std::optional<double> direction;      // 0–359.99
    std::optional<std::string> timestamp; // "YYYY:MM:DD HH:MM:SS"
};

struct BatchRules {
    std::optional<TimeShift> time_shift;
    std::optional<GpsData> gps;
    bool gps_clear = false; // 一键清除 GPS（优先级高于 gps）
    std::vector<TagEdit> exif_edits, xmp_edits;
    bool strip_privacy = false; // 删全部 EXIF+XMP，保留 ICC 与像素
    bool sync_mtime = false;
    bool is_noop() const;
};

struct MetadataOverride {      // 单文件例外（三态：继承/覆盖/清除）
    bool ignore_batch = false; // 整单忽略批量规则
    std::optional<TimeShift> time_shift;
    std::optional<GpsData> gps;
    bool gps_clear = false;
    std::vector<TagEdit> exif_edits, xmp_edits;
    std::optional<bool> strip_privacy;
};

struct MetadataPlan {
    Exiv2::ExifData exif;
    Exiv2::XmpData xmp;
    std::vector<Warning> warnings;
    bool has_time = false;
    std::string datetime_original; // effective 值（mtime 同步用，可空）
};

// effective = 源 ⊕ BatchRules ⊕ 例外（例外逐项覆盖；ignore_batch 先清空批量规则）
MetadataPlan build_plan(const SourceMeta &src, const BatchRules &rules,
                        const std::optional<MetadataOverride> &ex);

// PP-FROZEN(0.3.0) §3.5 · EffectiveField / EffectivePreview / preview_effective（**已落地**）
//   落地任务 = W1-T8（classify + 生效值；出口 `test_effective` 绿：生效值金值 + 与 build_plan
//   同源对拍）；原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记。
//   0.3.0 冻结形态（设计 §3.5 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// // 追加：生效值预览（纯函数，无副作用；供元数据卡即时显示）
// template <class T> struct EffectiveField { T original; T effective; bool changed; };
// struct EffectivePreview {
//     EffectiveField<std::optional<DateTimeVal>> datetime_original, datetime_digitized, datetime_modify;
//     EffectiveField<std::optional<GpsData>>     gps;
//     bool strip_privacy = false, sync_mtime = false;
// };
// EffectivePreview preview_effective(const SourceProbe&, const BatchRules&,
//                                    const std::optional<MetadataOverride>&);
// // BatchRules / MetadataOverride / build_plan / 写路径矩阵 均不变
// clang-format on
//   落定名（§3.5 未给出 `DateTimeVal`/`SourceProbe` 的定义；0.2 现形源码中两者都不存在，
//   M4-T1 已上报 → T8 按 §5.1 语义定稿，W5 收口入 m4-report）：
//     * `DateTimeVal` = EXIF 2.3 形态的时间值（`value` = "YYYY:MM:DD HH:MM:SS"，与本层其余
//       时间字段同口径；时区语义模式下的墙钟改写即体现在该值上）。
//     * `SourceProbe` = `SourceMeta`（【机械性】命名映射：0.2 起 probe 结果类型就是 SourceMeta，
//       `read_metadata()` 即 probe 入口；不新增重复类型）。
//   §5.1 语义（逐字摘录）：`preview_effective(probe, rules, exception)` 与 `build_plan()`
//   **共用同一套合成逻辑**（抽纯函数，杜绝显示/写入两张皮）；时间对 DateTimeOriginal /
//   DateTimeDigitized / DateTime(ModifyDate) 各出一 `EffectiveField<DateTimeVal>`；GPS 为
//   `EffectiveField<GpsData>`（规则未配置 → effective=original；隐私剥除开 → effective=nullopt）；
//   `changed` = (effective ≠ original)。
//   落地面（T8 口径，逐条对拍见 tests/unit/test_effective.cpp）：
//     * 合成逻辑单源 = metadata.cpp 的 `effective_rules()`（BatchRules ⊕ 例外：逐项覆盖、
//       ignore_batch 先清空批量规则）与 `shifted_exif_time()`（Δ/时区语义都走既有
//       `shift_exif_datetime` → `reinterpret_timezone`）；`build_plan()` 与 `preview_effective()`
//       共用这两个纯函数（无第二条合成路径）。
//     * 三个时间字段 = 三个 EXIF 时间标签（Exif.Photo.DateTimeOriginal /
//       Exif.Photo.DateTimeDigitized / Exif.Image.DateTime；ExifTool 名依次为
//       DateTimeOriginal / CreateDate / ModifyDate）。
//       XMP 日期（xmp:CreateDate/ModifyDate）由 build_plan 一并改写，但**不在**本预览三字段内
//       （§5.1 逐字只列三个 EXIF 字段）；值缺失/不可解析 → effective = original（build_plan
//       在这些情形同样跳过写入）。
//     * 隐私剥除（strip_privacy）：三个时间字段与 GPS 的 effective 全为 nullopt（build_plan 剥除后
//       不再写任何 EXIF/XMP 字段）；"将被移除"的显示语义由 UI 层负责（§5.1）。
//     * GPS：规则未配置 → effective = original（从 probe 的 Exif.GPSInfo.* 读回）；隐私剥除或
//       gps_clear → effective = nullopt；规则配置 → effective = 规则值。
//     * 纯函数零副作用：只读 probe（const&），不写文件、不改注册表、不落日志。
struct DateTimeVal {
    std::string value; // EXIF 2.3 形态 "YYYY:MM:DD HH:MM:SS"
    bool operator==(const DateTimeVal &) const = default;
};
using SourceProbe = SourceMeta; // §3.5 的 SourceProbe（= 0.2 起的 probe 类型 SourceMeta）
template <class T> struct EffectiveField {
    T original;
    T effective;
    bool changed;
};
struct EffectivePreview {
    EffectiveField<std::optional<DateTimeVal>> datetime_original, datetime_digitized,
        datetime_modify;
    EffectiveField<std::optional<GpsData>> gps;
    bool strip_privacy = false, sync_mtime = false;
};
EffectivePreview preview_effective(const SourceProbe &probe, const BatchRules &rules,
                                   const std::optional<MetadataOverride> &ex);
// —— 载荷（G1）——
struct Payloads {
    std::string exif_blob;
    std::string xmp_rdf;
};
// exif_blob = Exiv2::ExifParser::encode(blob, littleEndian, plan.exif)（M0 实测 API）
// xmp_rdf   = XmpParser::initialize() 后 XmpParser::encode(packet, plan.xmp)
Payloads make_payloads(const MetadataPlan &plan);

// —— 写入路径 ——
// 路径 A（JPEG/PNG/TIFF/WebP）：编码完成后 Exiv2 后写；返回空串=成功
// PNG 的 eXIf 若写入失败（R1）→ 关键 EXIF 字段镜像到 XMP + Warning{MetadataDropped}
// M2-T4（加性）：`out_warnings` 为可选出参（默认 nullptr），承载写路径的失败/降级消息，
// 文案与既有 plan.warnings 逐字节一致；既有 3 参调用点无需改动即可编译且行为不变。
std::string write_metadata_exiv2(const std::filesystem::path &out_file, const MetadataPlan &plan,
                                 const Payloads &payloads,
                                 std::vector<std::string> *out_warnings = nullptr);

// 路径 B/C（HEIF/AVIF/JXL）：编码器内注入，本函数只负责给编码器提供载荷 → 见 §3.8

// 仅元数据模式（同格式零重编码）：JPEG/PNG/TIFF/WebP 走 Exiv2 无损重写
// 返回空串=成功；HEIF/AVIF 由调用方在 UI 层置灰
std::string rewrite_metadata_only(const std::filesystem::path &src,
                                  const std::filesystem::path &out, const MetadataPlan &plan,
                                  const Payloads &payloads);

// mtime 同步：exif_datetime 为空 → 不动；格式 "YYYY:MM:DD HH:MM:SS"（本地时区）
std::string sync_file_mtime(const std::filesystem::path &out, const std::string &exif_datetime);

// —— 可单测纯函数 ——
// 时间偏移：Delta（月/年按日历）与时区语义（改写墙钟 + 写 OffsetTime*）
// EXIF 日期格式 "YYYY:MM:DD HH:MM:SS"；非法 → ok=false 且返回原串
std::string shift_exif_datetime(const std::string &exif_dt, const TimeShift &s, bool &ok);
std::string offset_time_string(int offset_min); // 480 → "+08:00"
// 时区语义：把"按时区 A 解释的时刻"改写为"时区 B 的墙钟时间"
std::string reinterpret_timezone(const std::string &exif_dt, int from_min, int to_min, bool &ok);

// GPS 有理数编码（G7：正值 Rational + 半球 ref）
// 写入 Exif.GPSInfo.GPSLatitude/GPSLatitudeRef/GPSLongitude/GPSLongitudeRef/GPSVersionID
// 以及可选的 GPSAltitude(Ref)/GPSImgDirection(Ref)/GPSTimeStamp+GPSDateStamp
void write_gps(Exiv2::ExifData &exif, const GpsData &gps);
void clear_gps(Exiv2::ExifData &exif);

// 隐私剥除：清空 exif/xmp（保留 ICC 与像素）
void strip_privacy(Exiv2::ExifData &exif, Exiv2::XmpData &xmp);

// 应用编辑列表（set/remove；key 为 Exiv2 全名，如 Exif.Image.Artist / Xmp.dc.title）
// 无效 key 或类型不符 → 记入 errors（不抛异常）
void apply_edits(Exiv2::ExifData &exif, Exiv2::XmpData &xmp, const std::vector<TagEdit> &exif_edits,
                 const std::vector<TagEdit> &xmp_edits, std::vector<std::string> &errors);

// 读取"拍摄时间"（DateTimeOriginal → DateTime → Xmp.xmp:CreateDate）
std::string effective_datetime(const Exiv2::ExifData &exif, const Exiv2::XmpData &xmp);

} // namespace pp
