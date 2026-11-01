// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页「关键信息」卡（B.1 拆分步 B5：meta_card_info）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B5）；契约面见 meta_state.h。
// 内容 = build_info_card + make_info_value + refresh_info_card + info_card_intents
// —— 自 page_meta.cpp 逐字迁出（M7-R2.1-F2；行为零变化）。
#include "ui/meta_probe_context.h" // R-182/R-258：钩子/词表单源
#include "ui/meta_state.h"

#include "ui/contextmenu.h"
#include "ui/formkit.h"
#include "ui/icons.h"
#include "ui/meta_format.h"
#include "ui/meta_rules.h"
#include "ui/page_meta.h"
#include "ui/theme.h"

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pp::ui::meta {

namespace {

// M13（0.4.1）/ C2.5：多选只读值聚合 —— 每个 kv 值先逐文件生成显示文本，再按
// uniform（同值 → 直显）/ mixed（→「多个值」+ 逐文件 tooltip）落地。
// 单选路径保持既有实现逐字不变（下方 refresh_info_card 的 else 分支）。
QString info_size_text(const MetaSelectionItem &item) {
    if (!item.probe_ok) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1×%2 · %3 bit · %4")
        .arg(item.info.width)
        .arg(item.info.height)
        .arg(item.info.src_bitdepth)
        .arg(channels_text(item.info));
}

QString info_color_text(const MetaSelectionItem &item) {
    if (!item.probe_ok) {
        return QStringLiteral("—");
    }
    return item.info.has_icc ? PageMeta::tr("内嵌 ICC 配置文件") : PageMeta::tr("无 ICC");
}

QString info_file_text(const MetaSelectionItem &item) {
    QString fmt = QString::fromStdString(item.info.format).toUpper();
    const QString suffix = QFileInfo(item.path).suffix().toLower();
    if (item.probe_ok) {
        // 与单选路径同口径：OIIO 对 AVIF 也报 "HEIF"，按扩展名纠正
        if (suffix == QStringLiteral("avif") || suffix == QStringLiteral("avifs")) {
            fmt = QStringLiteral("AVIF");
        } else if (suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif") ||
                   suffix == QStringLiteral("hif")) {
            fmt = QStringLiteral("HEIF");
        }
        return QStringLiteral("%1 · %2").arg(format_size(item.file_size), fmt);
    }
    return format_size(item.file_size);
}

void refresh_info_card_multi(MetaState *st, const std::vector<pp::EffectivePreview> &previews) {
    const int count = static_cast<int>(st->follow.items.size());
    SelectionSnapshotList sel;
    sel.reserve(static_cast<std::size_t>(count));
    for (const MetaSelectionItem &item : st->follow.items) {
        SelectionSnapshot snapshot;
        snapshot.path = item.path;
        sel.push_back(std::move(snapshot));
    }
    const auto put = [&](ElidedLabel *label, const QStringList &texts) {
        if (label == nullptr) {
            return;
        }
        const TextAggregate agg = aggregate_texts(sel, texts);
        const bool mixed = !agg.empty && !agg.uniform;
        label->set_full_text(mixed ? PageMeta::tr(kMixedValueText)
                                   : (texts.isEmpty() ? QString() : texts.value(0)));
        label->setToolTip(mixed ? aggregate_tooltip(agg) : QString());
    };
    // 逐槽生成「每文件文本」（previews 只覆盖前 ≤kMultiRowCap 个；其余 → "—"）
    std::array<QStringList, kInfoSlotCount> texts;
    QStringList wb_texts;
    for (int i = 0; i < count; ++i) {
        const MetaSelectionItem &item = st->follow.items.at(i);
        const bool has_probe = i < static_cast<int>(previews.size());
        const bool multi_probe = has_probe && i < static_cast<int>(st->follow.probes.size());
        const pp::EffectivePreview *preview = has_probe ? &previews[std::size_t(i)] : nullptr;
        const bool probe_ok = multi_probe && st->follow.probes[std::size_t(i)].ok;
        const pp::SourceMeta *meta =
            multi_probe ? &st->follow.probes[std::size_t(i)].meta : nullptr;
        texts[kInfoTime] << (probe_ok && preview != nullptr
                                 ? datetime_text(preview->datetime_original.original)
                                 : QStringLiteral("—"));
        texts[kInfoCamera] << (probe_ok && meta != nullptr ? camera_text(meta->exif)
                                                           : QStringLiteral("—"));
        texts[kInfoLens] << (probe_ok && meta != nullptr ? lens_text(meta->exif)
                                                         : QStringLiteral("—"));
        texts[kInfoExposure] << (probe_ok && meta != nullptr ? exposure_text(meta->exif)
                                                             : QStringLiteral("—"));
        texts[kInfoSize] << info_size_text(item);
        texts[kInfoColor] << info_color_text(item);
        texts[kInfoFile] << info_file_text(item);
        texts[kInfoGps] << (probe_ok && preview != nullptr && preview->gps.original.has_value()
                                ? QStringLiteral("%1, %2").arg(
                                      coord_text(preview->gps.original->lat, 4),
                                      coord_text(preview->gps.original->lon, 4))
                                : QStringLiteral("—"));
        wb_texts << (probe_ok && meta != nullptr ? white_balance_text(meta->exif)
                                                 : QStringLiteral("—"));
    }
    for (int slot = 0; slot < kInfoSlotCount; ++slot) {
        put(st->info.info_value[std::size_t(slot)], texts[std::size_t(slot)]);
    }
    put(st->info.info_wb, wb_texts);
    // GPS「已有」胶囊：仅全选统一"有 GPS"时显示（mixed 不显示，避免假统一）
    bool all_gps = count > 0;
    for (const QString &text : texts[kInfoGps]) {
        if (text == QStringLiteral("—")) {
            all_gps = false;
            break;
        }
    }
    st->info.info_gps_pill->setVisible(all_gps);
    // 评级：全选同值且都有评级才画星；mixed/部分缺失 → 文本「多个值」/「—」
    std::optional<int> common_rating;
    bool uniform_rating = true;
    for (int i = 0; i < count; ++i) {
        std::optional<int> rating;
        if (i < static_cast<int>(st->follow.probes.size()) &&
            st->follow.probes[std::size_t(i)].ok) {
            rating = rating_of(st->follow.probes[std::size_t(i)].meta);
        }
        if (!rating.has_value()) {
            uniform_rating = false;
            break;
        }
        if (!common_rating.has_value()) {
            common_rating = rating;
        } else if (*common_rating != *rating) {
            uniform_rating = false;
            break;
        }
    }
    st->info.info_rating->setVisible(uniform_rating && common_rating.has_value());
    st->info.info_rating->set_rating(common_rating.value_or(0));
    st->info.info_rating_none->setVisible(!uniform_rating || !common_rating.has_value());
    if (!uniform_rating) {
        bool any_rating = false;
        for (int i = 0; i < count && !any_rating; ++i) {
            if (i < static_cast<int>(st->follow.probes.size()) &&
                st->follow.probes[std::size_t(i)].ok) {
                any_rating = rating_of(st->follow.probes[std::size_t(i)].meta).has_value();
            }
        }
        st->info.info_rating_none->setText(any_rating ? PageMeta::tr(kMixedValueText)
                                                      : QStringLiteral("—"));
    } else {
        st->info.info_rating_none->setText(QStringLiteral("—"));
    }
}

} // namespace

void refresh_info_card(MetaState *st, const std::vector<pp::EffectivePreview> &previews) {
    const bool empty = st->follow.items.isEmpty();
    // 空态（修 1-23）：5 行 kv 收起，只留一行说明
    for (QWidget *row : st->info.info_row) {
        row->setVisible(!empty);
    }
    st->info.info_empty_row->setVisible(empty);
    if (empty || previews.empty()) {
        for (ElidedLabel *label : st->info.info_value) {
            label->set_full_text(QString());
            label->setToolTip(QString());
        }
        st->info.info_wb->set_full_text(QString());
        st->info.info_rating->setVisible(false);
        st->info.info_rating_none->setText(QStringLiteral("—"));
        st->info.info_rating_none->setVisible(!empty);
        st->info.info_gps_pill->setVisible(false);
        return;
    }
    // M13/C2.5：多选（≥2）→ 逐字段「多个值」聚合；单选 → 既有实现逐字不变
    if (st->follow.items.size() >= 2) {
        refresh_info_card_multi(st, previews);
        return;
    }
    const MetaSelectionItem &item = st->follow.items.first();
    const ProbeSnapshot &probe = st->follow.probes.front();
    const pp::EffectivePreview &preview = previews.front();
    const pp::ImageInfo &info = item.info;

    // 拍摄时间 = 源 DateTimeOriginal（只读源事实；生效值在时间卡里对照显示）
    st->info.info_value[kInfoTime]->set_full_text(
        probe.ok ? datetime_text(preview.datetime_original.original) : QStringLiteral("—"));
    st->info.info_value[kInfoCamera]->set_full_text(probe.ok ? camera_text(probe.meta.exif)
                                                             : QStringLiteral("—"));
    st->info.info_value[kInfoLens]->set_full_text(probe.ok ? lens_text(probe.meta.exif)
                                                           : QStringLiteral("—"));
    st->info.info_value[kInfoExposure]->set_full_text(probe.ok ? exposure_text(probe.meta.exif)
                                                               : QStringLiteral("—"));
    if (item.probe_ok) {
        st->info.info_value[kInfoSize]->set_full_text(QStringLiteral("%1×%2 · %3 bit · %4")
                                                          .arg(info.width)
                                                          .arg(info.height)
                                                          .arg(info.src_bitdepth)
                                                          .arg(channels_text(info)));
        st->info.info_value[kInfoColor]->set_full_text(
            info.has_icc ? PageMeta::tr("内嵌 ICC 配置文件") : PageMeta::tr("无 ICC"));
        // OIIO 对 AVIF 也报 "HEIF"；元数据页与文件列表同口径按扩展名纠正。
        QString fmt = QString::fromStdString(info.format).toUpper();
        const QString suffix = QFileInfo(item.path).suffix().toLower();
        if (suffix == QStringLiteral("avif") || suffix == QStringLiteral("avifs"))
            fmt = QStringLiteral("AVIF");
        else if (suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif") ||
                 suffix == QStringLiteral("hif"))
            fmt = QStringLiteral("HEIF");
        st->info.info_value[kInfoFile]->set_full_text(
            QStringLiteral("%1 · %2").arg(format_size(item.file_size), fmt));
    } else {
        st->info.info_value[kInfoSize]->set_full_text(QStringLiteral("—"));
        st->info.info_value[kInfoColor]->set_full_text(QStringLiteral("—"));
        st->info.info_value[kInfoFile]->set_full_text(format_size(item.file_size));
    }
    if (preview.gps.original.has_value()) {
        st->info.info_value[kInfoGps]->set_full_text(QStringLiteral("%1, %2").arg(
            coord_text(preview.gps.original->lat, 4), coord_text(preview.gps.original->lon, 4)));
        st->info.info_gps_pill->setVisible(true);
    } else {
        st->info.info_value[kInfoGps]->set_full_text(QStringLiteral("—"));
        st->info.info_gps_pill->setVisible(false);
    }
    // 白平衡 / 评级（§7.1 第 5 行）
    st->info.info_wb->set_full_text(probe.ok ? white_balance_text(probe.meta.exif)
                                             : QStringLiteral("—"));
    const std::optional<int> rating = probe.ok ? rating_of(probe.meta) : std::nullopt;
    st->info.info_rating->setVisible(rating.has_value());
    st->info.info_rating->set_rating(rating.value_or(0));
    st->info.info_rating_none->setVisible(!rating.has_value());
}

// ---------------------------------------------------------------------------
// 界面构建
// ---------------------------------------------------------------------------

// R-056（M7-WP4）：死函数 card_body(QWidget*) 已删（grep 复核全仓零调用；卡体由
// formkit::make_card 单源承担，R1 上 10 / 下 12 —— 原"上 0 由卡头下沿承担"两制收口）。

ElidedLabel *make_info_value(QWidget *parent, const QString &object_name) {
    auto *value = new ElidedLabel(parent);
    value->setObjectName(object_name);
    value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
    value->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    // kv 值：正文语序（mockup .kv-v 从首端起）；R-218：逻辑首端（LTR=左 / RTL=右）
    value->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    return value;
}

QWidget *build_info_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("关键信息"), objname::kInfoPill, objname::kInfoHint);
    st->info.info_pill = header.pill;
    st->info.info_hint = header.hint;
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);

    // kv 双列 5 行（§7.1 逐行）：拍摄时间/相机、镜头/曝光、尺寸/色彩、文件/GPS、白平衡/评级
    const std::array<QString, 10> keys = {
        PageMeta::tr("拍摄时间"), PageMeta::tr("相机"), PageMeta::tr("镜头"), PageMeta::tr("曝光"),
        PageMeta::tr("尺寸"),     PageMeta::tr("色彩"), PageMeta::tr("文件"), PageMeta::tr("GPS"),
        PageMeta::tr("白平衡"),   PageMeta::tr("评级")};
    // R-180：值槽 → objectName 词表（objname:: 单源；槽序 = InfoSlot 词表）
    static const std::array<QString, kInfoSlotCount> slot_name = {
        objname::kInfoTime, objname::kInfoCamera, objname::kInfoLens, objname::kInfoExposure,
        objname::kInfoSize, objname::kInfoColor,  objname::kInfoFile, objname::kInfoGps};
    // R-180：kv 行 → 值槽的**具名映射**（原 i*2 / i*2+1 双侧算术约定）
    constexpr InfoSlot kInfoRowLeft[4] = {kInfoTime, kInfoLens, kInfoSize, kInfoFile};
    constexpr InfoSlot kInfoRowRight[4] = {kInfoCamera, kInfoExposure, kInfoColor, kInfoGps};
    // F2 残余 ⑥-3（M7R22G2）：键表/槽表/行映射**编译期对拍** —— InfoSlot 词表变动时不再靠
    // 「恰好对上」维持（kv 键表 = 8 值槽 + 白平衡 + 评级；双侧行映射覆盖全部值槽）。
    static_assert(std::tuple_size_v<decltype(keys)> == size_t(kInfoSlotCount) + 2,
                  "kv 键表 == kInfoSlotCount + 2（8 值槽 + 白平衡 + 评级）");
    static_assert(std::size(kInfoRowLeft) + std::size(kInfoRowRight) == size_t(kInfoSlotCount),
                  "kv 双侧行映射覆盖全部 info_value 值槽");
    for (int i = 0; i < 5; ++i) {
        formkit::FieldCell left;
        formkit::FieldCell right;
        left.label = keys[std::size_t(i) * 2];
        right.label = keys[std::size_t(i) * 2 + 1];
        if (i == 4) {
            // 第 5 行：白平衡 / 评级（矢量星；无评级 → 「—」）
            st->info.info_wb = make_info_value(body, objname::kInfoWb);
            left.field = st->info.info_wb;
            TrackGroup rating = make_group(body);
            st->info.info_rating = new RatingStars(rating.widget());
            st->info.info_rating->setObjectName(objname::kInfoRating);
            rating.add(st->info.info_rating);
            st->info.info_rating_none = new QLabel(QStringLiteral("—"), rating.widget());
            rating.add(st->info.info_rating_none);
            rating.stretch();
            right.field = rating.widget();
        } else {
            const InfoSlot left_slot = kInfoRowLeft[i];
            const InfoSlot right_slot = kInfoRowRight[i];
            ElidedLabel *left_value = make_info_value(body, slot_name[left_slot]);
            ElidedLabel *right_value = make_info_value(body, slot_name[right_slot]);
            st->info.info_value[left_slot] = left_value;
            st->info.info_value[right_slot] = right_value;
            left.field = left_value;
            if (i == 3) {
                // 第 4 行右格：GPS 值 + 「已有」胶囊（mockup .kv-v.cells + .pill.ok）
                TrackGroup gps = make_group(body);
                gps.add(right_value, 1);
                gps.gap(theme::Metrics::group_gap);
                st->info.info_gps_pill = formkit::PillBadge::of(
                    gps.widget(), formkit::PillBadge::Tone::Ok, PageMeta::tr("已有"));
                st->info.info_gps_pill->setObjectName(objname::kInfoGpsPill);
                st->info.info_gps_pill->setVisible(false);
                st->add_pill(st->info.info_gps_pill);
                gps.add(st->info.info_gps_pill);
                gps.stretch();
                right.field = gps.widget();
            } else {
                right.field = right_value;
            }
        }
        left.width = FieldWidth::Flex;
        right.width = FieldWidth::Flex;
        const formkit::DoubleRow row = formkit::double_row(body, left, right);
        row.row->setObjectName(objname::kInfoRow1.arg(i));
        bv->addWidget(row.row);
        st->info.info_row[std::size_t(i)] = row.row;
    }
    // 空态一行（修 1-23）：未选择文件 → 一行说明（行高仍 28，栅格不错位）
    {
        st->info.info_empty = new ElidedLabel(body);
        st->info.info_empty->setObjectName(objname::kInfoEmpty);
        st->info.info_empty->setProperty(kFieldProperty, fieldprop::kEmpty);
        st->info.info_empty->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        st->info.info_empty->set_full_text(PageMeta::tr(kNoSelectionText));
        const formkit::Row empty_row =
            formkit::frow(body, QString(), st->info.info_empty, FieldWidth::Flex);
        empty_row.row->setObjectName(objname::kInfoEmptyRow);
        st->info.info_empty_row = empty_row.row;
        st->info.info_empty_row->setVisible(false);
        bv->addWidget(st->info.info_empty_row);
    }
    v->addWidget(body);
    return card;
}

// ---------------------------------------------------------------------------
// M7-R2D1 / UX-1②③④⑤（docs/ux-review-14.md §1 表）：元数据页四卡右键菜单。
//   * 意图一律走**既有控件/收口**（按钮 click / 开关 toggle / 既有静态函数），不新增第二套
//     行为路径；菜单内容 = contextmenu 的四类 actions（12 类表逐条）；
//   * 运行中置灰（intents.running）= 页面级 G5 禁用的读数 —— lock_for_run 对 page_meta
//     setEnabled(false) 时右键事件本就不达（与分类行「运行中不弹菜单」同效果），置灰是双保险；
//   * 「时间偏移·从文件读取时间」= 跟随项重读源文件（request_probe_scan；对照行随之刷新），
//     「GPS·地图选点」= 展开大地图点选（同一 map 控件，点图即取点），「GPS·取地图中心」= 既有
//     「地图选点」按钮的同一条链路（apply_picked_point(center)）—— 选择已记 handoff 裁定登记。
// ---------------------------------------------------------------------------

contextmenu::InfoCardIntents info_card_intents(MetaState *st) {
    contextmenu::InfoCardIntents in;
    in.running = st->core.page == nullptr || !st->core.page->isEnabled();
    in.copy_all = [st] {
        QStringList lines;
        const std::array<QString, 9> keys = {
            PageMeta::tr("拍摄时间"), PageMeta::tr("相机"), PageMeta::tr("镜头"),
            PageMeta::tr("曝光"),     PageMeta::tr("尺寸"), PageMeta::tr("色彩"),
            PageMeta::tr("文件"),     PageMeta::tr("GPS"),  PageMeta::tr("白平衡")};
        // F2 残余 ⑥-3（M7R22G2）：copy_all 键表 ↔ kInfoSlotCount **编译期对拍**
        // （8 值槽 + 白平衡槽；tests/unit/test_meta_format.cpp 同判据复述双端钉住）。
        static_assert(std::tuple_size_v<decltype(keys)> == size_t(kInfoSlotCount) + 1,
                      "copy_all 键表 == kInfoSlotCount + 1（8 值槽 + 白平衡）");
        for (int i = 0; i < static_cast<int>(std::size(keys)); ++i) {
            // R-180：槽界 = InfoSlot 词表（原裸常量 8）
            ElidedLabel *value =
                i < kInfoSlotCount ? st->info.info_value[std::size_t(i)] : st->info.info_wb;
            if (value == nullptr || value->full_text().isEmpty())
                continue;
            lines << QStringLiteral("%1: %2").arg(keys[std::size_t(i)], value->full_text());
        }
        if (QClipboard *clip = QApplication::clipboard())
            clip->setText(lines.join(QLatin1Char('\n')));
    };
    in.reload = [st] { request_probe_scan(st); };
    return in;
}

} // namespace pp::ui::meta
