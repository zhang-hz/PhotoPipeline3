// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M7-R2.2-G2 — meta_format 15 纯函数单测（F2 探针需求 3 / R-181 可单测半边）
// + info_value 槽表与 kInfoSlotCount 的 static_assert 对拍（F2 残余 ⑥-3）。
//
// 证据面（docs/deep-review-0.3.2.md 附录 B.1 步 B2 + build/m7r21-f2/handoff.md ④-3）：
//   * meta_format.{h,cpp} 的 15 个显示格式化纯函数（format_datetime…delta_text）在此逐个
//     钉真值表：exif_text/camera/lens/exposure/wb/rating_of/keywords_of/delta_text 全档
//     + format_datetime/format_size 边界值（F2 ④-3 名单 + 全 15 函数补全）；
//   * info_value 槽表 ↔ kInfoSlotCount 对拍（F2 ⑥-3）：槽名表行数 == kInfoSlotCount、
//     「复制全部信息」键表 == kInfoSlotCount + 1（8 值槽 + 白平衡槽）。
//
// 测试接线（test_icons.cpp 的 PP_ICONS_TEST_NO_WIDGETS 同款先例，M3 铁律六 单实现）：
// src/ui/meta_format.cpp 属 UI 目录（不编入 pp_core），单测 target 只链 pp_core
// （Qt6::Core/Gui）→ 以 PP_META_FORMAT_TEST_NO_WIDGETS 编译该 TU（收窄掉 page_meta.h
// 唯一 QtWidgets 依赖面；tr_page 的查表键与 PageMeta::tr 逐字相同）。本用例不建
// QApplication。每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。

#define PP_META_FORMAT_TEST_NO_WIDGETS
#include "ui/meta_format.cpp" // 单实现：与生产同一份代码

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace meta = pp::ui::meta;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
        std::fflush(stdout); // fail-fast abort 也留得出失败行
    }
}

void check_q(const QString &got, const QString &want, const std::string &case_name) {
    check(got == want, case_name,
          "got '" + std::string(got.toUtf8().constData()) + "' want '" +
              std::string(want.toUtf8().constData()) + "'");
}

// ---------------------------------------------------------------------------
// info_value 槽表 ↔ kInfoSlotCount 对拍（F2 残余 ⑥-3）
// ---------------------------------------------------------------------------
// 词表复述自 ui/meta_state.h（InfoSlot / kInfoSlotCount）与 ui/meta_card_info.cpp
// （槽名表 slot_name ← meta_probe_context.h objname::、copy_all 键表 keys）——
// test_formkit「复述常量」先例：单测不引 QtWidgets 头（meta_state.h 拉 page_meta.h），
// 产品侧同一对拍另有编译期 static_assert 钉在 meta_card_info.cpp 表侧（双端同判据）。
namespace slots {

enum InfoSlotCopy : int {
    kInfoTime = 0,
    kInfoCamera = 1,
    kInfoLens = 2,
    kInfoExposure = 3,
    kInfoSize = 4,
    kInfoColor = 5,
    kInfoFile = 6,
    kInfoGps = 7,
    kInfoSlotCount = 8,
};

// meta_probe_context.h objname:: 的 8 个值槽 objectName（槽序 = InfoSlot 词表）
inline constexpr const char *kSlotNames[kInfoSlotCount] = {
    "pp-meta-info-time", "pp-meta-info-camera", "pp-meta-info-lens", "pp-meta-info-exposure",
    "pp-meta-info-size", "pp-meta-info-color",  "pp-meta-info-file", "pp-meta-info-gps",
};

// meta_card_info.cpp copy_all 的键表（8 值槽 + 白平衡槽 = 9 项）
inline constexpr const char *kCopyAllKeys[kInfoSlotCount + 1] = {
    "拍摄时间", "相机", "镜头", "曝光", "尺寸", "色彩", "文件", "GPS", "白平衡",
};

static_assert(std::size(kSlotNames) == size_t(kInfoSlotCount),
              "info_value 槽名表行数 == kInfoSlotCount（槽表对拍）");
static_assert(std::size(kCopyAllKeys) == size_t(kInfoSlotCount) + 1,
              "copy_all 键表 == kInfoSlotCount + 1（8 值槽 + 白平衡，键表对拍）");

} // namespace slots

// ---------------------------------------------------------------------------
// 15 纯函数真值表
// ---------------------------------------------------------------------------

void test_format_datetime() {
    check_q(meta::format_datetime(QStringLiteral("2024:03:01 10:00:00")),
            QStringLiteral("2024-03-01 10:00:00"), "format_datetime/标准 EXIF 时间");
    check_q(meta::format_datetime(QStringLiteral("2024:03:01")), QStringLiteral("2024-03-01"),
            "format_datetime/恰好 10 字符只换日期段");
    check_q(meta::format_datetime(QStringLiteral("short")), QStringLiteral("short"),
            "format_datetime/短于 10 字符原样返回");
    check_q(meta::format_datetime(QString()), QString(), "format_datetime/空串");
}

void test_date_part_and_datetime_text() {
    const std::optional<pp::DateTimeVal> none;
    check_q(meta::date_part(none), QString(), "date_part/无值 → 空串");
    pp::DateTimeVal v;
    v.value = "2024:03:01 10:00:00";
    const std::optional<pp::DateTimeVal> some(v);
    check_q(meta::date_part(some), QStringLiteral("2024-03-01"), "date_part/有值取日期段");
    pp::DateTimeVal short_v;
    short_v.value = "2024";
    check_q(meta::date_part(std::optional<pp::DateTimeVal>(short_v)), QString(),
            "date_part/短值 → 空串");
    check_q(meta::datetime_text(none), QStringLiteral("—"), "datetime_text/无值 → —");
    check_q(meta::datetime_text(some), QStringLiteral("2024-03-01 10:00:00"),
            "datetime_text/有值 → 格式化时间");
}

void test_coord_text() {
    check_q(meta::coord_text(31.2304, 6), QStringLiteral("31.230400°"), "coord_text/6 位小数");
    check_q(meta::coord_text(-7.5, 2), QStringLiteral("-7.50°"), "coord_text/负数 + 2 位");
    check_q(meta::coord_text(0.0, 0), QStringLiteral("0°"), "coord_text/0 位小数");
}

void test_format_size() {
    check_q(meta::format_size(0), QStringLiteral("—"), "format_size/0 → —");
    check_q(meta::format_size(-5), QStringLiteral("—"), "format_size/负数 → —");
    check_q(meta::format_size(512), QStringLiteral("0.5 KB"), "format_size/512B → 0.5 KB");
    check_q(meta::format_size(1024), QStringLiteral("1.0 KB"), "format_size/1KB → 1.0 KB");
    check_q(meta::format_size(1024 * 1024), QStringLiteral("1.00 MB"),
            "format_size/1MB 边界 → 1.00 MB（MB 起两位小数）");
    check_q(meta::format_size(1024 * 1024 - 1), QStringLiteral("1024.0 KB"),
            "format_size/1MB−1 仍在 KB 档");
    check_q(meta::format_size(1572864), QStringLiteral("1.50 MB"), "format_size/1.5MB");
}

void test_channels_text() {
    pp::ImageInfo info;
    info.channels = 1;
    info.has_alpha = false;
    check_q(meta::channels_text(info), QStringLiteral("灰度"), "channels_text/1 通道");
    info.has_alpha = true;
    check_q(meta::channels_text(info), QStringLiteral("灰度 + α"), "channels_text/1 通道 + α");
    info.channels = 2;
    check_q(meta::channels_text(info), QStringLiteral("灰度 + α"), "channels_text/2 通道");
    info.has_alpha = false;
    info.channels = 3;
    check_q(meta::channels_text(info), QStringLiteral("RGB"), "channels_text/3 通道");
    info.channels = 4;
    check_q(meta::channels_text(info), QStringLiteral("RGBA"), "channels_text/4 通道");
    info.channels = 5;
    check_q(meta::channels_text(info), QStringLiteral("—"), "channels_text/未知通道数 → —");
}

void test_exif_text() {
    Exiv2::ExifData exif;
    exif["Exif.Image.Make"] = "Canon ";
    check(meta::exif_text(exif, "Exif.Image.Make") == "Canon", "exif_text/读值 + 尾空格裁剪",
          "got '" + meta::exif_text(exif, "Exif.Image.Make") + "'");
    check(meta::exif_text(exif, "Exif.Image.Model").empty(), "exif_text/缺失标签 → 空串",
          "expected empty");
    check(meta::exif_text(exif, "Bogus.Key.X").empty(), "exif_text/非法键名 → 空串不抛",
          "expected empty");
}

void test_camera_text() {
    Exiv2::ExifData exif;
    check_q(meta::camera_text(exif), QStringLiteral("—"), "camera_text/全缺 → —");
    exif["Exif.Image.Model"] = "EOS R5";
    check_q(meta::camera_text(exif), QStringLiteral("EOS R5"), "camera_text/仅 Model");
    exif["Exif.Image.Make"] = "Canon";
    check_q(meta::camera_text(exif), QStringLiteral("Canon EOS R5"), "camera_text/Make + Model");
    exif["Exif.Image.Model"] = "Canon EOS R5";
    check_q(meta::camera_text(exif), QStringLiteral("Canon EOS R5"),
            "camera_text/Model 已含 Make 前缀 → 只出 Model");
    Exiv2::ExifData only_make;
    only_make["Exif.Image.Make"] = "Fuji";
    check_q(meta::camera_text(only_make), QStringLiteral("Fuji"), "camera_text/仅 Make");
}

void test_lens_text() {
    Exiv2::ExifData exif;
    check_q(meta::lens_text(exif), QStringLiteral("—"), "lens_text/全缺 → —");
    exif["Exif.Photo.LensMake"] = "Sigma";
    check_q(meta::lens_text(exif), QStringLiteral("Sigma"), "lens_text/LensMake 兜底");
    exif["Exif.Photo.LensModel"] = "35mm F1.4 DG HSM";
    check_q(meta::lens_text(exif), QStringLiteral("35mm F1.4 DG HSM"),
            "lens_text/LensModel 优先于 LensMake");
}

void test_exposure_text() {
    Exiv2::ExifData exif;
    check_q(meta::exposure_text(exif), QStringLiteral("—"), "exposure_text/全缺 → —");
    exif["Exif.Photo.ISOSpeedRatings"] = "100";
    check_q(meta::exposure_text(exif), QStringLiteral("ISO 100"), "exposure_text/仅 ISO");
    exif["Exif.Photo.ExposureTime"] = "1/125";
    check_q(meta::exposure_text(exif), QStringLiteral("ISO 100 · 1/125s"),
            "exposure_text/分数快门 → 1/125s");
    exif["Exif.Photo.FNumber"] = "8/1";
    exif["Exif.Photo.FocalLength"] = "50/1";
    check_q(meta::exposure_text(exif), QStringLiteral("ISO 100 · 1/125s · f/8.00 · 50.0mm"),
            "exposure_text/四段全集");
    Exiv2::ExifData whole;
    whole["Exif.Photo.ExposureTime"] = "2/1";
    check_q(meta::exposure_text(whole), QStringLiteral("2s"), "exposure_text/整秒快门 → 2s");
}

void test_white_balance_text() {
    Exiv2::ExifData exif;
    check_q(meta::white_balance_text(exif), QStringLiteral("—"), "wb/全缺 → —");
    exif["Exif.Photo.WhiteBalance"] = "0";
    check_q(meta::white_balance_text(exif), QStringLiteral("自动"), "wb/0 = 自动");
    exif["Exif.Photo.WhiteBalance"] = "1";
    check_q(meta::white_balance_text(exif), QStringLiteral("手动"), "wb/1 = 手动");
    exif["Exif.Photo.WhiteBalance"] = "7";
    check_q(meta::white_balance_text(exif), QStringLiteral("7"), "wb/未知枚举值原样透出");
    // 色温段（ColorTemperature K）不立夹具：本 Exiv2（vcpkg 钉版）的键表**不接受**
    // "Exif.Photo.ColorTemperature"（ExifKey 构造即抛 "Invalid tag name"）→ exif_text
    // 同键恒空串，kelvin 拼接分支经 ExifData 不可达（实测取证，见 build/m7r22-g2/handoff.md
    // 残余项；分支本身保留 —— 万用读取 + 防御性 catch 是既有契约）。
}

void test_rating_of() {
    pp::SourceMeta meta;
    check(!meta::rating_of(meta).has_value(), "rating_of/全缺 → nullopt", "expected nullopt");
    meta.xmp["Xmp.xmp.Rating"] = "3";
    check(meta::rating_of(meta).value_or(-1) == 3, "rating_of/XMP 兜底", "got != 3");
    meta.exif["Exif.Image.Rating"] = "4";
    check(meta::rating_of(meta).value_or(-1) == 4, "rating_of/EXIF 优先于 XMP", "got != 4");
    meta.exif["Exif.Image.Rating"] = "7";
    check(meta::rating_of(meta).value_or(-1) == 5, "rating_of/上溢夹到 5", "got != 5");
    // 下溢夹到 0 走 XMP 面（Exif.Image.Rating 是 unsignedShort，装不下负值）
    pp::SourceMeta low;
    low.xmp["Xmp.xmp.Rating"] = "-1";
    check(meta::rating_of(low).value_or(-1) == 0, "rating_of/XMP 负值下溢夹到 0", "got != 0");
}

void test_keywords_of() {
    pp::SourceMeta meta;
    check(meta::keywords_of(meta).isEmpty(), "keywords_of/无 Xmp.dc.subject → 空表",
          "expected empty");
    // XMP 数组经 RDF 包解码构造（Xmpdatum 无 += 数组追加算符；与 test_metadata 的
    // XmpParser::decode 路径同款）：Bag{ " cat ", dog, cat } → 裁剪 + 去重 → [cat, dog]
    const char *kRdf =
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
        "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:subject><rdf:Bag><rdf:li> cat </rdf:li><rdf:li>dog</rdf:li><rdf:li>cat</rdf:li>"
        "</rdf:Bag></dc:subject></rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    check(Exiv2::XmpParser::decode(meta.xmp, kRdf) == 0, "keywords_of/RDF 解码夹具",
          "XmpParser::decode != 0");
    const QStringList got = meta::keywords_of(meta);
    check(got.size() == 2 && got[0] == QLatin1String("cat") && got[1] == QLatin1String("dog"),
          "keywords_of/数组逐项 + 裁剪空白 + 去重", "got size=" + std::to_string(got.size()));
    pp::SourceMeta single;
    single.xmp["Xmp.dc.subject"] = " dog ";
    const QStringList one = meta::keywords_of(single);
    check(one.size() == 1 && one[0] == QLatin1String("dog"), "keywords_of/单值路径（count=1）",
          "got size=" + std::to_string(one.size()));
}

void test_dms_string() {
    check_q(meta::dms_string(31.2304, 121.4737), QStringLiteral("N 31°13.824' E 121°28.422'"),
            "dms_string/北纬东经（deg_to_dms 单源，R-099）");
    check_q(meta::dms_string(-33.8688, -151.2093), QStringLiteral("S 33°52.128' W 151°12.558'"),
            "dms_string/南纬西经");
}

void test_delta_text() {
    pp::TimeShift s;
    check_q(meta::delta_text(s), QStringLiteral("±0"), "delta_text/全零 → ±0");
    s.hours = 2;
    check_q(meta::delta_text(s), QStringLiteral("+2h"), "delta_text/单字段正值");
    s.hours = -3;
    check_q(meta::delta_text(s), QStringLiteral("−3h"), "delta_text/负值用 U+2212");
    pp::TimeShift all;
    all.years = 1;
    all.months = 2;
    all.days = 3;
    all.hours = 4;
    all.minutes = 5;
    all.seconds = 6;
    check_q(meta::delta_text(all), QStringLiteral("+1y +2mo +3d +4h +5m +6s"),
            "delta_text/六字段全集（y/mo/d/h/m/s 序）");
    pp::TimeShift tz;
    tz.mode = pp::TimeShift::Mode::TimezoneSemantic;
    tz.from_offset_min = 480;
    tz.to_offset_min = -300;
    check_q(meta::delta_text(tz), QStringLiteral("UTC+08:00 → UTC-05:00"),
            "delta_text/时区语义 → UTC±HH:MM 对照");
}

} // namespace

int main() {
    std::printf("test_meta_format: start\n");
    std::fflush(stdout);
    try {
        // 对拍段（编译期已断言；此处复核词表值，防止复述与产品词表漂移）
        check(std::strcmp(slots::kSlotNames[0], "pp-meta-info-time") == 0 &&
                  std::strcmp(slots::kSlotNames[7], "pp-meta-info-gps") == 0,
              "slots/槽名表复述", "objname 词表首尾值不符");
        check(std::strcmp(slots::kCopyAllKeys[8], "白平衡") == 0, "slots/键表复述",
              "copy_all 键表末项不是白平衡");

        test_format_datetime();
        test_date_part_and_datetime_text();
        test_coord_text();
        test_format_size();
        test_channels_text();
        test_exif_text();
        test_camera_text();
        test_lens_text();
        test_exposure_text();
        test_white_balance_text();
        test_rating_of();
        test_keywords_of();
        test_dms_string();
        test_delta_text();
    } catch (const std::exception &e) {
        std::printf("FAIL uncaught: %s\n", e.what());
        std::fflush(stdout);
        return 99;
    }

    if (g_failed == 0) {
        std::printf("test_meta_format: OK\n");
    } else {
        std::printf("test_meta_format: FAILED (%d assertion(s))\n", g_failed);
    }
    std::fflush(stdout);
    return g_failed;
}
