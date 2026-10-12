// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页显示格式化纯函数（B.1 拆分步 B2：meta_format）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B2）；契约注释见 meta_format.h。
// 实现 = page_meta.cpp「文本格式化」段**逐字迁出**（M7-R2.1-F2；tr 上下文保持 PageMeta）。
#include "ui/meta_format.h"

#include "mapwidget/dms.h" // M6-T10：坐标显示格式化（度分；deg_to_dms 单源）
#include "ui/page_meta.h"  // PageMeta::tr（迁移不改翻译上下文）

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace pp::ui::meta {

QString format_datetime(const QString &exif_dt) {
    if (exif_dt.size() < 10) {
        return exif_dt;
    }
    QString date = exif_dt.left(10);
    date.replace(QLatin1Char(':'), QLatin1Char('-'));
    return date + exif_dt.mid(10);
}

QString date_part(const std::optional<pp::DateTimeVal> &v) {
    if (!v.has_value() || v->value.size() < 10) {
        return QString();
    }
    return format_datetime(QString::fromStdString(v->value)).left(10);
}

QString datetime_text(const std::optional<pp::DateTimeVal> &v) {
    return v.has_value() ? format_datetime(QString::fromStdString(v->value)) : QStringLiteral("—");
}

QString coord_text(double v, int decimals) {
    return QString::number(v, 'f', decimals) + QStringLiteral("°");
}

QString format_size(qint64 bytes) {
    if (bytes <= 0) {
        return QStringLiteral("—");
    }
    const double mb = double(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0) {
        return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 2));
    }
    return QStringLiteral("%1 KB").arg(QString::number(double(bytes) / 1024.0, 'f', 1));
}

QString channels_text(const pp::ImageInfo &info) {
    switch (info.channels) {
    case 1:
        return info.has_alpha ? PageMeta::tr("灰度 + α") : PageMeta::tr("灰度");
    case 2:
        return PageMeta::tr("灰度 + α");
    case 3:
        return QStringLiteral("RGB");
    case 4:
        return QStringLiteral("RGBA");
    default:
        return QStringLiteral("—");
    }
}

std::string exif_text(const Exiv2::ExifData &exif, const char *key) {
    std::unique_ptr<Exiv2::ExifKey> exif_key;
    try {
        exif_key = std::make_unique<Exiv2::ExifKey>(key);
    } catch (const std::exception &) {
        return {}; // 未知标签名 = 无值（不抛、不猜）
    }
    const auto it = exif.findKey(*exif_key);
    if (it == exif.end()) {
        return {};
    }
    std::string value = it->toString();
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

QString camera_text(const Exiv2::ExifData &exif) {
    const QString make = QString::fromStdString(exif_text(exif, "Exif.Image.Make")).trimmed();
    const QString model = QString::fromStdString(exif_text(exif, "Exif.Image.Model")).trimmed();
    if (!make.isEmpty() && !model.isEmpty() && !model.startsWith(make, Qt::CaseInsensitive)) {
        return make + QLatin1Char(' ') + model;
    }
    if (!model.isEmpty()) {
        return model;
    }
    return make.isEmpty() ? QStringLiteral("—") : make;
}

QString lens_text(const Exiv2::ExifData &exif) {
    const QString model = QString::fromStdString(exif_text(exif, "Exif.Photo.LensModel")).trimmed();
    if (!model.isEmpty()) {
        return model;
    }
    const QString make = QString::fromStdString(exif_text(exif, "Exif.Photo.LensMake")).trimmed();
    return make.isEmpty() ? QStringLiteral("—") : make;
}

QString exposure_text(const Exiv2::ExifData &exif) {
    QStringList parts;
    const std::string iso = exif_text(exif, "Exif.Photo.ISOSpeedRatings");
    if (!iso.empty()) {
        parts << QStringLiteral("ISO %1").arg(QString::fromStdString(iso));
    }
    const auto shutter = exif.findKey(Exiv2::ExifKey("Exif.Photo.ExposureTime"));
    if (shutter != exif.end()) {
        const Exiv2::Rational r = shutter->toRational();
        if (r.first > 0 && r.second > 0) {
            parts << (r.second == 1
                          ? QStringLiteral("%1s").arg(r.first)
                          : QStringLiteral("1/%1s").arg(std::lround(double(r.second) / r.first)));
        }
    }
    const auto fnumber = exif.findKey(Exiv2::ExifKey("Exif.Photo.FNumber"));
    if (fnumber != exif.end()) {
        const Exiv2::Rational r = fnumber->toRational();
        if (r.first > 0 && r.second > 0) {
            parts << QStringLiteral("f/%1").arg(
                QString::number(double(r.first) / r.second, 'f', 2));
        }
    }
    const auto focal = exif.findKey(Exiv2::ExifKey("Exif.Photo.FocalLength"));
    if (focal != exif.end()) {
        const Exiv2::Rational r = focal->toRational();
        if (r.first > 0 && r.second > 0) {
            parts << QStringLiteral("%1mm").arg(
                QString::number(double(r.first) / r.second, 'f', 1));
        }
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" · "));
}

QString white_balance_text(const Exiv2::ExifData &exif) {
    QStringList parts;
    const std::string wb = exif_text(exif, "Exif.Photo.WhiteBalance");
    if (!wb.empty()) {
        const int v = std::atoi(wb.c_str());
        parts << (v == 0 ? PageMeta::tr("自动")
                         : (v == 1 ? PageMeta::tr("手动") : QString::fromStdString(wb)));
    }
    const std::string kelvin = exif_text(exif, "Exif.Photo.ColorTemperature");
    if (!kelvin.empty()) {
        parts << QStringLiteral("%1K").arg(QString::fromStdString(kelvin));
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" · "));
}

std::optional<int> rating_of(const pp::SourceMeta &meta) {
    const std::string exif_value = exif_text(meta.exif, "Exif.Image.Rating");
    if (!exif_value.empty()) {
        bool ok = false;
        const int value = QString::fromStdString(exif_value).toInt(&ok);
        if (ok) {
            return std::clamp(value, 0, 5);
        }
    }
    try {
        const Exiv2::XmpKey key("Xmp.xmp.Rating");
        const auto it = meta.xmp.findKey(key);
        if (it != meta.xmp.end()) {
            bool ok = false;
            const int value = QString::fromStdString(it->toString()).toInt(&ok);
            if (ok) {
                return std::clamp(value, 0, 5);
            }
        }
    } catch (const std::exception &) {
        // 未知 XMP 键 = 无值
    }
    return std::nullopt;
}

QStringList keywords_of(const pp::SourceMeta &meta) {
    QStringList out;
    try {
        const Exiv2::XmpKey key("Xmp.dc.subject");
        const auto it = meta.xmp.findKey(key);
        if (it == meta.xmp.end()) {
            return out;
        }
        const std::size_t count = std::max<std::size_t>(1, it->count());
        for (std::size_t i = 0; i < count; ++i) {
            const QString value = QString::fromStdString(it->toString(i)).trimmed();
            if (!value.isEmpty() && !out.contains(value)) {
                out << value;
            }
        }
    } catch (const std::exception &) {
        // 未知 XMP 键 = 无值
    }
    return out;
}

// R-099（M7-WP7）：DMS 文案**统一走 pp::map::deg_to_dms 单源**（迁出自 page_meta.cpp）。
QString dms_string(double lat, double lon) {
    return pp::map::deg_to_dms(lat, true) + QLatin1Char(' ') + pp::map::deg_to_dms(lon, false);
}

QString delta_text(const pp::TimeShift &shift) {
    if (shift.mode == pp::TimeShift::Mode::TimezoneSemantic) {
        return QStringLiteral("UTC%1 → UTC%2")
            .arg(QString::fromStdString(pp::offset_time_string(shift.from_offset_min)),
                 QString::fromStdString(pp::offset_time_string(shift.to_offset_min)));
    }
    QStringList parts;
    const auto add = [&parts](int value, const QString &unit) {
        if (value == 0) {
            return;
        }
        parts << QStringLiteral("%1%2%3")
                     .arg(value > 0 ? QStringLiteral("+") : QStringLiteral("−"))
                     .arg(std::abs(value))
                     .arg(unit);
    };
    add(shift.years, PageMeta::tr("y"));
    add(shift.months, PageMeta::tr("mo"));
    add(shift.days, PageMeta::tr("d"));
    add(shift.hours, PageMeta::tr("h"));
    add(shift.minutes, PageMeta::tr("m"));
    add(shift.seconds, PageMeta::tr("s"));
    return parts.isEmpty() ? PageMeta::tr("±0") : parts.join(QStringLiteral(" "));
}

} // namespace pp::ui::meta
