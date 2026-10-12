// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页样式（B.1 拆分步 B7：meta_style）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B7：apply_style 拆 qss_for(tokens)
// 纯函数 + apply_icons）+ R-179（apply_style QSS 遗留 / kFieldProperty stringly 值收敛）。
// 实现自 page_meta.cpp 迁出（M7-R2.1-F2）：QSS 下发值与图标重放行为零变化。
#include "ui/meta_style.h"

#include "mapwidget/mapwidget.h" // MapWidget 完整类型（banner findChildren）
#include "ui/formkit.h"
#include "ui/icons.h"
#include "ui/meta_state.h"
#include "ui/page_meta.h"
#include "ui/theme.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWidget>

namespace pp::ui::meta {

// ---------------------------------------------------------------------------
// QSS 纯合成（B7 / R-179：页内 QSS 拼装单源，可单测；值域 = theme.h 六段）
// ---------------------------------------------------------------------------

QString qss_for(const theme::Tokens &t) {
    QString qss;
    qss += theme::formkit_qss(t);
    qss += theme::mod_qss(t);
    qss += theme::pill_qss(t);
    qss += theme::card_qss(t);
    qss += theme::field_qss(t);
    qss += theme::page_meta_qss(t);
    return qss;
}

// R-179 遗留：地图离线 banner 的宿主侧收口样式（原 apply_style 内联拼串 → 命名纯函数；
// 修 1-38/1-39 的中性灰口径不变）
QString map_banner_qss(const theme::Tokens &t) {
    const auto css = [](const QColor &c) { return theme::css_color(c); };
    return QStringLiteral("QLabel { background: ") + css(t.card_hi) +
           QStringLiteral("; border: 1px solid ") + css(t.card_bd) +
           QStringLiteral("; border-radius: ") + QString::number(t.radius_control) +
           QStringLiteral("px; color: ") + css(t.text3) + QStringLiteral("; padding: 4px; }");
}

// ---------------------------------------------------------------------------
// 矢量图标重放（M5-T3：QIcon 取色不吃 QSS 的 color；主题切换唯一落点）
// ---------------------------------------------------------------------------

void apply_icons(MetaState *st) {
    const theme::Tokens &t = st->tokens;
    // M5-T3：矢量图标随 tokens 重放（QIcon 取色不吃 QSS 的 color）；M6-T8 加：搜索钮放大镜
    set_fold_icon(st, st->rule_toggle, st->rule_toggle != nullptr && st->rule_toggle->isChecked());
    set_fold_icon(st, st->more_toggle, st->more_toggle != nullptr && st->more_toggle->isChecked());
    set_fold_icon(st, st->link_tags, st->tag_host != nullptr && st->tag_host->isVisible());
    set_fold_icon(st, st->link_exc, st->exc_host != nullptr && st->exc_host->isVisible());
    set_fold_icon(st, st->link_keywords, st->tag_host != nullptr && st->tag_host->isVisible());
    if (st->search_btn != nullptr) {
        icons::apply(st->search_btn, icons::Name::Search, t.text2, t.text3, 12);
    }
    // M6-T12：地图缩放钮的矢量图标随令牌重放（覆盖层固定白底 → 图标恒取深灰 #404040）
    if (st->overlay.zoom_in != nullptr) {
        icons::apply(st->overlay.zoom_in, icons::Name::ZoomIn, theme::overlay_tokens().hud_fg,
                     theme::overlay_tokens().hud_fg_dim,
                     kMapZoomIconPx); // overlay 令牌组（不随主题变）
    }
    if (st->overlay.zoom_out != nullptr) {
        icons::apply(st->overlay.zoom_out, icons::Name::ZoomOut, theme::overlay_tokens().hud_fg,
                     theme::overlay_tokens().hud_fg_dim,
                     kMapZoomIconPx); // overlay 令牌组（不随主题变）
    }
    // M6-T13：展开钮同档（覆盖层固定白底 → 图标恒取深灰 #404040）
    if (st->overlay.map_expand != nullptr) {
        icons::apply(st->overlay.map_expand, icons::Name::ZoomFit, theme::overlay_tokens().hud_fg,
                     theme::overlay_tokens().hud_fg_dim,
                     kMapZoomIconPx); // overlay 令牌组（不随主题变）
    }
    // M6-T10：坐标行 WGS-84 标签的 Info 矢量图标随令牌重放（QLabel 承载的 QPixmap 取色不吃 QSS）
    if (st->datum_icon != nullptr) {
        st->datum_icon->setPixmap(icons::pixmap(icons::Name::Info, kDatumIconPx, t.text3,
                                                st->datum_icon->devicePixelRatioF()));
    }
    if (st->dms_toggle != nullptr) {
        const QSignalBlocker blocker(st->dms_toggle); // 主题重放不动显示档，只保持控件态与档一致
        st->dms_toggle->setChecked(st->dms_mode);
        st->dms_toggle->set_tokens(t); // R-184：chip = formkit 自绘件（令牌随主题重放）
    }
    for (QPushButton *remove :
         st->page->findChildren<QPushButton *>(QStringLiteral("tag_remove"))) {
        icons::apply(remove, icons::Name::Clear, t.text2, t.text3, 12);
    }
}

// 折叠入口的插入符 = **矢量图标**（▸ 收起 / ▾ 展开），不再是字形文本（C3/A8）
void set_fold_icon(MetaState *st, QToolButton *button, bool expanded) {
    if (button == nullptr) {
        return;
    }
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    icons::apply(button, expanded ? icons::Name::ChevronDown : icons::Name::ChevronRight,
                 st->tokens.text3, st->tokens.text3, 12);
}

// ---------------------------------------------------------------------------
// 样式（tokens 单源 = ui/theme.h；本页只把 tokens 铺到自己的控件上；A6：硬编码色一律收口）
// ---------------------------------------------------------------------------

void apply_style(MetaState *st) {
    // B7（R-179）：QSS 纯合成 / banner QSS / 图标重放 已拆出（qss_for / map_banner_qss /
    // apply_icons）；本函数只做编排：样式串下发 → banner 收口 → 图标与令牌重放 → 阴影。
    const theme::Tokens &t = st->tokens;

    st->page->setStyleSheet(qss_for(t));

    // 地图离线 banner 的**宿主侧**收口（修 1-38/1-39：中性灰说明 + 括号进 tooltip；
    // mapwidget 文件零改动 —— 令牌/文案由本页铺）
    if (st->map != nullptr) {
        const QList<QLabel *> labels = st->map->findChildren<QLabel *>();
        for (QLabel *banner : labels) {
            if (!banner->text().contains(PageMeta::tr("地图离线"))) {
                continue;
            }
            banner->setText(PageMeta::tr("地图离线"));
            banner->setToolTip(PageMeta::tr("可手动输入坐标"));
            banner->setStyleSheet(map_banner_qss(t));
            break;
        }
    }

    apply_icons(st);
    // 自绘控件（胶囊/开关/分段/评级）持令牌副本
    for (formkit::PillBadge *pill : st->pills) {
        pill->set_tokens(t);
    }
    for (formkit::PillBadge *pill : st->keyword_pills) {
        pill->set_tokens(t);
    }
    for (formkit::SwitchControl *sw : st->switches) {
        sw->set_tokens(t);
    }
    if (st->seg != nullptr) {
        st->seg->set_tokens(t);
    }
    if (st->layer_seg != nullptr) {
        st->layer_seg->set_tokens(t); // M6-T8：图层 Seg 自绘（段填充/字色取令牌）
    }
    if (st->info_rating != nullptr) {
        st->info_rating->set_tokens(t);
    }
    // M6-T8：结果行重排（图钉图标取 --acc；空态占位取 --txt3 —— 与 QSS 同一次主题重放）
    if (st->search_results != nullptr) {
        fill_result_rows(st);
    }

    // 浅色卡阴影（§4.1 shadow；深色无阴影）
    const qreal dpr = st->page->devicePixelRatioF() > 0.01 ? st->page->devicePixelRatioF() : 1.0;
    for (QFrame *card : st->cards) {
        if (card == nullptr) {
            continue;
        }
        if (t.shadow_blur > 0) {
            auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
            if (shadow == nullptr) {
                shadow = new QGraphicsDropShadowEffect(card);
                card->setGraphicsEffect(shadow);
            }
            shadow->setColor(t.shadow_color);
            shadow->setBlurRadius(double(t.shadow_blur) * dpr);
            shadow->setXOffset(0);
            shadow->setYOffset(double(t.shadow_dy) * dpr);
        } else if (card->graphicsEffect() != nullptr) {
            card->setGraphicsEffect(nullptr); // 删旧效果（setGraphicsEffect(nullptr) 会析构它）
        }
    }
}

} // namespace pp::ui::meta
