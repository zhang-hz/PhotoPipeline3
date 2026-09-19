// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — preset manager dialog (M1b-U6)
//
// 规格：docs/m1b-tasks.md §2.10（PresetsDialog 规格，冻结）：
//   QListWidget（预设名，单选）+ `名称` QLineEdit（保存用，选中列表项时带出）+
//   按钮行 `载入`/`另存为`/`删除`/`关闭`；删除需 QMessageBox::question 确认；
//   保存名清洗：仅保留 [A-Za-z0-9_\- ]，其余剔除；空 → 仅“另存为”禁用
//   （主对话 2026-09-20 裁定：载入/删除只看列表选中，与名称清洗无关）；
//   文件名 = `<名>.json`（路径由 MainWindow 用 presets_dir 拼接；对话框只回名字与列表路径）。
//
// 落地说明（冻结头无任何私有成员，故状态不在成员里；子控件用 objectName 查找）：
//   - action/name/path 三个返回值存成 dynamic property（pp_action/pp_name/pp_path），
//     在按钮槽里写入后 accept()；MainWindow 在 exec()==Accepted 后读取。
//   - path()：Load/Delete = 列表项携带的预设文件路径；SaveAs = 空串——目录属于
//     MainWindow（§2.10 明确分组）；name() 已按 [A-Za-z0-9_\- ] 清洗。
//   - 清洗在“取名字”时做（不在按键时拦截），并折叠连续空格 / 去首尾空白
//     （QString::simplified）→ 例："My 预设 #1" → "My 1"；清洗后为空只禁用“另存为”。
#include "ui/presets_dialog.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace pp::ui {
namespace {

// objectName（自验 / U10 --ui-smoke / 走查定位用）
constexpr const char* kList = "preset_list";
constexpr const char* kName = "preset_name";
constexpr const char* kLoad = "btn_load";
constexpr const char* kSaveAs = "btn_saveas";
constexpr const char* kDelete = "btn_delete";
constexpr const char* kClose = "btn_close";

// dynamic property 名（返回值状态）
constexpr const char* kPropAction = "pp_action";
constexpr const char* kPropName = "pp_name";
constexpr const char* kPropPath = "pp_path";

QListWidget* list_of(const QObject* o) {
    return o->findChild<QListWidget*>(QLatin1String(kList));
}
QLineEdit* name_of(const QObject* o) {
    return o->findChild<QLineEdit*>(QLatin1String(kName));
}
QPushButton* button_of(const QObject* o, const char* name) {
    return o->findChild<QPushButton*>(QLatin1String(name));
}

// 保存名清洗：剔除 [A-Za-z0-9_\- ] 之外的字符，再折叠空白（simplified 同时去首尾）
QString sanitize_name(const QString& in) {
    static const QRegularExpression disallowed(QStringLiteral("[^A-Za-z0-9_\\- ]"));
    QString s = in;
    s.remove(disallowed);
    return s.simplified();
}

// 按钮可用性（主对话 2026-09-20 裁定；§2.10 更新版）：
//   载入/删除 = 列表有选中（预设路径本就是合法文件，与名称清洗无关）
//   另存为   = 清洗后名字非空（空名无法构成 <名>.json）
void refresh_buttons(const QDialog* dlg) {
    QListWidget* list = list_of(dlg);
    QLineEdit* edit = name_of(dlg);
    if (list == nullptr || edit == nullptr) return;
    const bool has_selection = list->currentItem() != nullptr;
    const bool has_name = !sanitize_name(edit->text()).isEmpty();
    if (QPushButton* b = button_of(dlg, kLoad)) b->setEnabled(has_selection);
    if (QPushButton* b = button_of(dlg, kSaveAs)) b->setEnabled(has_name);
    if (QPushButton* b = button_of(dlg, kDelete)) b->setEnabled(has_selection);
}

// 记录动作结果并关闭对话框（MainWindow 在 exec()==Accepted 后取 action()/name()/path()）
void commit(PresetsDialog* dlg, PresetsDialog::Action a, const QString& name,
            const QString& path) {
    dlg->setProperty(kPropAction, static_cast<int>(a));
    dlg->setProperty(kPropName, name);
    dlg->setProperty(kPropPath, path);
    dlg->accept();
}

void do_load(PresetsDialog* dlg) {
    QListWidget* list = list_of(dlg);
    QListWidgetItem* item = list != nullptr ? list->currentItem() : nullptr;
    if (item == nullptr) return;
    commit(dlg, PresetsDialog::Action::Load, item->text(), item->data(Qt::UserRole).toString());
}

void do_save_as(PresetsDialog* dlg) {
    QLineEdit* edit = name_of(dlg);
    if (edit == nullptr) return;
    const QString name = sanitize_name(edit->text());
    if (name.isEmpty()) return;     // 空名：按钮本已禁用，这里再兜一次
    edit->setText(name);            // 回显清洗后的名字（用户可见）
    // path 留空：<presets_dir>/<name>.json 由 MainWindow 拼接（§2.10）
    commit(dlg, PresetsDialog::Action::SaveAs, name, QString());
}

void do_delete(PresetsDialog* dlg) {
    QListWidget* list = list_of(dlg);
    QListWidgetItem* item = list != nullptr ? list->currentItem() : nullptr;
    if (item == nullptr) return;
    const QString name = item->text();
    const QString path = item->data(Qt::UserRole).toString();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        dlg, PresetsDialog::tr("删除预设"),
        PresetsDialog::tr("确定删除预设“%1”？").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;   // 取消二次确认：不动作、不关闭
    commit(dlg, PresetsDialog::Action::Delete, name, path);
}

}  // namespace

PresetsDialog::PresetsDialog(const std::vector<std::pair<QString, QString>>& presets,
                             const QString& suggested_name, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("预设"));
    setProperty(kPropAction, static_cast<int>(Action::None));
    setProperty(kPropName, QString());
    setProperty(kPropPath, QString());

    auto* list = new QListWidget(this);
    list->setObjectName(QLatin1String(kList));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const auto& entry : presets) {          // (path, display-name)
        auto* item = new QListWidgetItem(entry.second, list);
        item->setData(Qt::UserRole, entry.first);
    }

    auto* name_edit = new QLineEdit(this);
    name_edit->setObjectName(QLatin1String(kName));
    name_edit->setText(suggested_name);

    auto* form = new QFormLayout;
    form->addRow(tr("名称"), name_edit);

    auto* load_btn = new QPushButton(tr("载入"), this);
    load_btn->setObjectName(QLatin1String(kLoad));
    auto* saveas_btn = new QPushButton(tr("另存为"), this);
    saveas_btn->setObjectName(QLatin1String(kSaveAs));
    auto* delete_btn = new QPushButton(tr("删除"), this);
    delete_btn->setObjectName(QLatin1String(kDelete));
    auto* close_btn = new QPushButton(tr("关闭"), this);
    close_btn->setObjectName(QLatin1String(kClose));

    auto* button_row = new QHBoxLayout;
    button_row->addWidget(load_btn);
    button_row->addWidget(saveas_btn);
    button_row->addWidget(delete_btn);
    button_row->addStretch(1);
    button_row->addWidget(close_btn);

    auto* root = new QVBoxLayout(this);
    root->addWidget(list, 1);
    root->addLayout(form);
    root->addLayout(button_row);

    // 选中列表项 → 名称行带出该项名（§2.10）
    connect(list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                if (current != nullptr) {
                    if (QLineEdit* edit = name_of(this)) edit->setText(current->text());
                }
                refresh_buttons(this);
            });
    connect(name_edit, &QLineEdit::textChanged, this, [this] { refresh_buttons(this); });
    connect(load_btn, &QPushButton::clicked, this, [this] { do_load(this); });
    connect(saveas_btn, &QPushButton::clicked, this, [this] { do_save_as(this); });
    connect(delete_btn, &QPushButton::clicked, this, [this] { do_delete(this); });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::reject);

    refresh_buttons(this);
    resize(420, 360);
}

PresetsDialog::Action PresetsDialog::action() const {
    return static_cast<Action>(property(kPropAction).toInt());
}

QString PresetsDialog::name() const {
    return property(kPropName).toString();
}

QString PresetsDialog::path() const {
    return property(kPropPath).toString();
}

}  // namespace pp::ui
