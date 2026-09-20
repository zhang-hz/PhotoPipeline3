// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — single-file metadata editor dialog (M1b-U8)
//
// 规格：docs/m1b-tasks.md §2.12（冻结行为）+ docs/design.md §5.1/§6.4
//
// 落地要点：
//   * 源文件永远只读：pp::read_metadata 的结果只用于展示与校验；所有编辑只进
//     MetadataOverride（取消 → 不修改，确定 → 也只返回 override，不写任何文件）。
//   * 确定校验：把全部编辑经 pp::apply_edits 打到 read_metadata 的副本上；errors 非空 →
//     QMessageBox 列出错误并保持对话框打开（不 accept）。
//   * EXIF 分组（§2.12）：IFD0=Exif.Image.* / Exif=Exif.Photo.* / GPS=Exif.GPSInfo.* /
//     MakerNote（只读）= 其余分组全部归并，子节点按原分组名。MakerNote 归并组禁编辑；
//     另把 Exif.Photo.MakerNote 这个二进制块本身也置为只读（避免文本编辑破坏 MakerNote，
//     见报告 next-needed）。
//   * 〔批〕= 批量规则会命中的标签：时间字段（EXIF 3 + OffsetTime* 3 / XMP CreateDate 族）、
//     GPS 字段（批量 gps 或 gps_clear 时的 Exif.GPSInfo.*）、strip_privacy（全部）、
//     edits 命中键。勾选“忽略全部批量规则”后该文件不再受批量规则影响 → 标记消失。
//   * 已改值项前缀 “● ”；值留空 = 删除该标签（remove 编辑）。
//   * 时间三态“清除”：MetadataOverride 没有对应字段，语义化为对时间标签的 remove 编辑
//     （EXIF 3 + XMP 2；见报告 next-needed）；GPS“清除”= gps_clear；隐私剥除 = optional<bool>。
//   * 自验开关（不改冻结头）：动态属性 "pp_exif_editor_suppress_modal" = true 时，所有
//     QMessageBox 改为把内容写进动态属性（"pp_last_validation_errors" / "pp_last_warning"），
//     offscreen 自验程序据此断言“校验失败不 accept”，不会阻塞在模态框上。
//   * 对象名（objectName）供 --ui-smoke / .cache/tmp 自验程序查找控件。
//   * M2-T7 #29：XMP 页增设搜索框（objectName `xmp_search`），过滤逻辑与 EXIF 页
//     `exif_search` 同构（命中可见 / 非命中隐藏 / 组内无命中 → 组隐藏 / 清空恢复）。
#include "ui/exif_editor.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QLocale>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pp::ui {
namespace {

constexpr int kKeyRole = Qt::UserRole;       // QString: full Exiv2 key
constexpr int kNodeRole = Qt::UserRole + 1;  // int: 0 = tag leaf, 1 = group node
constexpr int kLeaf = 0;
constexpr int kGroupNode = 1;
constexpr int kTreeValueMax = 120;      // §2.12 树内只展示值的前若干字符（完整值在右侧编辑区）
constexpr int kMultilineThreshold = 60; // §2.12：长 ASCII > 60 → QPlainTextEdit

const char* const kSuppressModal = "pp_exif_editor_suppress_modal";
const char* const kLastErrors = "pp_last_validation_errors";
const char* const kLastWarning = "pp_last_warning";

const char* const kExifTimeKeys[3] = {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized",
                                      "Exif.Image.DateTime"};
const char* const kXmpTimeKeys[2] = {"Xmp.xmp.CreateDate", "Xmp.xmp.ModifyDate"};

QString T(const char* s) { return ExifEditor::tr(s); }

// "Exif.Image.Artist" → "Artist"; "Xmp.dc.title" → "title"
std::string tag_name(const std::string& key) {
    const std::size_t dot = key.rfind('.');
    return dot == std::string::npos ? key : key.substr(dot + 1);
}

// "Exif.<group>.<tag>" / "Xmp.<prefix>.<prop>" → 第二段（EXIF 分组名 / XMP 命名空间前缀）
std::string key_second(const std::string& key) {
    const std::size_t first = key.find('.');
    if (first == std::string::npos) return {};
    const std::size_t second = key.find('.', first + 1);
    if (second == std::string::npos) return {};
    return key.substr(first + 1, second - first - 1);
}

std::string clean_value(std::string s) {
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

// G15：LangAlt v1 只编辑 x-default。Exiv2 文档：LangAltValue::toString(0) 返回 x-default 文本。
std::string metadatum_text(const Exiv2::Metadatum& d) {
    std::string s;
    if (d.typeId() == Exiv2::langAlt) {
        s = d.toString(0);
        static const std::string kLangPrefix = "lang=\"x-default\" ";
        if (s.rfind(kLangPrefix, 0) == 0) s = s.substr(kLangPrefix.size());
    } else {
        s = d.toString();
    }
    return clean_value(std::move(s));
}

bool exif_std_group(const std::string& g) { return g == "Image" || g == "Photo" || g == "GPSInfo"; }

// MakerNote 归并组（非 Image/Photo/GPSInfo 的分组）与 MakerNote 二进制块都只读
bool exif_row_read_only(const std::string& key) {
    if (!exif_std_group(key_second(key))) return true;
    return tag_name(key).find("MakerNote") != std::string::npos;
}

bool is_user_comment(const std::string& key) {
    return tag_name(key).find("UserComment") != std::string::npos;
}

bool is_long_text_type(Exiv2::TypeId t) {
    switch (t) {
        case Exiv2::asciiString:
        case Exiv2::comment:
        case Exiv2::string:
        case Exiv2::xmpText:
        case Exiv2::langAlt:
            return true;
        default:
            return false;
    }
}

// —— 〔批〕判定 ——
bool is_exif_time_key(const std::string& k) {
    return k == "Exif.Photo.DateTimeOriginal" || k == "Exif.Photo.DateTimeDigitized" ||
           k == "Exif.Image.DateTime" || k == "Exif.Photo.OffsetTime" ||
           k == "Exif.Photo.OffsetTimeOriginal" || k == "Exif.Photo.OffsetTimeDigitized";
}

bool is_xmp_time_key(const std::string& k) {
    return k == "Xmp.xmp.CreateDate" || k == "Xmp.xmp.ModifyDate" || k == "Xmp.xmp.MetadataDate";
}

bool edits_hit(const std::vector<pp::TagEdit>& v, const std::string& key) {
    return std::any_of(v.begin(), v.end(), [&](const pp::TagEdit& e) { return e.key == key; });
}

const pp::BatchRules kNoRules{};

bool batch_affects(const pp::BatchRules& b, const std::string& key, bool is_xmp) {
    if (b.strip_privacy) return true;  // strip_privacy 全部
    if (is_xmp) {
        if (edits_hit(b.xmp_edits, key)) return true;
        return b.time_shift.has_value() && is_xmp_time_key(key);
    }
    if (edits_hit(b.exif_edits, key)) return true;
    if (b.time_shift.has_value() && is_exif_time_key(key)) return true;
    if ((b.gps.has_value() || b.gps_clear) && key_second(key) == "GPSInfo") return true;
    return false;
}

// —— 时间 / GPS 显示工具（与批量页 §2.11 同口径）——
QString offset_label(int minutes) {
    const QChar sign = minutes < 0 ? u'-' : u'+';
    const int a = std::abs(minutes);
    return QStringLiteral("UTC%1%2:%3")
        .arg(sign)
        .arg(a / 60, 2, 10, QChar(u'0'))
        .arg(a % 60, 2, 10, QChar(u'0'));
}

// 31°13'49.4"N / 121°28'25.3"E（§3.2：度分秒一位小数）
QString dms_component(double v, bool lat) {
    const char* hemi = lat ? (v < 0 ? "S" : "N") : (v < 0 ? "W" : "E");
    const double a = std::fabs(v);
    int deg = static_cast<int>(a);
    double m = (a - deg) * 60.0;
    int min = static_cast<int>(m);
    double sec = std::round((m - min) * 60.0 * 10.0) / 10.0;
    if (sec >= 60.0) {
        sec -= 60.0;
        ++min;
    }
    if (min >= 60) {
        min -= 60;
        ++deg;
    }
    return QStringLiteral("%1°%2'%3\"%4")
        .arg(deg)
        .arg(min, 2, 10, QChar(u'0'))
        .arg(sec, 0, 'f', 1)
        .arg(QString::fromLatin1(hemi));
}

}  // namespace

// ===========================================================================
// Impl
// ===========================================================================

struct ValuePane {
    bool is_xmp = false;
    std::string key;
    QLineEdit* line = nullptr;
    QPlainTextEdit* multi = nullptr;
    QLabel* title = nullptr;
    QLabel* type = nullptr;
    QLabel* note = nullptr;
};

struct ExifEditor::Impl {
    ExifEditor* q = nullptr;
    QString src;
    pp::BatchRules batch;
    std::optional<pp::MetadataOverride> existing;
    pp::SourceMeta meta;  // 只读源元数据（pp::read_metadata 结果）

    std::vector<pp::TagEdit> exif_edits, xmp_edits;  // 树中的 set/remove 记录

    struct Row {
        std::string key;
        std::string src_value;
        bool in_source = false;
        bool is_xmp = false;
        bool read_only = false;
        bool lang_alt = false;
        Exiv2::TypeId type = Exiv2::asciiString;
        std::string type_name;
        QTreeWidgetItem* item = nullptr;
    };
    std::map<std::string, Row> rows;

    QCheckBox* ignore_check = nullptr;
    QTabWidget* tabs = nullptr;

    // EXIF 页
    QLineEdit* exif_search = nullptr;
    QTreeWidget* exif_tree = nullptr;
    QTreeWidgetItem* ifd0_node = nullptr;
    QTreeWidgetItem* exif_node = nullptr;
    QTreeWidgetItem* gps_node = nullptr;
    QTreeWidgetItem* maker_node = nullptr;
    std::map<std::string, QTreeWidgetItem*> maker_subs;
    ValuePane exif_pane;
    QLineEdit* exif_number = nullptr;
    QComboBox* exif_group = nullptr;
    QComboBox* exif_type = nullptr;

    // XMP 页
    QLineEdit* xmp_search = nullptr;   // M2-T7 #29（objectName `xmp_search`）
    QTreeWidget* xmp_tree = nullptr;
    std::map<std::string, QTreeWidgetItem*> xmp_groups;
    ValuePane xmp_pane;
    QLineEdit* xmp_path = nullptr;

    // 时间 / GPS 页
    QComboBox* time_mode = nullptr;
    QWidget* time_params = nullptr;
    QComboBox* time_method = nullptr;
    QWidget* time_delta_box = nullptr;
    QWidget* time_tz_box = nullptr;
    QSpinBox* time_spin[6] = {};
    QComboBox* tz_from = nullptr;
    QComboBox* tz_to = nullptr;
    QComboBox* gps_mode = nullptr;
    QWidget* gps_params = nullptr;
    QLineEdit* gps_lat = nullptr;
    QLineEdit* gps_lon = nullptr;
    QLabel* gps_dms = nullptr;
    QGroupBox* gps_more = nullptr;
    QLineEdit* gps_alt = nullptr;
    QLineEdit* gps_dir = nullptr;
    QLineEdit* gps_ts = nullptr;
    QComboBox* privacy_mode = nullptr;

    void build();
    QWidget* make_exif_tab();
    QWidget* make_xmp_tab();
    QWidget* make_time_gps_tab();
    QWidget* make_value_pane(ValuePane& vp, const QString& name);

    void populate_exif_tree();
    void populate_xmp_tree();
    void init_from_existing();

    void add_row_item(Row r);
    void ensure_row(const std::string& key, bool is_xmp, Exiv2::TypeId chosen);
    QTreeWidgetItem* group_parent(const std::string& key, bool is_xmp);

    void on_index_selection(ValuePane& vp, const QModelIndex& idx);
    void show_row(ValuePane& vp, const std::string& key);
    void clear_pane(ValuePane& vp);
    void commit(ValuePane& vp, const QString& text);
    void set_edit(bool is_xmp, const std::string& key, const std::string& text);
    const pp::TagEdit* find_edit(bool is_xmp, const std::string& key) const;
    std::string effective_value(const Row& r) const;
    QString row_text(const Row& r) const;
    void refresh_row(const std::string& key);
    void refresh_all_rows();

    void apply_exif_filter();
    void apply_xmp_filter();   // M2-T7 #29：与 apply_exif_filter 同构（仅作用 is_xmp 行）
    void add_exif_by_number();
    void add_xmp_by_path();
    Exiv2::TypeId type_from_combo() const;

    void update_time_mode();
    void update_gps_mode();
    void update_dms();
    pp::TimeShift time_shift_from_widgets() const;

    pp::MetadataOverride build_override(std::vector<std::string>* errors) const;
    void on_accept();
    void report_errors(const std::vector<std::string>& errors);
    void warn(const QString& title, const QString& text);
};

// —— 值编辑区 ——

QWidget* ExifEditor::Impl::make_value_pane(ValuePane& vp, const QString& name) {
    auto* box = new QWidget();
    auto* v = new QVBoxLayout(box);

    vp.title = new QLabel();
    vp.title->setObjectName(name + QStringLiteral("_value_title"));
    QFont bold = vp.title->font();
    bold.setBold(true);
    vp.title->setFont(bold);
    vp.title->setWordWrap(true);
    v->addWidget(vp.title);

    vp.type = new QLabel();
    QPalette gray = vp.type->palette();
    gray.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    vp.type->setPalette(gray);
    v->addWidget(vp.type);

    // 值编辑区纵向填满右半区（单行 QLineEdit 也设 Expanding：下半部不留大片空白）
    vp.line = new QLineEdit();
    vp.line->setObjectName(name + QStringLiteral("_value"));
    vp.line->setClearButtonEnabled(true);
    vp.line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    v->addWidget(vp.line, 1);

    vp.multi = new QPlainTextEdit();
    vp.multi->setObjectName(name + QStringLiteral("_value_multi"));
    vp.multi->setMinimumHeight(140);
    vp.multi->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vp.multi->setVisible(false);
    v->addWidget(vp.multi, 1);

    vp.note = new QLabel();
    vp.note->setWordWrap(true);
    QPalette note_pal = vp.note->palette();
    note_pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    vp.note->setPalette(note_pal);
    v->addWidget(vp.note);

    // textChanged（而不是 textEdited）：程序化 setText 也能触发提交，自验/集成可直接注入值
    QObject::connect(vp.line, &QLineEdit::textChanged, q,
                     [this, &vp](const QString& text) { commit(vp, text); });
    QObject::connect(vp.multi, &QPlainTextEdit::textChanged, q,
                     [this, &vp] { commit(vp, vp.multi->toPlainText()); });

    clear_pane(vp);
    return box;
}

void ExifEditor::Impl::clear_pane(ValuePane& vp) {
    vp.key.clear();
    if (!vp.line) return;
    {
        const QSignalBlocker b1(vp.line);
        const QSignalBlocker b2(vp.multi);
        vp.line->clear();
        vp.multi->clear();
    }
    vp.line->setReadOnly(false);
    vp.multi->setReadOnly(false);
    vp.line->setVisible(true);
    vp.multi->setVisible(false);
    vp.title->setText(T("未选择标签"));
    vp.type->clear();
    vp.note->setText(T("在左侧选择标签后编辑其值"));
}

void ExifEditor::Impl::show_row(ValuePane& vp, const std::string& key) {
    auto it = rows.find(key);
    if (it == rows.end()) {
        clear_pane(vp);
        return;
    }
    const Row& r = it->second;
    vp.key = key;
    vp.title->setText(QString::fromStdString(key));
    vp.type->setText(T("类型：") + QString::fromLatin1(r.type_name.c_str()));

    const std::string val = effective_value(r);
    const bool multiline =
        is_user_comment(key) || (is_long_text_type(r.type) && val.size() > kMultilineThreshold);
    {
        const QSignalBlocker b1(vp.line);
        const QSignalBlocker b2(vp.multi);
        vp.line->setText(QString::fromStdString(val));
        vp.multi->setPlainText(QString::fromStdString(val));
        vp.line->setVisible(!multiline);
        vp.multi->setVisible(multiline);
    }
    vp.line->setReadOnly(r.read_only);
    vp.multi->setReadOnly(r.read_only);

    QString note;
    if (r.read_only) {
        note = T("MakerNote（只读）");
    } else {
        note = T("值留空 = 删除该标签");
    }
    if (r.lang_alt) note += T("　LangAlt 只编辑 x-default（值后缀标注）");
    vp.note->setText(note);
}

void ExifEditor::Impl::set_edit(bool is_xmp, const std::string& key, const std::string& text) {
    auto row_it = rows.find(key);
    const bool in_source = row_it != rows.end() && row_it->second.in_source;
    const std::string src_value = row_it != rows.end() ? row_it->second.src_value : std::string();

    std::vector<pp::TagEdit>& list = is_xmp ? xmp_edits : exif_edits;
    auto edit_it = std::find_if(list.begin(), list.end(),
                                [&](const pp::TagEdit& e) { return e.key == key; });

    // 空值：源中不存在 → 本来就是无操作；源中存在 → remove。非空且等于源值 → 撤销记录。
    const bool noop = text.empty() ? !in_source : (in_source && text == src_value);
    if (noop) {
        if (edit_it != list.end()) list.erase(edit_it);
        return;
    }
    pp::TagEdit e;
    e.key = key;
    if (text.empty()) {
        e.remove = true;
    } else {
        e.value = text;
    }
    if (edit_it != list.end()) {
        *edit_it = e;
    } else {
        list.push_back(e);
    }
}

void ExifEditor::Impl::commit(ValuePane& vp, const QString& text) {
    if (vp.key.empty()) return;
    auto it = rows.find(vp.key);
    if (it == rows.end() || it->second.read_only) return;
    set_edit(vp.is_xmp, vp.key, text.toStdString());
    refresh_row(vp.key);
}

const pp::TagEdit* ExifEditor::Impl::find_edit(bool is_xmp, const std::string& key) const {
    const std::vector<pp::TagEdit>& list = is_xmp ? xmp_edits : exif_edits;
    for (const pp::TagEdit& e : list) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

std::string ExifEditor::Impl::effective_value(const Row& r) const {
    if (const pp::TagEdit* e = find_edit(r.is_xmp, r.key)) {
        if (e->remove || !e->value.has_value()) return {};
        return *e->value;
    }
    return r.src_value;
}

QString ExifEditor::Impl::row_text(const Row& r) const {
    QString t;
    const pp::BatchRules& eff =
        (ignore_check && ignore_check->isChecked()) ? kNoRules : batch;
    if (batch_affects(eff, r.key, r.is_xmp)) t += QStringLiteral("〔批〕");
    const pp::TagEdit* e = find_edit(r.is_xmp, r.key);
    if (e) t += QStringLiteral("● ");
    t += QString::fromStdString(tag_name(r.key));
    t += QStringLiteral(" = ");
    QString v;
    if (e && (e->remove || !e->value.has_value())) {
        v = T("（将删除）");
    } else {
        v = QString::fromStdString(effective_value(r));
        if (r.lang_alt && !v.isEmpty()) v += T("（x-default）");
    }
    if (v.size() > kTreeValueMax) v = v.left(kTreeValueMax) + QStringLiteral("…");
    t += v;
    return t;
}

void ExifEditor::Impl::refresh_row(const std::string& key) {
    auto it = rows.find(key);
    if (it == rows.end() || !it->second.item) return;
    it->second.item->setText(0, row_text(it->second));
    it->second.item->setToolTip(0, QString::fromStdString(it->second.key + "  [" +
                                                          it->second.type_name + "]"));
}

void ExifEditor::Impl::refresh_all_rows() {
    for (auto& [key, row] : rows) {
        (void)key;
        refresh_row(row.key);
    }
}

// —— 树构造 ——

void ExifEditor::Impl::add_row_item(Row r) {
    r.item = new QTreeWidgetItem(group_parent(r.key, r.is_xmp));
    r.item->setData(0, kKeyRole, QString::fromStdString(r.key));
    r.item->setData(0, kNodeRole, kLeaf);
    const std::string key = r.key;
    rows.emplace(key, std::move(r));
    refresh_row(key);
}

QTreeWidgetItem* ExifEditor::Impl::group_parent(const std::string& key, bool is_xmp) {
    if (is_xmp) {
        const std::string prefix = key_second(key);
        auto it = xmp_groups.find(prefix);
        if (it != xmp_groups.end()) return it->second;
        auto* node = new QTreeWidgetItem(xmp_tree);
        node->setText(0, QString::fromStdString(prefix.empty() ? std::string("(?)") : prefix));
        node->setData(0, kNodeRole, kGroupNode);
        try {
            node->setToolTip(0, QString::fromStdString(Exiv2::XmpProperties::ns(prefix)));
        } catch (...) {
            // 未注册前缀：树仍展示（校验阶段由 apply_edits 报错）
        }
        xmp_groups.emplace(prefix, node);
        return node;
    }
    const std::string g = key_second(key);
    if (g == "Image") return ifd0_node;
    if (g == "Photo") return exif_node;
    if (g == "GPSInfo") return gps_node;
    auto it = maker_subs.find(g);
    if (it != maker_subs.end()) return it->second;
    auto* node = new QTreeWidgetItem(maker_node);
    node->setText(0, QString::fromStdString(g.empty() ? std::string("(?)") : g));
    node->setData(0, kNodeRole, kGroupNode);
    maker_subs.emplace(g, node);
    return node;
}

void ExifEditor::Impl::populate_exif_tree() {
    ifd0_node = new QTreeWidgetItem(exif_tree);
    ifd0_node->setText(0, T("IFD0"));
    ifd0_node->setData(0, kNodeRole, kGroupNode);
    exif_node = new QTreeWidgetItem(exif_tree);
    exif_node->setText(0, T("Exif"));
    exif_node->setData(0, kNodeRole, kGroupNode);
    gps_node = new QTreeWidgetItem(exif_tree);
    gps_node->setText(0, T("GPS"));
    gps_node->setData(0, kNodeRole, kGroupNode);
    maker_node = new QTreeWidgetItem(exif_tree);
    maker_node->setText(0, T("MakerNote（只读）"));
    maker_node->setData(0, kNodeRole, kGroupNode);

    const Exiv2::ExifData& exif = meta.exif;
    for (const Exiv2::Exifdatum& d : exif) {
        Row r;
        r.key = d.key();
        r.is_xmp = false;
        r.in_source = true;
        r.src_value = metadatum_text(d);
        r.type = d.typeId();
        const char* tn = Exiv2::TypeInfo::typeName(r.type);
        r.type_name = tn ? tn : "Unknown";
        r.lang_alt = (r.type == Exiv2::langAlt);
        r.read_only = exif_row_read_only(r.key);
        add_row_item(std::move(r));
    }
    exif_tree->expandAll();
}

void ExifEditor::Impl::populate_xmp_tree() {
    const Exiv2::XmpData& xmp = meta.xmp;
    for (const Exiv2::Xmpdatum& d : xmp) {
        Row r;
        r.key = d.key();
        r.is_xmp = true;
        r.in_source = true;
        r.src_value = metadatum_text(d);
        r.type = d.typeId();
        const char* tn = Exiv2::TypeInfo::typeName(r.type);
        r.type_name = tn ? tn : "Unknown";
        r.lang_alt = (r.type == Exiv2::langAlt);
        r.read_only = false;
        add_row_item(std::move(r));
    }
    xmp_tree->expandAll();
}

void ExifEditor::Impl::ensure_row(const std::string& key, bool is_xmp, Exiv2::TypeId chosen) {
    if (rows.find(key) != rows.end()) return;
    Row r;
    r.key = key;
    r.is_xmp = is_xmp;
    if (!is_xmp) {
        try {
            const Exiv2::ExifKey ek(key);
            auto it = meta.exif.findKey(ek);
            if (it != meta.exif.end()) {
                r.in_source = true;
                r.src_value = metadatum_text(*it);
                r.type = it->typeId();
            } else {
                r.type = ek.defaultTypeId();
            }
        } catch (...) {
            r.type = chosen;
        }
    } else {
        try {
            const Exiv2::XmpKey xk(key);
            auto it = meta.xmp.findKey(xk);
            if (it != meta.xmp.end()) {
                r.in_source = true;
                r.src_value = metadatum_text(*it);
                r.type = it->typeId();
            } else {
                r.type = Exiv2::XmpProperties::propertyType(xk);
            }
        } catch (...) {
            r.type = chosen;
        }
    }
    if (r.type == Exiv2::invalidTypeId) r.type = is_xmp ? Exiv2::xmpText : chosen;
    const char* tn = Exiv2::TypeInfo::typeName(r.type);
    r.type_name = tn ? tn : "Unknown";
    r.lang_alt = (r.type == Exiv2::langAlt);
    r.read_only = !is_xmp && exif_row_read_only(key);
    add_row_item(std::move(r));
}

// —— 选择 / 过滤 ——

void ExifEditor::Impl::on_index_selection(ValuePane& vp, const QModelIndex& idx) {
    if (!idx.isValid() || idx.data(kNodeRole).toInt() != kLeaf) {
        clear_pane(vp);
        return;
    }
    show_row(vp, idx.data(kKeyRole).toString().toStdString());
}

void ExifEditor::Impl::apply_exif_filter() {
    const QString needle = exif_search->text().trimmed();
    for (auto& [key, row] : rows) {
        (void)key;
        if (row.is_xmp || !row.item) continue;
        row.item->setHidden(!needle.isEmpty() &&
                            !row.item->text(0).contains(needle, Qt::CaseInsensitive));
    }
    std::function<bool(QTreeWidgetItem*)> node_visible = [&](QTreeWidgetItem* node) -> bool {
        bool any = false;
        for (int i = 0; i < node->childCount(); ++i) {
            QTreeWidgetItem* child = node->child(i);
            const bool vis = (child->data(0, kNodeRole).toInt() == kLeaf) ? !child->isHidden()
                                                                          : node_visible(child);
            child->setHidden(!vis);
            if (vis) any = true;
        }
        return any;
    };
    for (int i = 0; i < exif_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* node = exif_tree->topLevelItem(i);
        node->setHidden(!node_visible(node));
    }
}

// M2-T7 #29：XMP 页搜索（与 apply_exif_filter 同构：命中可见 / 非命中隐藏 /
// 组内无命中 → 组隐藏；空搜索串 → 全部恢复）。XMP 树的顶层项即命名空间分组。
void ExifEditor::Impl::apply_xmp_filter() {
    const QString needle = xmp_search->text().trimmed();
    for (auto& [key, row] : rows) {
        (void)key;
        if (!row.is_xmp || !row.item) continue;
        row.item->setHidden(!needle.isEmpty() &&
                            !row.item->text(0).contains(needle, Qt::CaseInsensitive));
    }
    std::function<bool(QTreeWidgetItem*)> node_visible = [&](QTreeWidgetItem* node) -> bool {
        bool any = false;
        for (int i = 0; i < node->childCount(); ++i) {
            QTreeWidgetItem* child = node->child(i);
            const bool vis = (child->data(0, kNodeRole).toInt() == kLeaf) ? !child->isHidden()
                                                                          : node_visible(child);
            child->setHidden(!vis);
            if (vis) any = true;
        }
        return any;
    };
    for (int i = 0; i < xmp_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* node = xmp_tree->topLevelItem(i);
        node->setHidden(!node_visible(node));
    }
}

// —— 按编号 / 路径添加 ——

Exiv2::TypeId ExifEditor::Impl::type_from_combo() const {
    return static_cast<Exiv2::TypeId>(exif_type->currentData().toInt());
}

void ExifEditor::Impl::add_exif_by_number() {
    QString text = exif_number->text().trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) text = text.mid(2);
    bool ok = false;
    const uint tag = text.toUShort(&ok, 16);
    if (text.isEmpty() || !ok) {
        warn(T("编号无效"), T("请输入十六进制编号，例如 0x0132"));
        return;
    }
    const std::string group = exif_group->currentData().toString().toStdString();
    std::string key;
    try {
        const Exiv2::ExifKey ek(static_cast<uint16_t>(tag), group);
        key = ek.key();
    } catch (const std::exception& e) {
        warn(T("编号无效"), T("无法用该分组与编号构造 Exiv2 标签：") + QString::fromUtf8(e.what()));
        return;
    }
    ensure_row(key, false, type_from_combo());
    auto it = rows.find(key);
    if (it != rows.end() && it->second.item) {
        exif_tree->setCurrentItem(it->second.item);
        exif_tree->scrollToItem(it->second.item);
    }
    exif_number->clear();
}

void ExifEditor::Impl::add_xmp_by_path() {
    std::string key = xmp_path->text().trimmed().toStdString();
    const std::size_t colon = key.find(':', 4);
    if (key.rfind("Xmp.", 0) == 0 && colon != std::string::npos) key[colon] = '.';
    if (key.rfind("Xmp.", 0) != 0 || key.find('.', 4) == std::string::npos) {
        warn(T("路径无效"), T("请输入形如 Xmp.dc.title 的完整路径"));
        return;
    }
    try {
        const Exiv2::XmpKey xk(key);
        (void)xk;
    } catch (const std::exception& e) {
        warn(T("路径无效"), T("无法识别该 XMP 路径：") + QString::fromUtf8(e.what()));
        return;
    }
    ensure_row(key, true, Exiv2::xmpText);
    auto it = rows.find(key);
    if (it != rows.end() && it->second.item) {
        xmp_tree->setCurrentItem(it->second.item);
        xmp_tree->scrollToItem(it->second.item);
    }
    xmp_path->clear();
}

// —— 页签构造 ——

QWidget* ExifEditor::Impl::make_exif_tab() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);

    auto* split = new QSplitter(Qt::Horizontal);
    auto* left = new QWidget();
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    exif_search = new QLineEdit();
    exif_search->setObjectName(QStringLiteral("exif_search"));
    exif_search->setPlaceholderText(T("搜索标签名 / 键 / 值"));
    exif_search->setClearButtonEnabled(true);
    lv->addWidget(exif_search);
    exif_tree = new QTreeWidget();
    exif_tree->setObjectName(QStringLiteral("exif_tree"));
    exif_tree->setHeaderLabels(QStringList{T("标签")});
    exif_tree->setUniformRowHeights(true);
    exif_tree->setAlternatingRowColors(true);
    exif_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    lv->addWidget(exif_tree, 1);
    split->addWidget(left);

    exif_pane.is_xmp = false;
    split->addWidget(make_value_pane(exif_pane, QStringLiteral("exif")));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto* add = new QHBoxLayout();
    add->addWidget(new QLabel(T("分组")));
    exif_group = new QComboBox();
    exif_group->setObjectName(QStringLiteral("add_exif_group"));
    exif_group->addItem(T("IFD0"), QStringLiteral("Image"));
    exif_group->addItem(T("Exif"), QStringLiteral("Photo"));
    exif_group->addItem(T("GPS"), QStringLiteral("GPSInfo"));
    add->addWidget(exif_group);
    add->addWidget(new QLabel(T("编号 (hex)")));
    exif_number = new QLineEdit();
    exif_number->setObjectName(QStringLiteral("add_exif_number"));
    exif_number->setPlaceholderText(QStringLiteral("0x0132"));
    exif_number->setMaximumWidth(110);
    add->addWidget(exif_number);
    add->addWidget(new QLabel(T("类型")));
    exif_type = new QComboBox();
    exif_type->setObjectName(QStringLiteral("add_exif_type"));
    exif_type->addItem(QStringLiteral("Byte"), static_cast<int>(Exiv2::unsignedByte));
    exif_type->addItem(QStringLiteral("Ascii"), static_cast<int>(Exiv2::asciiString));
    exif_type->addItem(QStringLiteral("Short"), static_cast<int>(Exiv2::unsignedShort));
    exif_type->addItem(QStringLiteral("Long"), static_cast<int>(Exiv2::unsignedLong));
    exif_type->addItem(QStringLiteral("Rational"), static_cast<int>(Exiv2::unsignedRational));
    exif_type->addItem(QStringLiteral("SRational"), static_cast<int>(Exiv2::signedRational));
    exif_type->addItem(QStringLiteral("Undefined"), static_cast<int>(Exiv2::undefined));
    exif_type->setCurrentIndex(1);  // Ascii
    add->addWidget(exif_type);
    auto* add_button = new QPushButton(T("添加标签"));
    add_button->setObjectName(QStringLiteral("add_exif_button"));
    add->addWidget(add_button);
    add->addStretch(1);
    v->addLayout(add);

    QObject::connect(add_button, &QPushButton::clicked, q, [this] { add_exif_by_number(); });
    QObject::connect(exif_number, &QLineEdit::returnPressed, q, [this] { add_exif_by_number(); });
    // 选中 → 右值区：挂 selectionModel 的 currentChanged（用户点击与程序化
    // setCurrentItem/setCurrentIndex 都会同步；currentItemChanged 覆盖不到后者）
    QObject::connect(exif_tree->selectionModel(), &QItemSelectionModel::currentChanged, q,
                     [this](const QModelIndex& cur, const QModelIndex&) {
                         on_index_selection(exif_pane, cur);
                     });
    QObject::connect(exif_search, &QLineEdit::textChanged, q,
                     [this](const QString&) { apply_exif_filter(); });
    return page;
}

QWidget* ExifEditor::Impl::make_xmp_tab() {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);

    auto* split = new QSplitter(Qt::Horizontal);
    auto* left = new QWidget();
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    // M2-T7 #29：XMP 页搜索框（与 EXIF 页 exif_search 同构；objectName `xmp_search`）
    xmp_search = new QLineEdit();
    xmp_search->setObjectName(QStringLiteral("xmp_search"));
    xmp_search->setPlaceholderText(T("搜索标签名 / 键 / 值"));
    xmp_search->setClearButtonEnabled(true);
    lv->addWidget(xmp_search);
    xmp_tree = new QTreeWidget();
    xmp_tree->setObjectName(QStringLiteral("xmp_tree"));
    xmp_tree->setHeaderLabels(QStringList{T("属性")});
    xmp_tree->setUniformRowHeights(true);
    xmp_tree->setAlternatingRowColors(true);
    xmp_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    lv->addWidget(xmp_tree, 1);
    split->addWidget(left);

    xmp_pane.is_xmp = true;
    split->addWidget(make_value_pane(xmp_pane, QStringLiteral("xmp")));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto* add = new QHBoxLayout();
    xmp_path = new QLineEdit();
    xmp_path->setObjectName(QStringLiteral("add_xmp_path"));
    xmp_path->setPlaceholderText(T("Xmp.xxx.yyy 自定义路径"));
    add->addWidget(xmp_path, 1);
    auto* add_button = new QPushButton(T("添加标签"));
    add_button->setObjectName(QStringLiteral("add_xmp_button"));
    add->addWidget(add_button);
    v->addLayout(add);

    QObject::connect(add_button, &QPushButton::clicked, q, [this] { add_xmp_by_path(); });
    QObject::connect(xmp_path, &QLineEdit::returnPressed, q, [this] { add_xmp_by_path(); });
    // M2-T7 #29：XMP 搜索过滤（与 exif_search 同构）
    QObject::connect(xmp_search, &QLineEdit::textChanged, q,
                     [this](const QString&) { apply_xmp_filter(); });
    // 同 EXIF 页：selectionModel::currentChanged（含程序化 setCurrentIndex）
    QObject::connect(xmp_tree->selectionModel(), &QItemSelectionModel::currentChanged, q,
                     [this](const QModelIndex& cur, const QModelIndex&) {
                         on_index_selection(xmp_pane, cur);
                     });
    return page;
}

void ExifEditor::Impl::update_time_mode() {
    const int mode = time_mode->currentIndex();
    time_params->setEnabled(mode == 1);
    const bool delta = time_method->currentIndex() == 0;
    time_delta_box->setVisible(delta);
    time_tz_box->setVisible(!delta);
}

pp::TimeShift ExifEditor::Impl::time_shift_from_widgets() const {
    pp::TimeShift ts;
    if (time_method->currentIndex() == 1) {
        ts.mode = pp::TimeShift::Mode::TimezoneSemantic;
        ts.from_offset_min = tz_from->currentData().toInt();
        ts.to_offset_min = tz_to->currentData().toInt();
        return ts;
    }
    ts.years = time_spin[0]->value();
    ts.months = time_spin[1]->value();
    ts.days = time_spin[2]->value();
    ts.hours = time_spin[3]->value();
    ts.minutes = time_spin[4]->value();
    ts.seconds = time_spin[5]->value();
    return ts;
}

void ExifEditor::Impl::update_gps_mode() {
    gps_params->setEnabled(gps_mode->currentIndex() == 1);
}

void ExifEditor::Impl::update_dms() {
    bool ok_lat = false, ok_lon = false;
    const double lat = gps_lat->text().trimmed().toDouble(&ok_lat);
    const double lon = gps_lon->text().trimmed().toDouble(&ok_lon);
    if (ok_lat && ok_lon && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
        gps_dms->setText(dms_component(lat, true) + QStringLiteral(" ") + dms_component(lon, false));
    } else {
        gps_dms->setText(QStringLiteral("—"));
    }
}

QWidget* ExifEditor::Impl::make_time_gps_tab() {
    auto* page = new QWidget();
    auto* outer = new QVBoxLayout(page);
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget();
    auto* v = new QVBoxLayout(inner);

    auto* hint = new QLabel(T("以下三态只作用于当前文件的例外：继承批量规则 / 覆盖 / 清除。"));
    hint->setWordWrap(true);
    QPalette hint_pal = hint->palette();
    hint_pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    hint->setPalette(hint_pal);
    v->addWidget(hint);

    // 1) 时间三态
    auto* time_group = new QGroupBox(T("时间"));
    auto* tv = new QVBoxLayout(time_group);
    auto* time_row = new QHBoxLayout();
    time_row->addWidget(new QLabel(T("时间偏移")));
    time_mode = new QComboBox();
    time_mode->setObjectName(QStringLiteral("time_mode"));
    time_mode->addItem(T("继承批量规则"));
    time_mode->addItem(T("覆盖"));
    time_mode->addItem(T("清除"));
    time_row->addWidget(time_mode);
    time_row->addStretch(1);
    tv->addLayout(time_row);

    time_params = new QWidget();
    auto* tp = new QVBoxLayout(time_params);
    tp->setContentsMargins(0, 0, 0, 0);
    auto* method_row = new QHBoxLayout();
    method_row->addWidget(new QLabel(T("方式")));
    time_method = new QComboBox();
    time_method->setObjectName(QStringLiteral("time_method"));
    time_method->addItem(T("Δ 偏移（时钟拨错）"));
    time_method->addItem(T("时区语义（旅行照片）"));
    method_row->addWidget(time_method);
    method_row->addStretch(1);
    tp->addLayout(method_row);

    time_delta_box = new QWidget();
    auto* grid = new QGridLayout(time_delta_box);
    grid->setContentsMargins(0, 0, 0, 0);
    const QString labels[6] = {T("年"), T("月"), T("日"), T("时"), T("分"), T("秒")};
    const char* names[6] = {"time_years", "time_months", "time_days",
                            "time_hours", "time_minutes", "time_seconds"};
    for (int i = 0; i < 6; ++i) {
        grid->addWidget(new QLabel(labels[i]), 0, i);
        time_spin[i] = new QSpinBox();
        time_spin[i]->setObjectName(QString::fromLatin1(names[i]));
        time_spin[i]->setRange(-9999, 9999);
        grid->addWidget(time_spin[i], 1, i);
    }
    tp->addWidget(time_delta_box);

    time_tz_box = new QWidget();
    auto* tz = new QHBoxLayout(time_tz_box);
    tz->setContentsMargins(0, 0, 0, 0);
    tz_from = new QComboBox();
    tz_from->setObjectName(QStringLiteral("tz_from"));
    tz_to = new QComboBox();
    tz_to->setObjectName(QStringLiteral("tz_to"));
    for (int m = -12 * 60; m <= 14 * 60; m += 30) {  // 整点 + 半小时时区全表
        tz_from->addItem(offset_label(m), m);
        tz_to->addItem(offset_label(m), m);
    }
    const int zero_from = tz_from->findData(0);
    if (zero_from >= 0) tz_from->setCurrentIndex(zero_from);
    const int zero_to = tz_to->findData(0);
    if (zero_to >= 0) tz_to->setCurrentIndex(zero_to);
    tz->addWidget(new QLabel(T("从")));
    tz->addWidget(tz_from);
    tz->addWidget(new QLabel(T("到")));
    tz->addWidget(tz_to);
    tz->addStretch(1);
    tp->addWidget(time_tz_box);
    tv->addWidget(time_params);
    v->addWidget(time_group);

    // 2) GPS 三态
    auto* gps_group = new QGroupBox(T("GPS"));
    auto* gv = new QVBoxLayout(gps_group);
    auto* gps_row = new QHBoxLayout();
    gps_row->addWidget(new QLabel(T("GPS")));
    gps_mode = new QComboBox();
    gps_mode->setObjectName(QStringLiteral("gps_mode"));
    gps_mode->addItem(T("继承批量规则"));
    gps_mode->addItem(T("覆盖"));
    gps_mode->addItem(T("清除 GPS"));
    gps_row->addWidget(gps_mode);
    gps_row->addStretch(1);
    gv->addLayout(gps_row);

    gps_params = new QWidget();
    auto* gp = new QVBoxLayout(gps_params);
    gp->setContentsMargins(0, 0, 0, 0);
    auto* coord = new QFormLayout();
    gps_lat = new QLineEdit();
    gps_lat->setObjectName(QStringLiteral("gps_lat"));
    gps_lat->setPlaceholderText(QStringLiteral("31.230400"));
    auto* lat_validator = new QDoubleValidator(-90.0, 90.0, 6, gps_lat);
    lat_validator->setNotation(QDoubleValidator::StandardNotation);
    lat_validator->setLocale(QLocale::c());
    gps_lat->setValidator(lat_validator);
    gps_lon = new QLineEdit();
    gps_lon->setObjectName(QStringLiteral("gps_lon"));
    gps_lon->setPlaceholderText(QStringLiteral("121.473700"));
    auto* lon_validator = new QDoubleValidator(-180.0, 180.0, 6, gps_lon);
    lon_validator->setNotation(QDoubleValidator::StandardNotation);
    lon_validator->setLocale(QLocale::c());
    gps_lon->setValidator(lon_validator);
    coord->addRow(T("纬度"), gps_lat);
    coord->addRow(T("经度"), gps_lon);
    gps_dms = new QLabel(QStringLiteral("—"));
    gps_dms->setObjectName(QStringLiteral("gps_dms"));
    QPalette dms_pal = gps_dms->palette();
    dms_pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    gps_dms->setPalette(dms_pal);
    coord->addRow(T("DMS"), gps_dms);
    gp->addLayout(coord);

    gps_more = new QGroupBox(T("更多字段"));
    gps_more->setObjectName(QStringLiteral("gps_more"));
    gps_more->setCheckable(true);
    gps_more->setChecked(false);
    gps_more->setToolTip(T("收起时不写入可选字段"));
    auto* more_form = new QFormLayout(gps_more);
    gps_alt = new QLineEdit();
    gps_alt->setObjectName(QStringLiteral("gps_alt"));
    gps_dir = new QLineEdit();
    gps_dir->setObjectName(QStringLiteral("gps_dir"));
    gps_ts = new QLineEdit();
    gps_ts->setObjectName(QStringLiteral("gps_ts"));
    gps_ts->setPlaceholderText(QStringLiteral("YYYY:MM:DD HH:MM:SS"));
    more_form->addRow(T("海拔 (m)"), gps_alt);
    more_form->addRow(T("方位角 (°)"), gps_dir);
    more_form->addRow(T("时间戳"), gps_ts);
    gp->addWidget(gps_more);
    gv->addWidget(gps_params);
    v->addWidget(gps_group);

    // 3) 隐私剥除三态
    auto* privacy_group = new QGroupBox(T("隐私剥除"));
    auto* pv = new QVBoxLayout(privacy_group);
    auto* privacy_row = new QHBoxLayout();
    privacy_row->addWidget(new QLabel(T("剥除全部 EXIF / XMP（保留 ICC 与像素）")));
    privacy_mode = new QComboBox();
    privacy_mode->setObjectName(QStringLiteral("privacy_mode"));
    privacy_mode->addItem(T("继承批量规则"));
    privacy_mode->addItem(T("强制开"));
    privacy_mode->addItem(T("强制关"));
    privacy_row->addWidget(privacy_mode);
    privacy_row->addStretch(1);
    pv->addLayout(privacy_row);
    auto* priority = new QLabel(T("优先级最高的规则"));
    QPalette priority_pal = priority->palette();
    priority_pal.setColor(QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    priority->setPalette(priority_pal);
    pv->addWidget(priority);
    v->addWidget(privacy_group);
    v->addStretch(1);

    scroll->setWidget(inner);
    outer->addWidget(scroll);

    QObject::connect(time_mode, &QComboBox::currentIndexChanged, q,
                     [this](int) { update_time_mode(); });
    QObject::connect(time_method, &QComboBox::currentIndexChanged, q,
                     [this](int) { update_time_mode(); });
    QObject::connect(gps_mode, &QComboBox::currentIndexChanged, q,
                     [this](int) { update_gps_mode(); });
    QObject::connect(gps_lat, &QLineEdit::textChanged, q, [this](const QString&) { update_dms(); });
    QObject::connect(gps_lon, &QLineEdit::textChanged, q, [this](const QString&) { update_dms(); });

    update_time_mode();
    update_gps_mode();
    update_dms();
    return page;
}

// —— 顶部与收尾 ——

void ExifEditor::Impl::build() {
    q->setWindowTitle(T("元数据编辑器 — ") + QFileInfo(src).fileName());
    q->resize(900, 640);
    q->setModal(true);

    auto* root = new QVBoxLayout(q);
    auto* top = new QHBoxLayout();
    auto* name = new QLabel(QFileInfo(src).fileName());
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    name->setToolTip(src);
    top->addWidget(name);
    top->addStretch(1);
    ignore_check = new QCheckBox(T("忽略全部批量规则"));
    ignore_check->setObjectName(QStringLiteral("ignore_batch"));
    ignore_check->setToolTip(T("勾选后本文件不使用任何批量规则，只应用下面的例外"));
    ignore_check->setChecked(existing.has_value() && existing->ignore_batch);
    top->addWidget(ignore_check);
    root->addLayout(top);

    if (!meta.error.empty()) {
        auto* err = new QLabel(T("无法读取源文件元数据：") + QString::fromStdString(meta.error));
        QPalette err_pal = err->palette();
        err_pal.setColor(QPalette::WindowText, QColor(0xdd, 0x33, 0x33));
        err->setPalette(err_pal);
        err->setWordWrap(true);
        root->addWidget(err);
    }

    tabs = new QTabWidget();
    tabs->setObjectName(QStringLiteral("tabs"));
    tabs->addTab(make_exif_tab(), T("EXIF"));
    tabs->addTab(make_xmp_tab(), T("XMP"));
    tabs->addTab(make_time_gps_tab(), T("时间 / GPS"));
    root->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setObjectName(QStringLiteral("buttons"));
    buttons->button(QDialogButtonBox::Ok)->setText(T("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(T("取消"));
    root->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, q, [this] { on_accept(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, q, [this] { q->reject(); });
    QObject::connect(ignore_check, &QCheckBox::toggled, q,
                     [this](bool) { refresh_all_rows(); });
}

void ExifEditor::Impl::init_from_existing() {
    if (!existing.has_value()) return;
    const pp::MetadataOverride& ex = *existing;

    ignore_check->setChecked(ex.ignore_batch);
    exif_edits = ex.exif_edits;
    xmp_edits = ex.xmp_edits;
    for (const pp::TagEdit& e : exif_edits) ensure_row(e.key, false, Exiv2::asciiString);
    for (const pp::TagEdit& e : xmp_edits) ensure_row(e.key, true, Exiv2::xmpText);

    if (ex.time_shift.has_value()) {
        time_mode->setCurrentIndex(1);
        const pp::TimeShift& ts = *ex.time_shift;
        if (ts.mode == pp::TimeShift::Mode::TimezoneSemantic) {
            time_method->setCurrentIndex(1);
            const int i1 = tz_from->findData(ts.from_offset_min);
            if (i1 >= 0) tz_from->setCurrentIndex(i1);
            const int i2 = tz_to->findData(ts.to_offset_min);
            if (i2 >= 0) tz_to->setCurrentIndex(i2);
        } else {
            time_method->setCurrentIndex(0);
            time_spin[0]->setValue(ts.years);
            time_spin[1]->setValue(ts.months);
            time_spin[2]->setValue(ts.days);
            time_spin[3]->setValue(ts.hours);
            time_spin[4]->setValue(ts.minutes);
            time_spin[5]->setValue(ts.seconds);
        }
    }

    if (ex.gps_clear) {
        gps_mode->setCurrentIndex(2);
    } else if (ex.gps.has_value()) {
        gps_mode->setCurrentIndex(1);
        const pp::GpsData& g = *ex.gps;
        gps_lat->setText(QString::number(g.lat, 'f', 6));
        gps_lon->setText(QString::number(g.lon, 'f', 6));
        if (g.altitude.has_value()) {
            gps_more->setChecked(true);
            gps_alt->setText(QString::number(*g.altitude, 'f', 3));
        }
        if (g.direction.has_value()) {
            gps_more->setChecked(true);
            gps_dir->setText(QString::number(*g.direction, 'f', 2));
        }
        if (g.timestamp.has_value()) {
            gps_more->setChecked(true);
            gps_ts->setText(QString::fromStdString(*g.timestamp));
        }
    }

    if (ex.strip_privacy.has_value()) privacy_mode->setCurrentIndex(*ex.strip_privacy ? 1 : 2);

    update_dms();
    refresh_all_rows();
}

// —— 校验 / 结果 ——

void ExifEditor::Impl::report_errors(const std::vector<std::string>& errors) {
    QStringList list;
    for (const std::string& e : errors) list << QString::fromStdString(e);
    if (q->property(kSuppressModal).toBool()) {
        q->setProperty(kLastErrors, list);
        return;
    }
    QMessageBox box(q);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(T("无法应用元数据编辑"));
    box.setText(T("以下编辑无法应用，请修正后再点确定："));
    box.setInformativeText(list.join(QChar('\n')));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void ExifEditor::Impl::warn(const QString& title, const QString& text) {
    if (q->property(kSuppressModal).toBool()) {
        q->setProperty(kLastWarning, text);
        return;
    }
    QMessageBox::warning(q, title, text);
}

pp::MetadataOverride ExifEditor::Impl::build_override(std::vector<std::string>* errors) const {
    pp::MetadataOverride ov;
    ov.ignore_batch = ignore_check->isChecked();
    ov.exif_edits = exif_edits;
    ov.xmp_edits = xmp_edits;

    // 时间三态：覆盖 = time_shift；清除 = 对时间标签的 remove 编辑（MetadataOverride 无 time_clear）
    const int tmode = time_mode->currentIndex();
    if (tmode == 1) {
        ov.time_shift = time_shift_from_widgets();
    } else if (tmode == 2) {
        for (const char* k : kExifTimeKeys) {
            pp::TagEdit e;
            e.key = k;
            e.remove = true;
            auto it = std::find_if(ov.exif_edits.begin(), ov.exif_edits.end(),
                                   [&](const pp::TagEdit& x) { return x.key == e.key; });
            if (it != ov.exif_edits.end()) {
                *it = e;
            } else {
                ov.exif_edits.push_back(e);
            }
        }
        for (const char* k : kXmpTimeKeys) {
            pp::TagEdit e;
            e.key = k;
            e.remove = true;
            auto it = std::find_if(ov.xmp_edits.begin(), ov.xmp_edits.end(),
                                   [&](const pp::TagEdit& x) { return x.key == e.key; });
            if (it != ov.xmp_edits.end()) {
                *it = e;
            } else {
                ov.xmp_edits.push_back(e);
            }
        }
    }

    // GPS 三态：覆盖（校验经纬度与可选字段）/ 清除
    const int gmode = gps_mode->currentIndex();
    if (gmode == 2) {
        ov.gps_clear = true;
    } else if (gmode == 1) {
        bool ok_lat = false, ok_lon = false;
        const double lat = gps_lat->text().trimmed().toDouble(&ok_lat);
        const double lon = gps_lon->text().trimmed().toDouble(&ok_lon);
        const bool lat_ok = ok_lat && lat >= -90.0 && lat <= 90.0;
        const bool lon_ok = ok_lon && lon >= -180.0 && lon <= 180.0;
        if (!lat_ok && errors) errors->push_back("GPS 纬度无效（需为 -90..90 的十进制数）");
        if (!lon_ok && errors) errors->push_back("GPS 经度无效（需为 -180..180 的十进制数）");
        if (lat_ok && lon_ok) {
            pp::GpsData g;
            g.lat = lat;
            g.lon = lon;
            if (gps_more->isChecked()) {
                const QString alt = gps_alt->text().trimmed();
                if (!alt.isEmpty()) {
                    bool ok = false;
                    const double v = alt.toDouble(&ok);
                    if (!ok) {
                        if (errors) errors->push_back("GPS 海拔必须是数字");
                    } else {
                        g.altitude = v;
                    }
                }
                const QString dir = gps_dir->text().trimmed();
                if (!dir.isEmpty()) {
                    bool ok = false;
                    const double v = dir.toDouble(&ok);
                    if (!ok || v < 0.0 || v > 359.99) {
                        if (errors) errors->push_back("GPS 方位角需为 0–359.99 的数字");
                    } else {
                        g.direction = v;
                    }
                }
                const QString ts = gps_ts->text().trimmed();
                if (!ts.isEmpty()) {
                    static const QRegularExpression re(
                        QStringLiteral("^\\d{4}:\\d{2}:\\d{2} \\d{2}:\\d{2}:\\d{2}$"));
                    if (!re.match(ts).hasMatch()) {
                        if (errors) errors->push_back("GPS 时间戳格式应为 YYYY:MM:DD HH:MM:SS");
                    } else {
                        g.timestamp = ts.toStdString();
                    }
                }
            }
            ov.gps = g;
        }
    }

    switch (privacy_mode->currentIndex()) {
        case 1:
            ov.strip_privacy = true;
            break;
        case 2:
            ov.strip_privacy = false;
            break;
        default:
            break;
    }
    return ov;
}

void ExifEditor::Impl::on_accept() {
    std::vector<std::string> errors;
    const pp::MetadataOverride ov = build_override(&errors);
    // 校验只作用于 read_metadata 的副本；源文件与源元数据永不修改
    Exiv2::ExifData exif = meta.exif;
    Exiv2::XmpData xmp = meta.xmp;
    pp::apply_edits(exif, xmp, ov.exif_edits, ov.xmp_edits, errors);
    if (!errors.empty()) {
        report_errors(errors);
        return;  // 保持对话框打开
    }
    q->accept();
}

// ===========================================================================
// ExifEditor
// ===========================================================================

ExifEditor::ExifEditor(const QString& src, const pp::BatchRules& batch,
                       const std::optional<pp::MetadataOverride>& existing, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>()) {
    impl_->q = this;
    impl_->src = src;
    impl_->batch = batch;
    impl_->existing = existing;
    impl_->meta = pp::read_metadata(std::filesystem::path(src.toStdString()));

    impl_->build();
    impl_->populate_exif_tree();
    impl_->populate_xmp_tree();
    impl_->init_from_existing();
}

ExifEditor::~ExifEditor() = default;

pp::MetadataOverride ExifEditor::result() const {
    std::vector<std::string> ignored;
    return impl_->build_override(&ignored);
}

}  // namespace pp::ui
