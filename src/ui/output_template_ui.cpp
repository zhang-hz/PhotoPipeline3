// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 输出页模板段（output_template_ui）（B.3 余量 TU 拆分产物；M7-R2.1-F3）
// 拆分口径 = 行为零变化（方法体自 page_output.cpp 逐字搬移；公开面逐位不变）。
#include "ui/page_output_impl.h"

#include "ui/contextmenu.h" // M7-R2.5：变量表菜单的主题化

namespace pp::ui {

QString OutputPageImpl::template_text() const {
    return template_edit != nullptr ? template_edit->text() : QString();
}

QString OutputPageImpl::template_tooltip() {
    return PageOutput::tr(
        "变量：$format=格式目录 · $dir=源相对目录 · $file=文件名.扩展名 · $name=主名 · "
        "$ext=扩展名\n"
        "示例：$format/$dir/$file → …/jxl/2024-05/IMG_2731.jxl\n"
        "段间以 / 分隔；未知 $ 符号、.. 与绝对路径会被拒绝（开始运行前阻断）");
}

void OutputPageImpl::show_vars_menu() {
    struct Var {
        const char *token;
        const char *desc;
    };
    static const Var vars[] = {
        {"$format", "格式目录（jxl / png / …）"},
        {"$dir", "源相对目录（如 2024/05）"},
        {"$file", "文件名.扩展名"},
        {"$name", "主名（不含扩展名）"},
        {"$ext", "扩展名"},
    };
    QMenu menu(q);
    contextmenu::style_menu(&menu); // 深色页上的顶层弹窗必须显式套主题
    for (const Var &v : vars) {
        QAction *action = menu.addAction(QString::fromLatin1(v.token) + QStringLiteral(" · ") +
                                         PageOutput::tr(v.desc));
        QObject::connect(action, &QAction::triggered, q, [this, token = v.token] {
            const int pos = template_edit->cursorPosition();
            QString text = template_edit->text();
            text.insert(pos, QString::fromLatin1(token));
            template_edit->setText(text); // textChanged → 示例/信号
            template_edit->setCursorPosition(pos + QString::fromLatin1(token).size());
        });
    }
    menu.exec(vars_button->mapToGlobal(QPoint(0, vars_button->height())));
}

bool OutputPageImpl::template_has_format(const QString &tmpl) {
    return pp::template_has_format(tmpl.toStdString());
}

void OutputPageImpl::sync_split_switch(const QString &tmpl) {
    if (split_switch == nullptr)
        return;
    const QSignalBlocker blocker(split_switch);
    split_switch->setChecked(template_has_format(tmpl));
}

void OutputPageImpl::apply_split_to_template(bool on) {
    if (template_edit == nullptr)
        return;
    QString tmpl = template_edit->text();
    QStringList segs = tmpl.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const bool has = template_has_format(tmpl);
    if (on && !has) {
        segs.prepend(QStringLiteral("$format"));
    } else if (!on && has) {
        segs.removeAll(QStringLiteral("$format"));
        if (segs.isEmpty())
            segs << QStringLiteral("$dir") << QStringLiteral("$file");
    } else {
        return;
    }
    template_edit->setText(segs.join(QLatin1Char('/'))); // textChanged → 示例/信号
}

void OutputPageImpl::update_template_example() {
    if (template_example == nullptr || template_edit == nullptr)
        return;
    const QString tmpl = template_edit->text();
    std::string err;
    if (!pp::validate_output_template(tmpl.toStdString(), &err)) {
        template_example->setProperty("ppInvalid", true);
        theme::repolish(template_example);
        // R-070/273：错误分类映射中文（error_text.h 单源），原始引擎串进 tooltip
        const std::string zh = pp::error_text_zh(err);
        const QString shown = QString::fromStdString(zh.empty() ? err : zh);
        template_example->set_full_text(PageOutput::tr("模板无效：%1").arg(shown));
        template_example->setToolTip(QString::fromStdString(err)); // 原文（含动态段）
        return;
    }
    template_example->setProperty("ppInvalid", false);
    theme::repolish(template_example);
    QStringList samples;
    for (const std::string &id : sel.items()) {
        const pp::FormatDef *f = pp::find_format(id);
        if (f == nullptr)
            continue;
        pp::PathCtx ctx;
        ctx.format_dir = f->id;
        ctx.rel_dir = std::filesystem::path("2024") / "05";
        ctx.stem = "IMG_2731";
        ctx.ext = f->ext;
        const std::filesystem::path out =
            pp::render_output_path(tmpl.toStdString(), ctx, std::filesystem::path("."));
        if (out.empty())
            continue;
        QString rel = QString::fromStdString(out.generic_string());
        if (rel.startsWith(QStringLiteral("./")))
            rel.remove(0, 2);
        samples << QStringLiteral("…/") + rel; // 根目录前缀以 "…" 表示（与模板语法同族，无 <根>）
        // R-265：原"取两份样本"死码 —— 显示面只用 first + 「等 N 个输出路径」计数，
        // 第二样本永不呈现 → 只取一份（文案/示例输出逐字不变）。
        break;
    }
    if (sel.empty()) {
        template_example->set_full_text(PageOutput::tr("选择输出格式后显示输出路径示例"));
    } else if (samples.isEmpty()) {
        template_example->set_full_text(PageOutput::tr("模板为空的段（无可展开的路径）"));
    } else if (sel.size() > 1) {
        template_example->set_full_text(samples.first() + QLatin1Char(' ') +
                                        PageOutput::tr("等 %1 个输出路径").arg(sel.size()));
    } else {
        template_example->set_full_text(samples.first());
    }
    template_example->setToolTip(template_tooltip() + QLatin1Char('\n') +
                                 template_example->full_text());
}

} // namespace pp::ui
