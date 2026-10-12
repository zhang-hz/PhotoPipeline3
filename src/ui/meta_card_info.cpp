// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页「关键信息」卡（B.1 拆分步 B5：meta_card_info）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B5）；契约面见 meta_state.h。
// 内容 = build_info_card + make_info_value + refresh_info_card + info_card_intents
// —— 自 page_meta.cpp 逐字迁出（M7-R2.1-F2；行为零变化）。
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
        }
        st->info.info_wb->set_full_text(QString());
        st->info.info_rating->setVisible(false);
        st->info.info_rating_none->setVisible(!empty);
        st->info.info_gps_pill->setVisible(false);
        return;
    }
    const MetaSelectionItem &item = st->follow.items.first();
    const ProbeSnapshot &probe = st->follow.probes.front();
    const pp::EffectivePreview &preview = previews.front();
    const pp::ImageInfo &info = item.info;

    // 拍摄时间 = 源 DateTimeOriginal（只读源事实；生效值在时间卡里对照显示）
    st->info.info_value[0]->set_full_text(
        probe.ok ? datetime_text(preview.datetime_original.original) : QStringLiteral("—"));
    st->info.info_value[1]->set_full_text(probe.ok ? camera_text(probe.meta.exif)
                                                   : QStringLiteral("—"));
    st->info.info_value[2]->set_full_text(probe.ok ? lens_text(probe.meta.exif)
                                                   : QStringLiteral("—"));
    st->info.info_value[3]->set_full_text(probe.ok ? exposure_text(probe.meta.exif)
                                                   : QStringLiteral("—"));
    if (item.probe_ok) {
        st->info.info_value[4]->set_full_text(QStringLiteral("%1×%2 · %3 bit · %4")
                                                  .arg(info.width)
                                                  .arg(info.height)
                                                  .arg(info.src_bitdepth)
                                                  .arg(channels_text(info)));
        st->info.info_value[5]->set_full_text(info.has_icc ? PageMeta::tr("内嵌 ICC 配置文件")
                                                           : PageMeta::tr("无 ICC"));
        st->info.info_value[6]->set_full_text(QStringLiteral("%1 · %2").arg(
            format_size(item.file_size), QString::fromStdString(info.format).toUpper()));
    } else {
        st->info.info_value[4]->set_full_text(QStringLiteral("—"));
        st->info.info_value[5]->set_full_text(QStringLiteral("—"));
        st->info.info_value[6]->set_full_text(format_size(item.file_size));
    }
    if (preview.gps.original.has_value()) {
        st->info.info_value[7]->set_full_text(QStringLiteral("%1, %2").arg(
            coord_text(preview.gps.original->lat, 4), coord_text(preview.gps.original->lon, 4)));
        st->info.info_gps_pill->setVisible(true);
    } else {
        st->info.info_value[7]->set_full_text(QStringLiteral("—"));
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
        make_header(st, card, PageMeta::tr("关键信息"), QStringLiteral("pp-meta-info-pill"),
                    QStringLiteral("pp-meta-info-hint"));
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
    const std::array<const char *, 8> hooks = {
        "pp-meta-info-time", "pp-meta-info-camera", "pp-meta-info-lens", "pp-meta-info-exposure",
        "pp-meta-info-size", "pp-meta-info-color",  "pp-meta-info-file", "pp-meta-info-gps"};
    for (int i = 0; i < 5; ++i) {
        formkit::FieldCell left;
        formkit::FieldCell right;
        left.label = keys[std::size_t(i) * 2];
        right.label = keys[std::size_t(i) * 2 + 1];
        if (i == 4) {
            // 第 5 行：白平衡 / 评级（矢量星；无评级 → 「—」）
            st->info.info_wb = make_info_value(body, QStringLiteral("pp-meta-info-wb"));
            left.field = st->info.info_wb;
            TrackGroup rating = make_group(body);
            st->info.info_rating = new RatingStars(rating.widget());
            st->info.info_rating->setObjectName(QStringLiteral("pp-meta-info-rating"));
            rating.add(st->info.info_rating);
            st->info.info_rating_none = new QLabel(QStringLiteral("—"), rating.widget());
            rating.add(st->info.info_rating_none);
            rating.stretch();
            right.field = rating.widget();
        } else {
            ElidedLabel *left_value =
                make_info_value(body, QString::fromLatin1(hooks[std::size_t(i) * 2]));
            ElidedLabel *right_value =
                make_info_value(body, QString::fromLatin1(hooks[std::size_t(i) * 2 + 1]));
            st->info.info_value[std::size_t(i) * 2] = left_value;
            st->info.info_value[std::size_t(i) * 2 + 1] = right_value;
            left.field = left_value;
            if (i == 3) {
                // 第 4 行右格：GPS 值 + 「已有」胶囊（mockup .kv-v.cells + .pill.ok）
                TrackGroup gps = make_group(body);
                gps.add(right_value, 1);
                gps.gap(theme::Metrics::group_gap);
                st->info.info_gps_pill = formkit::PillBadge::of(
                    gps.widget(), formkit::PillBadge::Tone::Ok, PageMeta::tr("已有"));
                st->info.info_gps_pill->setObjectName(QStringLiteral("pp-meta-info-gps-pill"));
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
        row.row->setObjectName(QStringLiteral("pp-meta-info-row-%1").arg(i));
        bv->addWidget(row.row);
        st->info.info_row[std::size_t(i)] = row.row;
    }
    // 空态一行（修 1-23）：未选择文件 → 一行说明（行高仍 28，栅格不错位）
    {
        st->info.info_empty = new ElidedLabel(body);
        st->info.info_empty->setObjectName(QStringLiteral("pp-meta-info-empty"));
        st->info.info_empty->setProperty(kFieldProperty, fieldprop::kEmpty);
        st->info.info_empty->setFont(theme::font(theme::Typography::body_px, QFont::Normal));
        st->info.info_empty->set_full_text(PageMeta::tr(kNoSelectionText));
        const formkit::Row empty_row =
            formkit::frow(body, QString(), st->info.info_empty, FieldWidth::Flex);
        empty_row.row->setObjectName(QStringLiteral("pp-meta-info-empty-row"));
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
        for (int i = 0; i < 9; ++i) {
            ElidedLabel *value = i < 8 ? st->info.info_value[std::size_t(i)] : st->info.info_wb;
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
