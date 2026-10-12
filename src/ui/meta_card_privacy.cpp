// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页「隐私与文件时间」卡（B.1 拆分步 B5：meta_card_privacy）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B5，privacy 先行）；契约面见 meta_state.h。
// 内容 = build_switches_card + refresh_privacy_inputs + refresh_keywords + privacy_card_intents
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

// 关键字胶囊组（§7.1「关键字 胶囊组」）：跟随项的 XMP dc:subject；超过 3 枚 → 「+N」
void refresh_keywords(MetaState *st) {
    if (st->keyword_row == nullptr) {
        return;
    }
    // 胶囊是**重建件**：本函数每次刷新都换一批 —— 令牌重放清单先清空（否则 apply_style 会对
    // 已 deleteLater 的旧胶囊取 tokens → 悬垂指针）
    st->keyword_pills.clear();
    // 说明位是常驻子控件（与 multi_more 同口径：重复入列/误删会造成悬垂引用）→ 只清胶囊
    while (st->keyword_row->count() > 1) {
        QLayoutItem *item = st->keyword_row->takeAt(1);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    QStringList keywords;
    if (!st->probes.empty() && !st->items.isEmpty()) {
        keywords = keywords_of(st->probes.front().meta);
    }
    st->keyword_none->setVisible(keywords.isEmpty());
    constexpr int kShown = 3;
    for (int i = 0; i < std::min<int>(kShown, keywords.size()); ++i) {
        auto *pill = formkit::PillBadge::of(st->keyword_box, formkit::PillBadge::Tone::Neutral,
                                            keywords.at(i));
        pill->setObjectName(QStringLiteral("pp-meta-keyword-%1").arg(i));
        pill->set_tokens(st->tokens);
        st->keyword_pills.append(pill);
        st->keyword_row->addWidget(pill, 0, Qt::AlignVCenter);
    }
    if (keywords.size() > kShown) {
        auto *more = formkit::PillBadge::of(st->keyword_box, formkit::PillBadge::Tone::Neutral,
                                            QStringLiteral("+%1").arg(keywords.size() - kShown));
        more->setObjectName(QStringLiteral("pp-meta-keyword-more"));
        more->set_tokens(st->tokens);
        st->keyword_pills.append(more);
        st->keyword_row->addWidget(more, 0, Qt::AlignVCenter);
    }
}

// UX-5：隐私与文件时间卡的置灰（开关/标签编辑入口；例外列表是只读面不动）
void refresh_privacy_inputs(MetaState *st) {
    const bool card_on = st->privacy_enable == nullptr || st->privacy_enable->isChecked();
    st->strip->setEnabled(card_on);
    st->mtime->setEnabled(card_on);
    st->link_tags->setEnabled(card_on);
    st->link_keywords->setEnabled(card_on);
    st->add_tag->setEnabled(card_on);
}

QWidget *build_switches_card(MetaState *st, QWidget *parent) {
    auto *card = make_card(parent);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    const CardHeader header = make_header(st, card, PageMeta::tr("隐私与文件时间"),
                                          QStringLiteral("pp-meta-privacy-pill"), QString(),
                                          QStringLiteral("pp-meta-privacy-enable")); // UX-5
    st->privacy_pill = header.pill;
    st->privacy_enable = header.enable;
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
        sw->set_tokens(st->tokens);
        st->switches.append(sw);
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
    bv->addWidget(switch_row(PageMeta::tr("隐私剥除"), QStringLiteral("privacy_strip"), &st->strip,
                             PageMeta::tr("输出前移除全部 EXIF / XMP，保留 ICC"),
                             QStringLiteral("privacy_strip_link"), PageMeta::tr("标签编辑…"),
                             &st->link_tags));
    bv->addWidget(switch_row(PageMeta::tr("时间同步"), QStringLiteral("mtime_sync"), &st->mtime,
                             PageMeta::tr("输出 mtime 跟随拍摄时间"),
                             QStringLiteral("mtime_sync_link"), PageMeta::tr("例外…"),
                             &st->link_exc));
    // 关键字（§7.1：胶囊组 + 「编辑…」链）
    {
        TrackGroup group = make_group(body);
        // 胶囊组本体 = 独立子容器（随跟随项重建；说明位/胶囊都在它内部）
        st->keyword_box = new QWidget(group.widget());
        st->keyword_row = new QHBoxLayout(st->keyword_box);
        st->keyword_row->setContentsMargins(0, 0, 0, 0);
        st->keyword_row->setSpacing(formkit::kInlineGap);
        st->keyword_none = new ElidedLabel(st->keyword_box);
        st->keyword_none->setObjectName(QStringLiteral("pp-meta-keyword-none"));
        st->keyword_none->setProperty(kFieldProperty, fieldprop::kNote);
        st->keyword_none->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        st->keyword_none->set_full_text(PageMeta::tr(kNoKeywordsText));
        st->keyword_row->addWidget(st->keyword_none, 0, Qt::AlignVCenter);
        group.add(st->keyword_box);
        group.stretch();
        st->link_keywords = make_link(group.widget(), QStringLiteral("pp-meta-keywords-link"),
                                      PageMeta::tr("编辑…"));
        group.add(st->link_keywords);
        const formkit::Row row =
            formkit::frow(body, PageMeta::tr("关键字"), group.widget(), FieldWidth::Flex);
        bv->addWidget(row.row);
    }

    // 标签编辑（折叠；「标签编辑…」/「编辑…」入口）
    st->tag_host = new QWidget(card);
    st->tag_host->setObjectName(QStringLiteral("pp-meta-tag-host"));
    auto *tv = new QVBoxLayout(st->tag_host);
    tv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
    auto *rows_host = new QWidget(st->tag_host);
    rows_host->setObjectName(QStringLiteral("tag_rows_host"));
    st->tag_rows = new QVBoxLayout(rows_host);
    st->tag_rows->setContentsMargins(0, 0, 0, 0);
    st->tag_rows->setSpacing(theme::Metrics::group_gap);
    tv->addWidget(rows_host);
    st->add_tag =
        make_button(st->tag_host, QStringLiteral("tag_add_button"), PageMeta::tr("添加标签"));
    tv->addWidget(st->add_tag, 0, Qt::AlignLeft);
    st->tag_host->setVisible(false);
    v->addWidget(st->tag_host);

    // 例外列表（折叠；「例外…」入口）
    st->exc_host = new QWidget(card);
    st->exc_host->setObjectName(QStringLiteral("pp-meta-exception-host"));
    auto *xv = new QVBoxLayout(st->exc_host);
    xv->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
    st->exc_label = new QLabel(PageMeta::tr("无例外文件"), st->exc_host);
    st->exc_label->setObjectName(QStringLiteral("exception_label"));
    st->exc_label->setProperty(kFieldProperty, fieldprop::kSub);
    st->exc_label->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
    xv->addWidget(st->exc_label);
    st->exc_list = new QListWidget(st->exc_host);
    st->exc_list->setObjectName(QStringLiteral("exception_list"));
    st->exc_list->setContextMenuPolicy(Qt::CustomContextMenu);
    st->exc_list->setMaximumHeight(120);
    xv->addWidget(st->exc_list);
    st->exc_host->setVisible(false);
    v->addWidget(st->exc_host);

    QObject::connect(st->strip, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    // UX-5：卡头「启用」开关 —— 关 = 整卡不参与规则（置灰 + build_rules 跳过）
    QObject::connect(st->privacy_enable, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_privacy_inputs(st);
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    QObject::connect(st->mtime, &formkit::SwitchControl::toggled, st, [st](bool) {
        if (!st->loading) {
            refresh_effective(st);
            notify_rules_changed(st);
        }
    });
    const auto toggle_tags = [st] {
        const bool on = !st->tag_host->isVisible();
        st->tag_host->setVisible(on);
        set_fold_icon(st, st->link_tags, on);
        set_fold_icon(st, st->link_keywords, on);
    };
    QObject::connect(st->link_tags, &QToolButton::clicked, st, [toggle_tags] { toggle_tags(); });
    QObject::connect(st->link_keywords, &QToolButton::clicked, st,
                     [toggle_tags] { toggle_tags(); });
    QObject::connect(st->link_exc, &QToolButton::clicked, st, [st] {
        const bool on = !st->exc_host->isVisible();
        st->exc_host->setVisible(on);
        set_fold_icon(st, st->link_exc, on);
    });
    QObject::connect(st->add_tag, &QPushButton::clicked, st, [st] {
        add_tag_row(st, QString(), QString());
        st->tag_host->setVisible(true);
        notify_rules_changed(st);
    });
    QObject::connect(st->exc_list, &QListWidget::itemDoubleClicked, st,
                     [st](QListWidgetItem *item) {
                         if (item != nullptr) {
                             emit st->page->open_editor_requested(item->text());
                         }
                     });
    QObject::connect(st->exc_list, &QListWidget::customContextMenuRequested, st,
                     [st](const QPoint &pos) {
                         QListWidgetItem *item = st->exc_list->itemAt(pos);
                         if (item == nullptr) {
                             return;
                         }
                         QMenu menu(st->exc_list);
                         QAction *clear = menu.addAction(PageMeta::tr("清除例外"));
                         if (menu.exec(st->exc_list->viewport()->mapToGlobal(pos)) == clear) {
                             emit st->page->clear_exception_requested(item->text());
                         }
                     });
    return card;
}

contextmenu::PrivacyCardIntents privacy_card_intents(MetaState *st) {
    contextmenu::PrivacyCardIntents in;
    in.running = st->page == nullptr || !st->page->isEnabled();
    in.enabled = st->privacy_enable != nullptr && st->privacy_enable->isChecked();
    in.enable_toggle = [st] {
        if (st->privacy_enable != nullptr)
            st->privacy_enable->toggle();
    };
    in.strip_privacy = st->strip != nullptr && st->strip->isChecked();
    in.strip_privacy_toggle = [st] {
        if (st->strip != nullptr)
            st->strip->toggle();
    };
    in.sync_mtime = st->mtime != nullptr && st->mtime->isChecked();
    in.sync_mtime_toggle = [st] {
        if (st->mtime != nullptr)
            st->mtime->toggle();
    };
    in.edit_keywords = [st] {
        if (st->link_keywords != nullptr)
            st->link_keywords->click();
    };
    return in;
}

} // namespace pp::ui::meta
