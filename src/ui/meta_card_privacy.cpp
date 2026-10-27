// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页「隐私与文件时间」卡（B.1 拆分步 B5：meta_card_privacy）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B5，privacy 先行）；契约面见 meta_state.h。
// 内容 = build_switches_card + refresh_privacy_inputs + refresh_keywords + privacy_card_intents
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

// 关键字胶囊组（§7.1「关键字 胶囊组」）：跟随项的 XMP dc:subject；超过 3 枚 → 「+N」
void refresh_keywords(MetaState *st) {
    if (st->privacy.keyword_row == nullptr) {
        return;
    }
    // 胶囊是**重建件**：本函数每次刷新都换一批 —— 令牌重放清单先清空（否则 apply_style 会对
    // 已 deleteLater 的旧胶囊取 tokens → 悬垂指针）
    st->core.keyword_pills.clear();
    // 说明位是常驻子控件（与 multi_more 同口径：重复入列/误删会造成悬垂引用）→ 只清胶囊。
    // R-180：常驻约定显式化 —— 首位即标记件（清理循环按位豁免，断言钉住不变量）。
    Q_ASSERT(st->privacy.keyword_row->count() == 0 ||
             is_resident(st->privacy.keyword_row->itemAt(0)->widget()));
    while (st->privacy.keyword_row->count() > 1) {
        QLayoutItem *item = st->privacy.keyword_row->takeAt(1);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    QStringList keywords;
    if (!st->follow.probes.empty() && !st->follow.items.isEmpty()) {
        keywords = keywords_of(st->follow.probes.front().meta);
    }
    st->privacy.keyword_none->setVisible(keywords.isEmpty());
    constexpr int kShown = 3;
    for (int i = 0; i < std::min<int>(kShown, keywords.size()); ++i) {
        auto *pill = formkit::PillBadge::of(st->privacy.keyword_box,
                                            formkit::PillBadge::Tone::Neutral, keywords.at(i));
        pill->setObjectName(objname::kKeyword1.arg(i));
        pill->set_tokens(st->core.tokens);
        st->core.keyword_pills.append(pill);
        st->privacy.keyword_row->addWidget(pill, 0, Qt::AlignVCenter);
    }
    if (keywords.size() > kShown) {
        auto *more =
            formkit::PillBadge::of(st->privacy.keyword_box, formkit::PillBadge::Tone::Neutral,
                                   QStringLiteral("+%1").arg(keywords.size() - kShown));
        more->setObjectName(objname::kKeywordMore);
        more->set_tokens(st->core.tokens);
        st->core.keyword_pills.append(more);
        st->privacy.keyword_row->addWidget(more, 0, Qt::AlignVCenter);
    }
}

// UX-5：隐私与文件时间卡的置灰（开关/标签编辑入口；例外列表是只读面不动）
void refresh_privacy_inputs(MetaState *st) {
    const bool card_on =
        st->privacy.privacy_enable == nullptr || st->privacy.privacy_enable->isChecked();
    st->privacy.strip->setEnabled(card_on);
    st->privacy.mtime->setEnabled(card_on);
    if (st->privacy.modernize != nullptr)
        st->privacy.modernize->setEnabled(card_on);
    st->privacy.link_tags->setEnabled(card_on);
    st->privacy.link_keywords->setEnabled(card_on);
    st->privacy.add_tag->setEnabled(card_on);
}

QWidget *build_switches_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header =
        make_header(st, card, PageMeta::tr("隐私与文件时间"), objname::kPrivacyPill, QString(),
                    objname::kPrivacyEnable); // UX-5
    st->privacy.privacy_pill = header.pill;
    st->privacy.privacy_enable = header.enable;
    v->addWidget(header.row);

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x,
                           theme::Metrics::card_pad_bottom);
    bv->setSpacing(0);
    // W-R-T13（A39 ⑥ 缺陷根治）：卡体必须进卡布局 —— 缺这一行时 body 是"有父无布局"的孤儿
    // （几何停在默认 100×30、行被压成 6px pitch 并盖住卡题），截图里表现为卡题上的"圆形伪影"。
    // 与其余三张卡（build_info_card/build_time_card/build_gps_card）同一条接线。
    v->addWidget(body);

    // 开关行（§6：独立布尔（立即生效）= 开关）：[开关 30×16][说明][……][链]
    const auto switch_row = [&](const QString &label, const QString &object_name,
                                formkit::SwitchControl **out, const QString &sub,
                                const QString &link_name, const QString &link_text,
                                QToolButton **link_out) {
        TrackGroup group = make_group(body);
        auto *sw = new formkit::SwitchControl(group.widget());
        sw->setObjectName(object_name);
        sw->set_tokens(st->core.tokens);
        st->core.switches.append(sw);
        *out = sw;
        group.add(sw);
        group.gap(theme::Metrics::group_gap);
        auto *note = new QLabel(sub, group.widget());
        note->setProperty(kFieldProperty, fieldprop::kSub);
        note->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        group.add(note);
        group.stretch();
        QToolButton *link = make_link(group.widget(), link_name, link_text);
        group.add(link);
        *link_out = link;
        const formkit::Row row = formkit::frow(body, label, group.widget(), FieldWidth::Flex);
        return row.row;
    };
    bv->addWidget(switch_row(
        PageMeta::tr("隐私剥除"), QStringLiteral("privacy_strip"), &st->privacy.strip,
        PageMeta::tr("输出前移除全部 EXIF / XMP，保留 ICC"), QStringLiteral("privacy_strip_link"),
        PageMeta::tr("标签编辑…"), &st->privacy.link_tags));
    bv->addWidget(switch_row(PageMeta::tr("时间同步"), QStringLiteral("mtime_sync"),
                             &st->privacy.mtime, PageMeta::tr("输出 mtime 跟随拍摄时间"),
                             QStringLiteral("mtime_sync_link"), PageMeta::tr("例外…"),
                             &st->privacy.link_exc));
    // 2026-10 可选能力：补全现代 EXIF。只补**可推导**字段；卡片关闭时不参与规则。
    {
        TrackGroup group = make_group(body);
        auto *sw = new formkit::SwitchControl(group.widget());
        sw->setObjectName(QStringLiteral("modernize_exif"));
        sw->set_tokens(st->core.tokens);
        st->core.switches.append(sw);
        group.add(sw);
        group.gap(theme::Metrics::group_gap);
        auto *note = new formkit::ElidedLabel(group.widget());
        note->setObjectName(QStringLiteral("pp-modernize-exif-note"));
        note->setProperty(kFieldProperty, fieldprop::kSub);
        note->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        note->set_full_text(PageMeta::tr("仅补可推导字段（版本/尺寸/GPS 基准），不编造拍摄信息"));
        note->setToolTip(
            PageMeta::tr("只补源文件缺失且能从已有事实推导的字段：ExifVersion、PixelXDimension/Y、"
                         "GPSVersionID、GPSMapDatum、GPSAltitudeRef、GPSImgDirectionRef；"
                         "不生成拍摄时间、镜头、相机、位置等未知信息。"));
        group.add(note);
        group.stretch();
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("EXIF 补全"), group.widget(), FieldWidth::Flex);
        bv->addWidget(row.row);
        st->privacy.modernize = sw;
        QObject::connect(sw, &formkit::SwitchControl::toggled, st, [st](bool) {
            if (!st->core.loading) {
                refresh_effective(st);
                notify_rules_changed(st);
            }
        });
    }
    // 关键字（§7.1：胶囊组 + 「编辑…」链）
    {
        TrackGroup group = make_group(body);
        // 胶囊组本体 = 独立子容器（随跟随项重建；说明位/胶囊都在它内部）
        st->privacy.keyword_box = new QWidget(group.widget());
        st->privacy.keyword_row = new QHBoxLayout(st->privacy.keyword_box);
        st->privacy.keyword_row->setContentsMargins(0, 0, 0, 0);
        st->privacy.keyword_row->setSpacing(formkit::kInlineGap);
        st->privacy.keyword_none = new ElidedLabel(st->privacy.keyword_box);
        st->privacy.keyword_none->setObjectName(objname::kKeywordNone);
        mark_resident(st->privacy.keyword_none); // R-180：常驻说明位显式标记（清理循环豁免）
        st->privacy.keyword_none->setProperty(kFieldProperty, fieldprop::kNote);
        st->privacy.keyword_none->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        st->privacy.keyword_none->set_full_text(PageMeta::tr(kNoKeywordsText));
        st->privacy.keyword_row->addWidget(st->privacy.keyword_none, 0, Qt::AlignVCenter);
        group.add(st->privacy.keyword_box);
        group.stretch();
        st->privacy.link_keywords =
            make_link(group.widget(), objname::kKeywordsLink, PageMeta::tr("编辑…"));
        group.add(st->privacy.link_keywords);
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("关键字"), group.widget(), FieldWidth::Flex);
        bv->addWidget(row.row);
    }

    // 标签编辑（折叠；「标签编辑…」/「编辑…」入口）
    st->privacy.tag_host = new QWidget(card);
    st->privacy.tag_host->setObjectName(objname::kTagHost);
    auto *tv = new QVBoxLayout(st->privacy.tag_host);
    tv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
    auto *rows_host = new QWidget(st->privacy.tag_host);
    rows_host->setObjectName(objname::kTagRowsHost);
    st->privacy.tag_rows = new QVBoxLayout(rows_host);
    st->privacy.tag_rows->setContentsMargins(0, 0, 0, 0);
    st->privacy.tag_rows->setSpacing(theme::Metrics::group_gap);
    tv->addWidget(rows_host);
    st->privacy.add_tag =
        make_button(st->privacy.tag_host, objname::kTagAddButton, PageMeta::tr("添加标签"));
    tv->addWidget(st->privacy.add_tag, 0, Qt::AlignLeading); // R-218：逻辑首端
    st->privacy.tag_host->setVisible(false);
    v->addWidget(st->privacy.tag_host);

    // 例外列表（折叠；「例外…」入口）
    st->privacy.exc_host = new QWidget(card);
    st->privacy.exc_host->setObjectName(objname::kExceptionHost);
    auto *xv = new QVBoxLayout(st->privacy.exc_host);
    xv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
    st->privacy.exc_label = new QLabel(PageMeta::tr("无例外文件"), st->privacy.exc_host);
    st->privacy.exc_label->setObjectName(objname::kExceptionLabel);
    st->privacy.exc_label->setProperty(kFieldProperty, fieldprop::kSub);
    st->privacy.exc_label->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    xv->addWidget(st->privacy.exc_label);
    st->privacy.exc_list = new QListWidget(st->privacy.exc_host);
    st->privacy.exc_list->setObjectName(objname::kExceptionList);
    st->privacy.exc_list->setContextMenuPolicy(Qt::CustomContextMenu);
    st->privacy.exc_list->setMaximumHeight(120);
    xv->addWidget(st->privacy.exc_list);
    st->privacy.exc_host->setVisible(false);
    v->addWidget(st->privacy.exc_host);

    QObject::connect(st->privacy.strip, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->core.loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    // UX-5：卡头「启用」开关 —— 关 = 整卡不参与规则（置灰 + build_rules 跳过）
    QObject::connect(st->privacy.privacy_enable, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->core.loading) {
            refresh_privacy_inputs(st);
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->privacy.mtime, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->core.loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    const auto toggle_tags = [st] {
        const bool on = !st->privacy.tag_host->isVisible();
        st->privacy.tag_host->setVisible(on);
        set_fold_icon(st, st->privacy.link_tags, on);
        set_fold_icon(st, st->privacy.link_keywords, on);
    };
    QObject::connect(st->privacy.link_tags, &QToolButton::clicked, st,
                     [toggle_tags] { toggle_tags(); });
    QObject::connect(st->privacy.link_keywords, &QToolButton::clicked, st,
                     [toggle_tags] { toggle_tags(); });
    QObject::connect(st->privacy.link_exc, &QToolButton::clicked, st, [st] {
        const bool on = !st->privacy.exc_host->isVisible();
        st->privacy.exc_host->setVisible(on);
        set_fold_icon(st, st->privacy.link_exc, on);
    });
    QObject::connect(st->privacy.add_tag, &QPushButton::clicked, st, [st] {
        add_tag_row(st, QString(), QString());
        st->privacy.tag_host->setVisible(true);
        notify_rules_changed(st);
    });
    QObject::connect(st->privacy.exc_list, &QListWidget::itemDoubleClicked, st,
                     [st](QListWidgetItem *item) {
                         if (item != nullptr) {
                             emit st->core.page->open_editor_requested(item->text());
                         }
                     });
    QObject::connect(st->privacy.exc_list, &QListWidget::customContextMenuRequested, st,
                     [st](const QPoint &pos) {
                         QListWidgetItem *item = st->privacy.exc_list->itemAt(pos);
                         if (item == nullptr) {
                             return;
                         }
                         QMenu menu(st->privacy.exc_list);
                         contextmenu::style_menu(&menu);
                         QAction *clear = menu.addAction(PageMeta::tr("清除例外"));
                         if (menu.exec(st->privacy.exc_list->viewport()->mapToGlobal(pos)) ==
                             clear) {
                             emit st->core.page->clear_exception_requested(item->text());
                         }
                     });
    return card;
}

contextmenu::PrivacyCardIntents privacy_card_intents(MetaState *st) {
    contextmenu::PrivacyCardIntents in;
    in.running = st->core.page == nullptr || !st->core.page->isEnabled();
    in.enabled = st->privacy.privacy_enable != nullptr && st->privacy.privacy_enable->isChecked();
    in.enable_toggle = [st] {
        if (st->privacy.privacy_enable != nullptr)
            st->privacy.privacy_enable->toggle();
    };
    in.strip_privacy = st->privacy.strip != nullptr && st->privacy.strip->isChecked();
    in.strip_privacy_toggle = [st] {
        if (st->privacy.strip != nullptr)
            st->privacy.strip->toggle();
    };
    in.sync_mtime = st->privacy.mtime != nullptr && st->privacy.mtime->isChecked();
    in.sync_mtime_toggle = [st] {
        if (st->privacy.mtime != nullptr)
            st->privacy.mtime->toggle();
    };
    in.edit_keywords = [st] {
        if (st->privacy.link_keywords != nullptr)
            st->privacy.link_keywords->click();
    };
    return in;
}

} // namespace pp::ui::meta
