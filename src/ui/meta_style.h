// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页样式（B.1 拆分步 B7：meta_style）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B7）+ R-179。契约注释见 meta_style.cpp。
#pragma once

#include "ui/theme.h"
#include <QApplication> // theme.h 要求包含者先引入

#include <QString>

namespace pp::ui::meta {

struct MetaState;

// QSS 纯合成（tokens → 样式串；页内 QSS 拼装单源，可单测）
QString qss_for(const theme::Tokens &t);

// 地图离线 banner 的宿主侧收口样式（R-179 遗留收口：命名纯函数）
QString map_banner_qss(const theme::Tokens &t);

// 矢量图标随 tokens 重放（主题切换唯一落点）
void apply_icons(MetaState *st);

} // namespace pp::ui::meta
