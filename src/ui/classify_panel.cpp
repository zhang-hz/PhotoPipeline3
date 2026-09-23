// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T11）— 分类面板实现（设计 §6.2 / mockup 左栏 .cls-panel）
//
// 见 classify_panel.h 的规格与落地口径。本 TU 的实现要点：
//   * 行 = 自绘色点（DotLabel，零 QSS）+ 名称 QLabel + 计数 QLabel；整体 24px（.cls-row
//     padding 5px 12px + 11.5px 行高）；
//   * 行部件 contextMenuPolicy=CustomContextMenu → build_row_menu() + exec（不新增点击语义）；
//   * 「全部」= 保留虚拟分类（core/classify 的 kAllId）：计数 = 模型行数，列表首行；
//   * id 生成（新建）：名称 → ASCII  slug（[a-z0-9_-]）；无可用字符（中文名等）→ "class"；
//     再按注册表去重（-2/-3…）—— id 是持久化键，显示名可随时改；
//   * 颜色/热键对话框只作用于**已存在**的类；被注册表拒绝（热键冲突/保留 id）→ 无改动 +
//     状态提示（QMessageBox），绝不静默。
#include "ui/classify_panel.h"

#include <QAction>
#include <QApplication> // theme.h 依赖（qApp->style()）→ 必须先于 ui/theme.h
#include <QColor>
#include <QColorDialog>
#include <QCursor>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include "ui/filelistmodel.h"
#include "ui/theme.h"

namespace pp::ui {
namespace {

// 色点（mockup .cls-dot 9px 圆）；自绘，无 QSS，随主题无关（颜色来自类定义/保留色）
class DotLabel : public QWidget {
public:
    explicit DotLabel(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(kDot, kDot); }
    void set_color(const QColor &c) {
        color_ = c;
        update();
    }
    static constexpr int kDot = 9;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color_);
        painter.drawEllipse(rect());
    }

private:
    QColor color_;
};

constexpr int kRowHeight = 24; // .cls-row{padding:5px 12px} + 11.5px 行
constexpr int kRowPadX = 12;
constexpr int kDotGap = 8;

// 「全部」行的保留色点（mockup #c9c9d2，主题无关的灰）
const QColor kAllDot(0xc9, 0xc9, 0xd2);

// 新建分类的默认色轮（首色与 mockup 类色一致：精选绿 / 待定琥珀 / 废片红 / 扫描件紫）
const std::uint32_t kDefaultColors[] = {0x2E7D32u, 0xF9A825u, 0xC62828u, 0x7C4DFFu,
                                        0x0277BDu, 0x00897Bu, 0x6D4C41u, 0xAD1457u};

QString slugify(const QString &name) {
    QString out;
    for (const QChar &ch : name) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128) {
            out.append(ch.toLower());
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char(' ')) {
            if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
                out.append(QLatin1Char('-'));
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

} // namespace

struct ClassifyPanel::Impl {
    ClassifyPanel *panel = nullptr;
    pp::ClassRegistry *registry = nullptr;
    FileListModel *model = nullptr;
    theme::Tokens tokens;
    bool has_tokens = false;
    bool locked = false;
    QString current_class_id;     // 空 = 无分类（高亮「全部」）
    std::vector<QWidget *> rows;  // 行部件（index 0 = 「全部」）
    std::vector<QString> row_ids; // "" = 「全部」
    QVBoxLayout *rows_layout = nullptr;
    QWidget *new_btn = nullptr;
    QTimer *refresh_timer = nullptr;

    void rebuild_rows();
    void apply_row_styles();
    void apply_row_style(int row);
    void schedule_refresh(); // 打标/文件变化 → 0ms 合并刷新（批量打标不做 N 次面板重建）
    std::string id_at(int row) const {
        if (row <= 0)
            return {};
        if (registry == nullptr || std::size_t(row) > registry->classes.size())
            return {};
        return registry->classes[std::size_t(row) - 1].id;
    }
};

ClassifyPanel::ClassifyPanel(QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    Impl &d = *impl_;
    d.panel = this;
    setObjectName(QStringLiteral("pp-class-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    d.refresh_timer = new QTimer(this);
    d.refresh_timer->setSingleShot(true);
    d.refresh_timer->setInterval(0); // 合并同一轮事件里的多次变化（批量打标 = 1 次刷新）
    connect(d.refresh_timer, &QTimer::timeout, this, [this] { impl_->rebuild_rows(); });
    d.rows_layout = new QVBoxLayout();
    d.rows_layout->setContentsMargins(0, 0, 0, 0);
    d.rows_layout->setSpacing(0);
    layout->addLayout(d.rows_layout);
    d.rebuild_rows();
}

ClassifyPanel::~ClassifyPanel() = default;

void ClassifyPanel::set_tokens(const theme::Tokens &tokens) {
    impl_->tokens = tokens;
    impl_->has_tokens = true;
    impl_->apply_row_styles();
}

void ClassifyPanel::set_registry(pp::ClassRegistry *registry) {
    impl_->registry = registry;
    impl_->rebuild_rows();
}

void ClassifyPanel::set_model(FileListModel *model) {
    Impl &d = *impl_;
    if (d.model == model)
        return;
    if (d.model != nullptr)
        disconnect(d.model, nullptr, this, nullptr);
    d.model = model;
    if (d.model != nullptr) {
        // 文件集合/归属变化 → 计数与高亮跟随（打标/删行/清空都经这两条信号）
        // 批量打标会对每行发一次 class_changed → 走 0ms 合并刷新，避免 N 次面板重建
        connect(d.model, &FileListModel::content_changed, this,
                [this] { impl_->schedule_refresh(); });
        connect(d.model, &FileListModel::class_changed, this,
                [this] { impl_->schedule_refresh(); });
    }
    impl_->rebuild_rows();
}

void ClassifyPanel::set_locked(bool locked) {
    impl_->locked = locked;
    for (QWidget *row : impl_->rows) {
        if (row != nullptr)
            row->setEnabled(!locked);
    }
    if (impl_->new_btn != nullptr)
        impl_->new_btn->setEnabled(!locked);
    if (!locked)
        impl_->apply_row_styles(); // 解锁后补一次计数/高亮（锁定期不跟随 content_changed）
}

bool ClassifyPanel::locked() const { return impl_->locked; }

void ClassifyPanel::set_current_class_id(const QString &class_id) {
    if (impl_->current_class_id == class_id)
        return;
    impl_->current_class_id = class_id;
    impl_->apply_row_styles();
}

// ---------------------------------------------------------------------------
// 行构建 / 样式
// ---------------------------------------------------------------------------

void ClassifyPanel::Impl::rebuild_rows() {
    // 清旧行（含「＋ 新建分类…」按钮）
    while (QLayoutItem *item = rows_layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    rows.clear();
    row_ids.clear();

    const auto add_row = [&](const QString &id, const QString &label, int count,
                             const QColor &dot) {
        auto *row = new QWidget(panel);
        row->setObjectName(QStringLiteral("pp-class-row"));
        row->setFixedHeight(kRowHeight);
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(kRowPadX, 0, kRowPadX, 0);
        lay->setSpacing(kDotGap);
        auto *dot_label = new DotLabel(row);
        dot_label->setObjectName(QStringLiteral("pp-class-dot"));
        dot_label->set_color(dot);
        auto *name = new QLabel(label, row);
        name->setObjectName(QStringLiteral("pp-class-name"));
        auto *cnt = new QLabel(QString::number(count), row);
        cnt->setObjectName(QStringLiteral("pp-class-count"));
        lay->addWidget(dot_label);
        lay->addWidget(name);
        lay->addStretch(1);
        lay->addWidget(cnt);
        const int index = int(rows.size());
        connect(row, &QWidget::customContextMenuRequested, panel,
                [this, index, row](const QPoint &pos) {
                    if (locked)
                        return; // G5：运行中只读（不弹菜单）
                    QMenu *menu = panel->build_row_menu(index);
                    menu->setAttribute(Qt::WA_DeleteOnClose, true);
                    menu->exec(row->mapToGlobal(pos));
                });
        rows_layout->addWidget(row);
        rows.push_back(row);
        row_ids.push_back(id);
    };

    // 首行 = 「全部」（保留虚拟分类；计数 = 模型行数）
    const int total = model != nullptr ? int(model->size()) : 0;
    add_row(QString(), ClassifyPanel::tr("全部"), total, kAllDot);
    if (registry != nullptr) {
        for (const pp::ClassDef &c : registry->classes) {
            add_row(QString::fromStdString(c.id), QString::fromStdString(c.name),
                    int(model != nullptr ? model->class_file_count(c.id) : 0),
                    QColor(QRgb(c.rgb & 0xFFFFFFu)));
        }
    }

    // 末行 = 「＋ 新建分类…」（mockup .cls-new 全宽 mini-btn）
    auto *btn = new QPushButton(ClassifyPanel::tr("＋ 新建分类…"), panel);
    btn->setObjectName(QStringLiteral("pp-class-new"));
    btn->setFixedHeight(theme::Metrics::input_h);
    btn->setEnabled(!locked);
    connect(btn, &QPushButton::clicked, panel, [this] {
        if (locked)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(panel, ClassifyPanel::tr("新建分类"),
                                                   ClassifyPanel::tr("分类名称："),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            panel->new_class(name.trimmed());
    });
    auto *btn_row = new QWidget(panel);
    auto *btn_lay = new QHBoxLayout(btn_row);
    btn_lay->setContentsMargins(kRowPadX, 6, kRowPadX, 10); // .cls-new{margin:6px 12px 10px}
    btn_lay->addWidget(btn);
    rows_layout->addWidget(btn_row);
    new_btn = btn; // 供 set_locked/apply_row_styles 复用（成员持有，不额外父子关系）
    new_btn->setVisible(true);

    apply_row_styles();
}

void ClassifyPanel::Impl::apply_row_style(int row) {
    const bool active =
        row == 0 ? current_class_id.isEmpty() : row_ids[std::size_t(row)] == current_class_id;
    QWidget *row_widget = rows[std::size_t(row)];
    if (row_widget == nullptr)
        return;
    if (QLabel *name = row_widget->findChild<QLabel *>(QStringLiteral("pp-class-name"))) {
        QFont font = name->font();
        font.setBold(active); // .cls-row.on .nm{font-weight:700}
        name->setFont(font);
        const QColor color = active ? tokens.text : tokens.text2; // --txt / --txt2
        name->setStyleSheet(QStringLiteral("color:%1").arg(theme::css_color(color)));
    }
    if (QLabel *cnt = row_widget->findChild<QLabel *>(QStringLiteral("pp-class-count"))) {
        cnt->setStyleSheet(QStringLiteral("color:%1").arg(theme::css_color(tokens.text3)));
        cnt->setFont(theme::font(theme::Typography::hint_px));
    }
}

void ClassifyPanel::Impl::schedule_refresh() {
    if (locked)
        return; // 运行期只读：分类与文件集合都不变 → 计数无需跟随（避免运行期逐事件重扫）
    if (refresh_timer != nullptr)
        refresh_timer->start();
}

void ClassifyPanel::Impl::apply_row_styles() {
    if (!has_tokens)
        return;
    for (int i = 0; i < int(rows.size()); ++i) {
        QWidget *row = rows[std::size_t(i)];
        // 计数实时重取（文件增删/打标后跟随）
        if (QLabel *cnt = row != nullptr
                              ? row->findChild<QLabel *>(QStringLiteral("pp-class-count"))
                              : nullptr) {
            const std::string id = id_at(i);
            const int value = id.empty() ? int(model != nullptr ? model->size() : 0)
                                         : int(model != nullptr ? model->class_file_count(id) : 0);
            cnt->setText(QString::number(value));
        }
        apply_row_style(i);
    }
    if (new_btn != nullptr) {
        new_btn->setFont(theme::font(theme::Typography::body_px, 600));
        new_btn->setStyleSheet(
            QStringLiteral("QPushButton#pp-class-new { background:%1; border:1px solid %2; "
                           "border-radius:%3px; color:%4; }"
                           "QPushButton#pp-class-new:disabled { color:%5; }")
                .arg(theme::css_color(tokens.control), theme::css_color(tokens.control_bd))
                .arg(theme::Metrics::control_radius)
                .arg(theme::css_color(tokens.text2), theme::css_color(tokens.text3)));
    }
}

// ---------------------------------------------------------------------------
// 圈选（§6.2/D2）
// ---------------------------------------------------------------------------

void ClassifyPanel::check_class(const QString &class_id) {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->check_class(class_id.toStdString());
}

void ClassifyPanel::invert_class(const QString &class_id) {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->invert_class(class_id.toStdString());
}

void ClassifyPanel::clear_checks() {
    if (impl_->locked || impl_->model == nullptr)
        return;
    impl_->model->clear_checks();
}

// ---------------------------------------------------------------------------
// CRUD（§6.2）
// ---------------------------------------------------------------------------

bool ClassifyPanel::new_class(const QString &name, std::uint32_t rgb, char hotkey) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr || name.trimmed().isEmpty())
        return false;
    QString id = slugify(name);
    if (id.isEmpty())
        id = QStringLiteral("class");
    if (d.registry->find(id.toStdString()) != nullptr ||
        id == QString::fromUtf8(pp::ClassRegistry::kAllId.data(),
                                int(pp::ClassRegistry::kAllId.size()))) {
        QString candidate;
        for (int n = 2; n < 1000; ++n) {
            candidate = QStringLiteral("%1-%2").arg(id).arg(n);
            if (d.registry->find(candidate.toStdString()) == nullptr)
                break;
            candidate.clear();
        }
        id = candidate.isEmpty() ? id + QStringLiteral("-x") : candidate;
    }
    const std::uint32_t color =
        rgb != 0 ? rgb : kDefaultColors[d.registry->classes.size() % std::size(kDefaultColors)];
    if (!d.registry->add(pp::ClassDef{id.toStdString(), name.toStdString(), color, hotkey})) {
        emit error_occurred(tr("无法创建分类「%1」（名称/热键被占用）").arg(name));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::rename_class(const QString &id, const QString &name) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr || name.trimmed().isEmpty())
        return false;
    if (!d.registry->rename(id.toStdString(), name.toStdString())) {
        emit error_occurred(tr("无法重命名（分类不存在或名称为空）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::set_class_color(const QString &id, std::uint32_t rgb) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr)
        return false;
    if (!d.registry->set_color(id.toStdString(), rgb)) {
        emit error_occurred(tr("无法设置颜色（分类不存在）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::set_class_hotkey(const QString &id, char key) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr)
        return false;
    if (!d.registry->set_hotkey(id.toStdString(), key)) {
        // 热键唯一（core/classify 口径）：冲突/非法（'0' 保留作清除）一律拒绝
        emit error_occurred(tr("热键不可用（已被其它分类占用或非法）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

bool ClassifyPanel::delete_class(const QString &id) {
    Impl &d = *impl_;
    if (d.locked || d.registry == nullptr)
        return false;
    if (!d.registry->erase(id.toStdString())) {
        emit error_occurred(tr("无法删除（分类不存在）"));
        return false;
    }
    d.rebuild_rows();
    emit registry_changed();
    return true;
}

// ---------------------------------------------------------------------------
// 右键菜单（唯一交互入口）
// ---------------------------------------------------------------------------

QMenu *ClassifyPanel::build_row_menu(int row) {
    Impl &d = *impl_;
    auto *menu = new QMenu(this);
    const std::string id = d.id_at(row);
    const QString class_id = QString::fromStdString(id);
    const bool is_all = id.empty();

    // §6.2/D2 圈选三项（「全部」行同样可用：「全部」= 全部文件）
    QAction *a_all = menu->addAction(tr("全选该类"));
    QAction *a_inv = menu->addAction(tr("反选该类"));
    QAction *a_clear = menu->addAction(tr("清空勾选"));
    connect(a_all, &QAction::triggered, this, [this, class_id] { check_class(class_id); });
    connect(a_inv, &QAction::triggered, this, [this, class_id] { invert_class(class_id); });
    connect(a_clear, &QAction::triggered, this, [this] { clear_checks(); });

    if (!is_all) {
        menu->addSeparator();
        QAction *a_rename = menu->addAction(tr("重命名…"));
        QAction *a_color = menu->addAction(tr("颜色…"));
        QAction *a_hotkey = menu->addAction(tr("热键…"));
        QAction *a_del = menu->addAction(tr("删除"));
        connect(a_rename, &QAction::triggered, this, [this, class_id] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("重命名分类"), tr("新名称："),
                                                       QLineEdit::Normal, class_id, &ok);
            if (ok && !name.trimmed().isEmpty())
                rename_class(class_id, name.trimmed());
        });
        connect(a_color, &QAction::triggered, this, [this, class_id] {
            const pp::ClassDef *def = impl_->registry != nullptr
                                          ? impl_->registry->find(class_id.toStdString())
                                          : nullptr;
            const QColor initial =
                def != nullptr ? QColor(QRgb(def->rgb & 0xFFFFFFu)) : QColor(Qt::gray);
            const QColor chosen = QColorDialog::getColor(initial, this, tr("分类颜色"));
            if (chosen.isValid())
                set_class_color(class_id, std::uint32_t(chosen.rgb() & 0xFFFFFFu));
        });
        connect(a_hotkey, &QAction::triggered, this, [this, class_id] {
            // 热键候选 = 1–9 + 无；已被他类占用者置灰（唯一性由注册表强制）
            QMenu submenu;
            QStringList labels{QStringLiteral("无")};
            QList<char> keys{0};
            for (char k = '1'; k <= '9'; ++k) {
                keys << k;
                QString owner;
                if (impl_->registry != nullptr) {
                    for (const pp::ClassDef &c : impl_->registry->classes) {
                        if (c.hotkey == k) {
                            owner = QString::fromStdString(c.name);
                            break;
                        }
                    }
                }
                labels << (owner.isEmpty() ? QString(QChar(k))
                                           : QStringLiteral("%1（%2）").arg(QChar(k), owner));
            }
            QList<QAction *> actions;
            for (const QString &label : labels)
                actions << submenu.addAction(label);
            for (int i = 0; i < actions.size(); ++i) {
                const char key = keys.at(i);
                if (key == 0 || impl_->registry == nullptr)
                    continue;
                for (const pp::ClassDef &c : impl_->registry->classes) {
                    if (c.hotkey == key && c.id != class_id.toStdString()) {
                        actions.at(i)->setEnabled(false);
                        break;
                    }
                }
            }
            QAction *chosen = submenu.exec(QCursor::pos());
            if (chosen == nullptr)
                return;
            const int index = actions.indexOf(chosen);
            if (index >= 0)
                set_class_hotkey(class_id, keys.at(index));
        });
        connect(a_del, &QAction::triggered, this, [this, class_id] {
            const pp::ClassDef *def = impl_->registry != nullptr
                                          ? impl_->registry->find(class_id.toStdString())
                                          : nullptr;
            const QString name = def != nullptr ? QString::fromStdString(def->name) : class_id;
            if (QMessageBox::question(
                    this, tr("删除分类"), tr("删除分类「%1」？该分类的归属将一并清除。").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                delete_class(class_id);
            }
        });
    }
    return menu;
}

// ---------------------------------------------------------------------------
// 冒烟/诊断读数
// ---------------------------------------------------------------------------

int ClassifyPanel::row_count() const { return int(impl_->rows.size()); }

QString ClassifyPanel::row_id(int row) const {
    if (row < 0 || row >= int(impl_->row_ids.size()))
        return {};
    return impl_->row_ids[std::size_t(row)];
}

QString ClassifyPanel::row_label(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()) || impl_->rows[std::size_t(row)] == nullptr)
        return {};
    if (QLabel *name =
            impl_->rows[std::size_t(row)]->findChild<QLabel *>(QStringLiteral("pp-class-name")))
        return name->text();
    return {};
}

int ClassifyPanel::row_count_value(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()) || impl_->rows[std::size_t(row)] == nullptr)
        return -1;
    if (QLabel *cnt =
            impl_->rows[std::size_t(row)]->findChild<QLabel *>(QStringLiteral("pp-class-count")))
        return cnt->text().toInt();
    return -1;
}

bool ClassifyPanel::row_active(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()) || impl_->rows[std::size_t(row)] == nullptr)
        return false;
    if (QLabel *name =
            impl_->rows[std::size_t(row)]->findChild<QLabel *>(QStringLiteral("pp-class-name")))
        return name->font().bold();
    return false;
}

QStringList ClassifyPanel::hotkey_labels() const {
    QStringList out;
    if (impl_->registry == nullptr)
        return out;
    for (const pp::ClassDef &c : impl_->registry->classes) {
        if (c.hotkey == 0)
            continue;
        out << QStringLiteral("%1=%2").arg(QChar(c.hotkey)).arg(QString::fromStdString(c.name));
    }
    return out;
}

QWidget *ClassifyPanel::row_widget(int row) const {
    if (row < 0 || row >= int(impl_->rows.size()))
        return nullptr;
    return impl_->rows[std::size_t(row)];
}

} // namespace pp::ui
