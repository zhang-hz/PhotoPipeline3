// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页显示格式化纯函数（B.1 拆分步 B2：meta_format）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（page_meta 拆分蓝图，步序 B2）
// 本单元 = page_meta.cpp 原「文本格式化」段的**整体迁出**（M7-R2.1-F2）：
//   * **纯函数、零 GUI 状态、零 IO** —— 只做显示层排版；数值来源是 preview_effective /
//     probe 摘要（调用方持有）。EXIF/XMP 只读取（exif_text 族）仍属显示层读，与 IO 分离
//     （R-181：UI 内嵌 IO 归 meta_probe 的 ProbeScanner，本单元不发任何磁盘/网络请求）；
//   * **可单测**：入参为值/只读引用（Exiv2::ExifData / pp::SourceMeta / pp::TimeShift），
//     不触碰 MetaState / QWidget；
//   * 迁移零行为变化：tr 上下文保持 PageMeta（不改翻译查表键）、输出串逐字节不变。
#pragma once

#include "core/metadata.h"

#include <QString>
#include <QStringList>
#include <optional>

namespace pp::ui::meta {

// "2024:03:01 10:00:00" → "2024-03-01 10:00:00"（mockup 的拍摄时间/生效时间口径）
QString format_datetime(const QString &exif_dt);

// 日期部分（生效行注释的「日期」段）；空值 → 空串
QString date_part(const std::optional<pp::DateTimeVal> &v);

// 有值 → 格式化时间；无值 → "—"
QString datetime_text(const std::optional<pp::DateTimeVal> &v);

// 数值 → 坐标读数（N 位小数 + °）
QString coord_text(double v, int decimals);

// 字节数 → "x.xx MB" / "x.x KB"（<1MB 用 KB；≤0 → "—"）
QString format_size(qint64 bytes);

// probe 摘要的通道数 → 名称（core/types.h：channels ∈ {1,2,3,4}）
QString channels_text(const pp::ImageInfo &info);

// EXIF 字符串读取（显示层只读；未知标签名 = 无值，不抛、不猜）
std::string exif_text(const Exiv2::ExifData &exif, const char *key);

// 相机（Make + Model；Model 已含 Make 前缀时只出 Model）
QString camera_text(const Exiv2::ExifData &exif);

// 镜头（LensModel 优先，LensMake 兜底）
QString lens_text(const Exiv2::ExifData &exif);

// 曝光 = ISO · 快门 · 光圈 · 焦距（§5.2 字段集；缺项即略过，全缺 → "—"）
QString exposure_text(const Exiv2::ExifData &exif);

// 白平衡（Exif.Photo.WhiteBalance 0=自动/1=手动 + ColorTemperature K；全缺 → "—"）
QString white_balance_text(const Exiv2::ExifData &exif);

// 评级：Exif.Image.Rating 优先，XMP xmp:Rating 兜底；无 → nullopt（显示 "—"）
std::optional<int> rating_of(const pp::SourceMeta &meta);

// 关键字：Xmp.dc.subject（数组；非数组值按单项处理）
QStringList keywords_of(const pp::SourceMeta &meta);

// DMS 文案（pp::map::deg_to_dms 单源；R-099）
QString dms_string(double lat, double lon);

// Δ 模式摘要（生效行注释的「Δ」段；mockup「+8h」口径）：y/mo/d/h/m/s，全零 → ±0
QString delta_text(const pp::TimeShift &shift);

} // namespace pp::ui::meta
