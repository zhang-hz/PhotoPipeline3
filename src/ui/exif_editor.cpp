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
//     M5-T10（A8）：行首不再有「〔批〕」字形 —— 改为行内**中性 pill「批」**（delegate 画 16px
//     胶囊，色 = theme::pill_paint(kPillNeutral)）。
//   * 已改值项：M5-T10（A8）不再用 "● " 前缀 —— 改值 = **强调色字 + 700**（行内 .mod 等价；
//     树行没有边框可画，故取"强调边框+强调色字"的字色一档）。
//   * 值留空 = 删除该标签（remove 编辑）。
//   * 时间三态“清除”：MetadataOverride 没有对应字段，语义化为对时间标签的 remove 编辑
//     （键集 = ui/time_tag_keys.h 的 wall_clock 5 键，与引擎写路径同集；命中〔批〕的
//     touched 9 键见同表 —— R-197 4 份真相单表收口）；GPS“清除”= gps_clear；
//     隐私剥除 = optional<bool>。
//   * 自验开关（不改冻结头）：动态属性 "pp_exif_editor_suppress_modal" = true 时，所有
//     QMessageBox 改为把内容写进动态属性（"pp_last_validation_errors" / "pp_last_warning"），
//     offscreen 自验程序据此断言“校验失败不 accept”，不会阻塞在模态框上。
//   * 对象名（objectName）供 --ui-smoke / .cache/tmp 自验程序查找控件。
//   * M2-T7 #29：XMP 页增设搜索框（objectName `xmp_search`），过滤逻辑与 EXIF 页
//     `exif_search` 同构（命中可见 / 非命中隐藏 / 组内无命中 → 组隐藏 / 清空恢复）。
#include "ui/exif_editor.h"

// theme.h 依赖包含者先引入 <QApplication>（qApp->style()；见 ui/classify_panel.h 同注）
#include <QApplication>

#include "mapwidget/dms.h" // R-099：DMS 文案单源 pp::map::deg_to_dms（M7-R2D2）
#include "ui/theme.h" // M4-T12：.mod 样式（theme::set_mod / mod_qss）；M5-T10：令牌 + dialogkit

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher> // R-081：read_metadata 后台化
#include <QtConcurrent/QtConcurrentRun> // R-081：read_metadata IO 下后台
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyledItemDelegate>
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

#include "platform/frameless.h" // M5-T10：无边框接线（与主窗/设置弹窗同一机制）
#include "ui/formkit.h"         // M5-T10：勾选框（忽略批量规则）走 formkit 单实现
#include "ui/settings_dialog.h" // M5-T10：dialogkit（标题栏/页签样式/按钮/焦点环/QSS）
#include "ui/time_tag_keys.h"   // R-197（M7-R2.1-F3）：时间标签集合单表（清除/命中双角色）

namespace pp::ui {
namespace {

// role 值单源 = exif_editor.h 的 exif_roles（探针按同一对值读回）
constexpr int kKeyRole = exif_roles::key;   // QString: full Exiv2 key
constexpr int kNodeRole = exif_roles::node; // int: 0 = tag leaf, 1 = group node
// M5-T10：行级标记（delegate 用；也是探针读回面 —— A8 零字形两条）。role 值单源 = exif_editor.h
constexpr int kBatchRole = exif_roles::batch;     // bool: 批量规则会命中（画中性 pill「批」）
constexpr int kChangedRole = exif_roles::changed; // bool: 该行已改值（强调色字 + 700）
constexpr int kLeaf = 0;
constexpr int kGroupNode = 1;
constexpr int kTreeValueMax = 120;      // §2.12 树内只展示值的前若干字符（完整值在右侧编辑区）
constexpr int kMultilineThreshold = 60; // §2.12：长 ASCII > 60 → QPlainTextEdit

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」；与 ui/mainwindow.cpp、
// ui/page_meta.cpp 的 ElidedLabel 同口径）：本编辑器原先有 4 处开了**自动折行**
// （值区标题/说明/提示/读取错误），M4-T12 一并清零 → 单行 + 省略号 + 悬浮全文。
// （M4-T15 收尾自证：全仓 `setWordWrap(false)` 之外再无开启折行的调用点。）
// R-020/R-320（M7-WP4）：ElidedLabel 的 7 份逐字拷贝收敛到 formkit 唯一实现
// （本文件原 30+ 行自拷类删除；行为逐位等价 —— 全文 + ppFullText + tooltip + 省略号）。
using ElidedLabel = formkit::ElidedLabel;

// 弹窗文案取词（ExifEditor 上下文；dialogkit/本文件的辅助件共用）
QString T(const char *s) { return ExifEditor::tr(s); }

const char *const kSuppressModal = "pp_exif_editor_suppress_modal";
const char *const kLastErrors = "pp_last_validation_errors";
const char *const kLastWarning = "pp_last_warning";

// 树行 delegate（M5-T10 / A8 零字形两条）：行首画**中性 pill「批」**（批量规则命中），
// 改值行用**强调色字 + 700**（树行无边框可画 → 取 .mod 的字色/字重一档）。行文本本身不再
// 含任何标记字形（〔批〕/●）；两个标记同时以 item data 存回（探针读回面）。
class TagItemDelegate : public QStyledItemDelegate {
public:
    explicit TagItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void set_tokens(const theme::Tokens &t) { tokens_ = t; }
    const theme::Tokens &tokens() const { return tokens_; }
    int pill_width() const {
        const QFontMetrics fm(theme::font(theme::Typography::badge_px, QFont::Bold));
        return fm.horizontalAdvance(T("批")) + 2 * theme::Metrics::pill_pad_x + 2;
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QSize base = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(kBatchRole).toBool())
            base.setWidth(base.width() + pill_width() + kPillGap);
        return base;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString text = opt.text;
        const bool batch = index.data(kBatchRole).toBool();
        const bool changed = index.data(kChangedRole).toBool();
        opt.text.clear(); // 文本自绘（底/选中/缩进仍走基类）
        QStyledItemDelegate::paint(painter, opt, index);

        // R-218（M7-R2E2）：行内几何走逻辑坐标（pill 贴 leading）+ 单点镜像；LTR 逐字节不变。
        const bool rtl = opt.direction == Qt::RightToLeft;
        const auto mirror_x = [&opt](int x, int w) {
            return opt.rect.left() + opt.rect.right() - (x + w - 1);
        };
        int left = opt.rect.left() + 2;
        if (batch) { // 中性 pill「批」（R4 高 16 r3 + 9.5px/700；色 = 中性档）
            const theme::PillPaint paint = theme::pill_paint(tokens_, theme::kPillNeutral);
            const int w = pill_width();
            QRectF box(left, opt.rect.center().y() - theme::Metrics::pill_h / 2.0 + 0.5, w,
                       theme::Metrics::pill_h);
            if (rtl)
                box.moveLeft(mirror_x(left, w));
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(paint.bd, 1.0));
            painter->setBrush(paint.bg);
            painter->drawRoundedRect(box, theme::Metrics::pill_radius, theme::Metrics::pill_radius);
            painter->setFont(theme::font(theme::Typography::badge_px, QFont::Bold));
            painter->setPen(paint.fg);
            painter->drawText(box, Qt::AlignCenter, T("批"));
            painter->restore();
            left += w + kPillGap;
        }
        QFont font = opt.font;
        font.setWeight(changed ? QFont::Bold : QFont::Normal);
        painter->save();
        painter->setFont(font);
        painter->setPen(changed ? tokens_.accent : tokens_.text);
        const int tw = std::max(0, opt.rect.right() - left);
        QRect text_rect(left, opt.rect.top(), tw, opt.rect.height());
        if (rtl)
            text_rect.moveLeft(mirror_x(left, tw));
        painter->drawText(text_rect, (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(text, rtl ? Qt::ElideLeft : Qt::ElideRight,
                                                        text_rect.width()));
        painter->restore();
    }

private:
    static constexpr int kPillGap = 6; // pill ↔ 文字
    theme::Tokens tokens_ = theme::tokens(theme::preferred_theme_mode());
};

// R-197（M7-R2.1-F3）：时间标签集合 4 份真相收敛为 ui/time_tag_keys.h 单表（清除集 =
// wall_clock 5 键、命中集 = touched 9 键，逐键角色显式）；原 kExifTimeKeys/kXmpTimeKeys 与
// is_exif_time_key/is_xmp_time_key 的字面量全部退役（清除 3+2 vs 命中 6+3 的静默漂移消除）。

// R-199（M7-R2.1-F3）：三态机/二态机组合框的**唯一真相 = itemData**（构造时 addItem 文本 +
// 枚举值，与 R-190 同一收口法）—— 读回 currentData()、落值 findData()；原
// currentIndex()==1/2 魔法索引全部退役（deep-review R-199/E5-09「调序即全对话框错位」）。
// M7R21F4-0：三枚举基型 int→uint8_t（tidy performance-enum-size，值域 0..2 适配；R-197/199
// 收敛新增的三个 file-local 枚举，读写面 static_cast<int> 照旧，行为零变化）。
enum class TriMode : std::uint8_t { Inherit = 0, Override = 1, Clear = 2 };       // 时间 / GPS 三态
enum class TimeMethod : std::uint8_t { Delta = 0, TimeZone = 1 };                 // 时间方式二态
enum class PrivacyMode : std::uint8_t { Inherit = 0, ForceOn = 1, ForceOff = 2 }; // 隐私剥除三态

void add_tri_items(QComboBox *cb, const QString &inherit, const QString &over,
                   const QString &clear) {
    cb->addItem(inherit, static_cast<int>(TriMode::Inherit));
    cb->addItem(over, static_cast<int>(TriMode::Override));
    cb->addItem(clear, static_cast<int>(TriMode::Clear));
}

void set_tri(QComboBox *cb, TriMode mode) {
    const int i = cb->findData(static_cast<int>(mode));
    if (i >= 0)
        cb->setCurrentIndex(i);
}

TriMode tri_of(const QComboBox *cb) {
    const QVariant v = cb->currentData();
    return v.isValid() ? static_cast<TriMode>(v.toInt()) : TriMode::Inherit;
}

void set_method(QComboBox *cb, TimeMethod m) {
    const int i = cb->findData(static_cast<int>(m));
    if (i >= 0)
        cb->setCurrentIndex(i);
}

TimeMethod method_of(const QComboBox *cb) {
    const QVariant v = cb->currentData();
    return v.isValid() ? static_cast<TimeMethod>(v.toInt()) : TimeMethod::Delta;
}

void set_privacy(QComboBox *cb, PrivacyMode m) {
    const int i = cb->findData(static_cast<int>(m));
    if (i >= 0)
        cb->setCurrentIndex(i);
}

PrivacyMode privacy_of(const QComboBox *cb) {
    const QVariant v = cb->currentData();
    return v.isValid() ? static_cast<PrivacyMode>(v.toInt()) : PrivacyMode::Inherit;
}

// 本文件下拉的唯一构造点（GUI-R2）：走 formkit::combo_box —— 钩子/高/箭头/字体/无障碍
// 一律单源（原 dress_combo 已随之退役）。调用点不得再 `new QComboBox`（formkit.h 契约 C2）。
QComboBox *make_combo(const QString &object_name, const QString &accessible,
                      const QString &tooltip = QString()) {
    formkit::ComboSpec spec;
    spec.object_name = object_name;
    spec.accessible = accessible;
    spec.tooltip = tooltip;
    return formkit::combo_box(nullptr, spec); // 父由后续 addWidget/setParent 落定
}

void dress_edit(QLineEdit *edit, const QString &accessible, const QString &tooltip) {
    if (edit == nullptr)
        return;
    edit->setFont(theme::font(theme::Typography::body_px));
    edit->setProperty(dialogkit::kFieldProperty, QString::fromLatin1(dialogkit::kFieldInput));
    if (!tooltip.isEmpty())
        edit->setToolTip(tooltip);
    if (!accessible.isEmpty())
        edit->setAccessibleName(accessible);
}

// 数值 spin（R4 高 24 / 值区右对齐 + tabular）。A8：↑↓ 操作提示只进 tooltip（不上界面）
void dress_spin(QSpinBox *spin, const QString &accessible) {
    if (spin == nullptr)
        return;
    spin->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    spin->setAlignment(Qt::AlignRight);
    spin->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkSpin));
    spin->setToolTip(T("↑↓ 调整数值（也可直接输入）"));
    spin->setAccessibleName(accessible);
}

// 按钮选择器钩子（dialogkit 词汇：button / primary —— 与 page_output 的 ppField 同源）
void hook_button(QPushButton *button, bool primary) {
    if (button != nullptr) {
        button->setProperty(
            dialogkit::kFieldProperty,
            QString::fromLatin1(primary ? dialogkit::kFieldPrimary : dialogkit::kFieldButton));
    }
}

// "Exif.Image.Artist" → "Artist"; "Xmp.dc.title" → "title"
std::string tag_name(const std::string &key) {
    const std::size_t dot = key.rfind('.');
    return dot == std::string::npos ? key : key.substr(dot + 1);
}

// "Exif.<group>.<tag>" / "Xmp.<prefix>.<prop>" → 第二段（EXIF 分组名 / XMP 命名空间前缀）
std::string key_second(const std::string &key) {
    const std::size_t first = key.find('.');
    if (first == std::string::npos)
        return {};
    const std::size_t second = key.find('.', first + 1);
    if (second == std::string::npos)
        return {};
    return key.substr(first + 1, second - first - 1);
}

std::string clean_value(std::string s) {
    while (!s.empty() &&
           (s.back() == '\0' || s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

// G15：LangAlt v1 只编辑 x-default。Exiv2 文档：LangAltValue::toString(0) 返回 x-default 文本。
std::string metadatum_text(const Exiv2::Metadatum &d) {
    std::string s;
    if (d.typeId() == Exiv2::langAlt) {
        s = d.toString(0);
        static const std::string kLangPrefix = "lang=\"x-default\" ";
        if (s.rfind(kLangPrefix, 0) == 0)
            s = s.substr(kLangPrefix.size());
    } else {
        s = d.toString();
    }
    return clean_value(std::move(s));
}

bool exif_std_group(const std::string &g) { return g == "Image" || g == "Photo" || g == "GPSInfo"; }

// MakerNote 归并组（非 Image/Photo/GPSInfo 的分组）与 MakerNote 二进制块都只读
bool exif_row_read_only(const std::string &key) {
    if (!exif_std_group(key_second(key)))
        return true;
    return tag_name(key).find("MakerNote") != std::string::npos;
}

bool is_user_comment(const std::string &key) {
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
// R-197：is_exif_time_key / is_xmp_time_key（命中集 = touched 9 键）收敛为
// ui/time_tag_keys.h 单表谓词（原两处字面量退役）。
using timetags::is_exif_time_key;
using timetags::is_xmp_time_key;

bool edits_hit(const std::vector<pp::TagEdit> &v, const std::string &key) {
    return std::any_of(v.begin(), v.end(), [&](const pp::TagEdit &e) { return e.key == key; });
}

const pp::BatchRules kNoRules{};

bool batch_affects(const pp::BatchRules &b, const std::string &key, bool is_xmp) {
    if (b.strip_privacy)
        return true; // strip_privacy 全部
    if (is_xmp) {
        if (edits_hit(b.xmp_edits, key))
            return true;
        return b.time_shift.has_value() && is_xmp_time_key(key);
    }
    if (edits_hit(b.exif_edits, key))
        return true;
    if (b.time_shift.has_value() && is_exif_time_key(key))
        return true;
    if ((b.gps.has_value() || b.gps_clear) && key_second(key) == "GPSInfo")
        return true;
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

// ---------------------------------------------------------------------------
// UX-6③（M7-R2D2）：行高 28 节奏（formkit::kRowPitch 单源）—— 行容器**逐行包壳**。
// 审计（docs/ux-review-14.md §6-A）：本文件此前零 setFixedHeight，行高由 24px 控件 +
// QGroupBox padding 自算，全文件不守 28 节奏（"每个卡片行距不一样"投诉的最大来源）。
//   * wrap_row28(QLayout*)：布局行（QHBoxLayout）包进固定高 28 容器；
//   * wrap_field28(QWidget*)：表单行（QFormLayout）字段包进固定高 28 容器（标签仍在表单
//     标签列右对齐，不改对齐语义；控件 24 高在 28 行内垂直居中）。
// 双行栅格（Δ 偏移 6 列 × 2 行）按 2×kRowPitch 钉高，保持 28 的整数倍节奏。
// ---------------------------------------------------------------------------
QWidget *wrap_row28(QLayout *line) {
    auto *host = new QWidget();
    auto *outer = new QVBoxLayout(host);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addLayout(line);
    host->setFixedHeight(formkit::kRowPitch);
    return host;
}

QWidget *wrap_field28(QWidget *field) {
    auto *host = new QWidget();
    auto *outer = new QVBoxLayout(host);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(field, 0, Qt::AlignVCenter);
    host->setFixedHeight(formkit::kRowPitch);
    return host;
}

} // namespace

// ===========================================================================
// Impl
// ===========================================================================

struct ValuePane {
    bool is_xmp = false;
    std::string key;
    QLineEdit *line = nullptr;
    QPlainTextEdit *multi = nullptr;
    QLabel *title = nullptr;
    QLabel *type = nullptr;
    QLabel *note = nullptr;
};

struct ExifEditor::Impl {
    ExifEditor *q = nullptr;
    QString src;
    pp::BatchRules batch;
    std::optional<pp::MetadataOverride> existing;
    pp::SourceMeta meta; // 只读源元数据（pp::read_metadata 结果）
    bool meta_loading = false; // R-081：后台读取中 —— 未完成时 accept 拒绝（防基底空写回）
    theme::Tokens tokens = theme::tokens(theme::preferred_theme_mode()); // M5-T10：弹窗令牌（单源）
    TagItemDelegate *delegate = nullptr; // M5-T10：行首 pill「批」+ 改值强调色字（A8）

    std::vector<pp::TagEdit> exif_edits, xmp_edits; // 树中的 set/remove 记录

    struct Row {
        std::string key;
        std::string src_value;
        bool in_source = false;
        bool is_xmp = false;
        bool read_only = false;
        bool lang_alt = false;
        Exiv2::TypeId type = Exiv2::asciiString;
        std::string type_name;
        QTreeWidgetItem *item = nullptr;
    };
    std::map<std::string, Row> rows;

    QCheckBox *ignore_check = nullptr;
    QTabWidget *tabs = nullptr;

    // EXIF 页
    QLineEdit *exif_search = nullptr;
    QTreeWidget *exif_tree = nullptr;
    QTreeWidgetItem *ifd0_node = nullptr;
    QTreeWidgetItem *exif_node = nullptr;
    QTreeWidgetItem *gps_node = nullptr;
    QTreeWidgetItem *maker_node = nullptr;
    std::map<std::string, QTreeWidgetItem *> maker_subs;
    ValuePane exif_pane;
    QLineEdit *exif_number = nullptr;
    QComboBox *exif_group = nullptr;
    QComboBox *exif_type = nullptr;

    // XMP 页
    QLineEdit *xmp_search = nullptr; // M2-T7 #29（objectName `xmp_search`）
    QTreeWidget *xmp_tree = nullptr;
    std::map<std::string, QTreeWidgetItem *> xmp_groups;
    ValuePane xmp_pane;
    QLineEdit *xmp_path = nullptr;

    // 时间 / GPS 页
    QComboBox *time_mode = nullptr;
    QWidget *time_params = nullptr;
    QComboBox *time_method = nullptr;
    QWidget *time_delta_box = nullptr;
    QWidget *time_tz_box = nullptr;
    QSpinBox *time_spin[6] = {};
    QComboBox *tz_from = nullptr;
    QComboBox *tz_to = nullptr;
    QComboBox *gps_mode = nullptr;
    QWidget *gps_params = nullptr;
    QLineEdit *gps_lat = nullptr;
    QLineEdit *gps_lon = nullptr;
    QLabel *gps_dms = nullptr;
    QGroupBox *gps_more = nullptr;
    QLineEdit *gps_alt = nullptr;
    QLineEdit *gps_dir = nullptr;
    QLineEdit *gps_ts = nullptr;
    QComboBox *privacy_mode = nullptr;
    // M4-T12（§5.2 生效值样式）：本文件例外的**生效值**回显（原 → 生效的"生效"端；改动走 .mod）
    // M4-T15 收尾：补齐 mockup `.tline` 的另两端 —— 原（删除线）+ 右端来源字段，
    //   与 ui/page_meta.cpp 时间卡同一版式（原 → 生效 + src）。
    ElidedLabel *time_effective = nullptr;
    ElidedLabel *time_effective_old = nullptr;
    ElidedLabel *time_effective_src = nullptr;
    ElidedLabel *gps_effective = nullptr;
    ElidedLabel *gps_effective_old = nullptr;

    void refresh_effective_values();
    void attach_delegate(QTreeWidget *tree); // M5-T10：树行 delegate（pill「批」/改值强调色字）
    void build();
    QWidget *make_exif_tab();
    QWidget *make_xmp_tab();
    QWidget *make_time_gps_tab();
    QWidget *make_value_pane(ValuePane &vp, const QString &name);

    void populate_exif_tree();
    void populate_xmp_tree();
    void init_from_existing();

    void add_row_item(Row r);
    // R-196c/R-198（M7-R2.1-F3）：Row 填充/派生字段单点（原三处各填一份）
    void add_source_row(const Exiv2::Metadatum &d, bool is_xmp);
    void finalize_row(Row &r, Exiv2::TypeId fallback_type);
    void ensure_row(const std::string &key, bool is_xmp, Exiv2::TypeId fallback_type);
    QTreeWidgetItem *group_parent(const std::string &key, bool is_xmp);

    void on_index_selection(ValuePane &vp, const QModelIndex &idx);
    void show_row(ValuePane &vp, const std::string &key);
    void clear_pane(ValuePane &vp);
    void commit(ValuePane &vp, const QString &text);
    void set_edit(bool is_xmp, const std::string &key, const std::string &text);
    const pp::TagEdit *find_edit(bool is_xmp, const std::string &key) const;
    std::string effective_value(const Row &r) const;
    QString row_text(const Row &r) const;
    void refresh_row(const std::string &key);
    void refresh_all_rows();

    void apply_exif_filter();
    void apply_xmp_filter(); // M2-T7 #29：XMP 页搜索（R-196：与 apply_exif_filter 共用单点）
    void apply_tree_filter(QTreeWidget *tree, const QString &needle, bool is_xmp); // R-196 过滤单点
    void add_exif_by_number();
    void add_xmp_by_path();
    Exiv2::TypeId type_from_combo() const;

    void update_time_mode();
    void update_gps_mode();
    void update_dms();
    pp::TimeShift time_shift_from_widgets() const;

    pp::MetadataOverride build_override(std::vector<std::string> *errors) const;
    void on_accept();
    void report_errors(const std::vector<std::string> &errors);
    void warn(const QString &title, const QString &text);
};

// —— 值编辑区 ——

QWidget *ExifEditor::Impl::make_value_pane(ValuePane &vp, const QString &name) {
    auto *box = new QWidget();
    auto *v = new QVBoxLayout(box);

    auto *title = new ElidedLabel();
    vp.title = title;
    vp.title->setObjectName(name + QStringLiteral("_value_title"));
    vp.title->setFont(
        theme::font(theme::Typography::card_title_px, QFont::Bold)); // §4.2 卡题 12/700
    v->addWidget(vp.title);

    vp.type = new QLabel();
    // M5-T10（A6）：硬编码灰 0x808080 → 令牌 txt3（§4.2 辅助 10px）
    vp.type->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote));
    vp.type->setFont(theme::font(theme::Typography::hint_px));
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

    vp.note = new ElidedLabel();
    vp.note->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote)); // A6：令牌 txt3
    vp.note->setFont(theme::font(theme::Typography::hint_px));
    v->addWidget(vp.note);

    // textChanged（而不是 textEdited）：程序化 setText 也能触发提交，自验/集成可直接注入值
    QObject::connect(vp.line, &QLineEdit::textChanged, q,
                     [this, &vp](const QString &text) { commit(vp, text); });
    QObject::connect(vp.multi, &QPlainTextEdit::textChanged, q,
                     [this, &vp] { commit(vp, vp.multi->toPlainText()); });

    clear_pane(vp);
    return box;
}

void ExifEditor::Impl::clear_pane(ValuePane &vp) {
    vp.key.clear();
    if (!vp.line)
        return;
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
    static_cast<ElidedLabel *>(vp.title)->set_full_text(T("未选择标签"));
    vp.type->clear();
    static_cast<ElidedLabel *>(vp.note)->set_full_text(T("在左侧选择标签后编辑其值"));
}

void ExifEditor::Impl::show_row(ValuePane &vp, const std::string &key) {
    auto it = rows.find(key);
    if (it == rows.end()) {
        clear_pane(vp);
        return;
    }
    const Row &r = it->second;
    vp.key = key;
    static_cast<ElidedLabel *>(vp.title)->set_full_text(QString::fromStdString(key));
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
    if (r.lang_alt)
        note += T("　LangAlt 只编辑 x-default（值后缀标注）");
    static_cast<ElidedLabel *>(vp.note)->set_full_text(note);
}

void ExifEditor::Impl::set_edit(bool is_xmp, const std::string &key, const std::string &text) {
    auto row_it = rows.find(key);
    const bool in_source = row_it != rows.end() && row_it->second.in_source;
    const std::string src_value = row_it != rows.end() ? row_it->second.src_value : std::string();

    std::vector<pp::TagEdit> &list = is_xmp ? xmp_edits : exif_edits;
    auto edit_it =
        std::find_if(list.begin(), list.end(), [&](const pp::TagEdit &e) { return e.key == key; });

    // 空值：源中不存在 → 本来就是无操作；源中存在 → remove。非空且等于源值 → 撤销记录。
    const bool noop = text.empty() ? !in_source : (in_source && text == src_value);
    if (noop) {
        if (edit_it != list.end())
            list.erase(edit_it);
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

void ExifEditor::Impl::commit(ValuePane &vp, const QString &text) {
    if (vp.key.empty())
        return;
    auto it = rows.find(vp.key);
    if (it == rows.end() || it->second.read_only)
        return;
    set_edit(vp.is_xmp, vp.key, text.toStdString());
    refresh_row(vp.key);
}

const pp::TagEdit *ExifEditor::Impl::find_edit(bool is_xmp, const std::string &key) const {
    const std::vector<pp::TagEdit> &list = is_xmp ? xmp_edits : exif_edits;
    for (const pp::TagEdit &e : list) {
        if (e.key == key)
            return &e;
    }
    return nullptr;
}

std::string ExifEditor::Impl::effective_value(const Row &r) const {
    if (const pp::TagEdit *e = find_edit(r.is_xmp, r.key)) {
        if (e->remove || !e->value.has_value())
            return {};
        return *e->value;
    }
    return r.src_value;
}

QString ExifEditor::Impl::row_text(const Row &r) const {
    // M5-T10（A8 零字形）：行文本只留「标签 = 值」——「批」「已改值」两个标记走 item data
    // （kBatchRole/kChangedRole）+ delegate 绘制（中性 pill / 强调色字），不再用 〔批〕/● 字形。
    QString t = QString::fromStdString(tag_name(r.key));
    t += QStringLiteral(" = ");
    const pp::TagEdit *e = find_edit(r.is_xmp, r.key);
    QString v;
    if (e && (e->remove || !e->value.has_value())) {
        v = T("（将删除）");
    } else {
        v = QString::fromStdString(effective_value(r));
        if (r.lang_alt && !v.isEmpty())
            v += T("（x-default）");
    }
    if (v.size() > kTreeValueMax)
        v = v.left(kTreeValueMax) + QStringLiteral("…");
    t += v;
    return t;
}

void ExifEditor::Impl::refresh_row(const std::string &key) {
    auto it = rows.find(key);
    if (it == rows.end() || !it->second.item)
        return;
    Row &r = it->second;
    const pp::BatchRules &eff = (ignore_check && ignore_check->isChecked()) ? kNoRules : batch;
    const bool batch_hit = batch_affects(eff, r.key, r.is_xmp);
    const bool changed = find_edit(r.is_xmp, r.key) != nullptr;
    QTreeWidgetItem *item = r.item;
    item->setData(0, kBatchRole, batch_hit); // delegate：中性 pill「批」
    item->setData(0, kChangedRole, changed); // delegate：强调色字 + 700
    item->setText(0, row_text(r));
    // 字色/字重同步写回 item（探针读回面 + 非 delegate 路径的兜底），色值 = 令牌（A6）
    QFont font = item->font(0);
    font.setWeight(changed ? QFont::Bold : QFont::Normal);
    item->setFont(0, font);
    item->setForeground(0, changed ? tokens.accent : tokens.text);
    item->setToolTip(0, QString::fromStdString(r.key + "  [" + r.type_name + "]"));
}

void ExifEditor::Impl::refresh_all_rows() {
    for (auto &[key, row] : rows) {
        (void)key;
        refresh_row(row.key);
    }
}

// —— 树构造 ——

// R-198 / R-196c（M7-R2.1-F3）：Row 派生字段填充单点 —— 显示名 / lang_alt / 只读 / 类型
// 回落（原 populate_exif_tree / populate_xmp_tree / ensure_row 三处各填一份 = R-196c
// 「Row 填充三处」；TypeId 触点收敛于此 + type_from_combo/is_long_text_type 两谓词）。
// fallback_type = exif_type 下拉的**真实作用**（ExifKey 构造失败或类型未知时的回落类型），
// 选择几乎不影响行为是既有语义 —— 名实一致，不假装它是类型系统入口（R-198）。
void ExifEditor::Impl::finalize_row(Row &r, Exiv2::TypeId fallback_type) {
    if (r.type == Exiv2::invalidTypeId)
        r.type = r.is_xmp ? Exiv2::xmpText : fallback_type;
    const char *tn = Exiv2::TypeInfo::typeName(r.type);
    r.type_name = tn ? tn : "Unknown";
    r.lang_alt = (r.type == Exiv2::langAlt);
    r.read_only = !r.is_xmp && exif_row_read_only(r.key);
}

// R-196c：源元数据 → Row → 树项的单点（原 populate_exif_tree / populate_xmp_tree 两处逐字
// 同款的字段搬运；Exifdatum/Xmpdatum 同为 Exiv2::Metadatum）。
void ExifEditor::Impl::add_source_row(const Exiv2::Metadatum &d, bool is_xmp) {
    Row r;
    r.key = d.key();
    r.is_xmp = is_xmp;
    r.in_source = true;
    r.src_value = metadatum_text(d);
    r.type = d.typeId();
    finalize_row(r, is_xmp ? Exiv2::xmpText : Exiv2::asciiString);
    add_row_item(std::move(r));
}

void ExifEditor::Impl::add_row_item(Row r) {
    r.item = new QTreeWidgetItem(group_parent(r.key, r.is_xmp));
    r.item->setData(0, kKeyRole, QString::fromStdString(r.key));
    r.item->setData(0, kNodeRole, kLeaf);
    const std::string key = r.key;
    rows.emplace(key, std::move(r));
    refresh_row(key);
}

QTreeWidgetItem *ExifEditor::Impl::group_parent(const std::string &key, bool is_xmp) {
    if (is_xmp) {
        const std::string prefix = key_second(key);
        auto it = xmp_groups.find(prefix);
        if (it != xmp_groups.end())
            return it->second;
        auto *node = new QTreeWidgetItem(xmp_tree);
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
    if (g == "Image")
        return ifd0_node;
    if (g == "Photo")
        return exif_node;
    if (g == "GPSInfo")
        return gps_node;
    auto it = maker_subs.find(g);
    if (it != maker_subs.end())
        return it->second;
    auto *node = new QTreeWidgetItem(maker_node);
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

    const Exiv2::ExifData &exif = meta.exif;
    for (const Exiv2::Exifdatum &d : exif)
        add_source_row(d, false); // R-196c：Row 填充单点（原逐字段搬运）
    exif_tree->expandAll();
}

void ExifEditor::Impl::populate_xmp_tree() {
    const Exiv2::XmpData &xmp = meta.xmp;
    for (const Exiv2::Xmpdatum &d : xmp)
        add_source_row(d, true); // R-196c：Row 填充单点
    xmp_tree->expandAll();
}

void ExifEditor::Impl::ensure_row(const std::string &key, bool is_xmp,
                                  Exiv2::TypeId fallback_type) {
    if (rows.find(key) != rows.end())
        return;
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
            r.type = fallback_type;
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
            r.type = fallback_type;
        }
    }
    finalize_row(r, fallback_type); // R-198/R-196c：派生字段填充单点
    add_row_item(std::move(r));
}

// —— 选择 / 过滤 ——

void ExifEditor::Impl::on_index_selection(ValuePane &vp, const QModelIndex &idx) {
    if (!idx.isValid() || idx.data(kNodeRole).toInt() != kLeaf) {
        clear_pane(vp);
        return;
    }
    show_row(vp, idx.data(kKeyRole).toString().toStdString());
}

// R-196（M7-R2.1-F3）：过滤双胞胎（apply_exif_filter / apply_xmp_filter 逐字同构）收敛为
// apply_tree_filter 单点 —— 命中可见 / 非命中隐藏 / 组内无命中 → 组隐藏 / 空搜索串全恢复
// （M2-T7 #29 的 XMP 页搜索语义逐字不变；XMP 树顶层项即命名空间分组）。
void ExifEditor::Impl::apply_tree_filter(QTreeWidget *tree, const QString &needle, bool is_xmp) {
    for (auto &[key, row] : rows) {
        (void)key;
        if (row.is_xmp != is_xmp || !row.item)
            continue;
        row.item->setHidden(!needle.isEmpty() &&
                            !row.item->text(0).contains(needle, Qt::CaseInsensitive));
    }
    std::function<bool(QTreeWidgetItem *)> node_visible = [&](QTreeWidgetItem *node) -> bool {
        bool any = false;
        for (int i = 0; i < node->childCount(); ++i) {
            QTreeWidgetItem *child = node->child(i);
            const bool vis = (child->data(0, kNodeRole).toInt() == kLeaf) ? !child->isHidden()
                                                                          : node_visible(child);
            child->setHidden(!vis);
            if (vis)
                any = true;
        }
        return any;
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *node = tree->topLevelItem(i);
        node->setHidden(!node_visible(node));
    }
}

void ExifEditor::Impl::apply_exif_filter() {
    apply_tree_filter(exif_tree, exif_search->text().trimmed(), false);
}

void ExifEditor::Impl::apply_xmp_filter() {
    apply_tree_filter(xmp_tree, xmp_search->text().trimmed(), true);
}

// —— 按编号 / 路径添加 ——

Exiv2::TypeId ExifEditor::Impl::type_from_combo() const {
    return static_cast<Exiv2::TypeId>(exif_type->currentData().toInt());
}

void ExifEditor::Impl::add_exif_by_number() {
    QString text = exif_number->text().trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        text = text.mid(2);
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
    } catch (const std::exception &e) {
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
    if (key.rfind("Xmp.", 0) == 0 && colon != std::string::npos)
        key[colon] = '.';
    if (key.rfind("Xmp.", 0) != 0 || key.find('.', 4) == std::string::npos) {
        warn(T("路径无效"), T("请输入形如 Xmp.dc.title 的完整路径"));
        return;
    }
    try {
        const Exiv2::XmpKey xk(key);
        (void)xk;
    } catch (const std::exception &e) {
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

QWidget *ExifEditor::Impl::make_exif_tab() {
    auto *page = new QWidget();
    auto *v = new QVBoxLayout(page);

    auto *split = new QSplitter(Qt::Horizontal);
    auto *left = new QWidget();
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    exif_search = new QLineEdit();
    exif_search->setObjectName(QStringLiteral("exif_search"));
    exif_search->setPlaceholderText(T("搜索标签名 / 键 / 值"));
    exif_search->setClearButtonEnabled(true);
    exif_search->setFont(theme::font(theme::Typography::body_px));
    exif_search->setProperty(dialogkit::kFieldProperty,
                             QString::fromLatin1(dialogkit::kFieldInput));
    exif_search->setAccessibleName(T("搜索标签"));
    lv->addWidget(exif_search);
    exif_tree = new QTreeWidget();
    exif_tree->setObjectName(QStringLiteral("exif_tree"));
    exif_tree->setHeaderLabels(QStringList{T("标签")});
    exif_tree->setUniformRowHeights(true);
    exif_tree->setAlternatingRowColors(true);
    exif_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    exif_tree->setFont(theme::font(theme::Typography::body_px));
    exif_tree->setAccessibleName(T("EXIF 标签树"));
    attach_delegate(exif_tree); // M5-T10：行首 pill「批」+ 改值强调色字（A8）
    lv->addWidget(exif_tree, 1);
    split->addWidget(left);

    exif_pane.is_xmp = false;
    split->addWidget(make_value_pane(exif_pane, QStringLiteral("exif")));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto *add = new QHBoxLayout();
    auto *group_label = new QLabel(T("分组"));
    group_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    group_label->setFont(theme::font(theme::Typography::small_px));
    add->addWidget(group_label);
    exif_group = make_combo(QStringLiteral("add_exif_group"), T("分组"));
    exif_group->addItem(T("IFD0"), QStringLiteral("Image"));
    exif_group->addItem(T("Exif"), QStringLiteral("Photo"));
    exif_group->addItem(T("GPS"), QStringLiteral("GPSInfo"));
    add->addWidget(exif_group);
    auto *number_label = new QLabel(T("编号 (hex)"));
    number_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    number_label->setFont(theme::font(theme::Typography::small_px));
    add->addWidget(number_label);
    exif_number = new QLineEdit();
    exif_number->setObjectName(QStringLiteral("add_exif_number"));
    exif_number->setPlaceholderText(QStringLiteral("0x0132"));
    exif_number->setMaximumWidth(110);
    exif_number->setFont(theme::font(theme::Typography::body_px));
    exif_number->setProperty(dialogkit::kFieldProperty,
                             QString::fromLatin1(dialogkit::kFieldInput));
    exif_number->setToolTip(T("十六进制标签编号，例如 0x0132"));
    add->addWidget(exif_number);
    auto *type_label = new QLabel(T("类型"));
    type_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    type_label->setFont(theme::font(theme::Typography::small_px));
    add->addWidget(type_label);
    exif_type = make_combo(QStringLiteral("add_exif_type"), T("类型"));
    exif_type->addItem(QStringLiteral("Byte"), static_cast<int>(Exiv2::unsignedByte));
    exif_type->addItem(QStringLiteral("Ascii"), static_cast<int>(Exiv2::asciiString));
    exif_type->addItem(QStringLiteral("Short"), static_cast<int>(Exiv2::unsignedShort));
    exif_type->addItem(QStringLiteral("Long"), static_cast<int>(Exiv2::unsignedLong));
    exif_type->addItem(QStringLiteral("Rational"), static_cast<int>(Exiv2::unsignedRational));
    exif_type->addItem(QStringLiteral("SRational"), static_cast<int>(Exiv2::signedRational));
    exif_type->addItem(QStringLiteral("Undefined"), static_cast<int>(Exiv2::undefined));
    exif_type->setCurrentIndex(1); // Ascii
    add->addWidget(exif_type);
    auto *add_button = new QPushButton(T("添加标签"));
    add_button->setObjectName(QStringLiteral("add_exif_button"));
    hook_button(add_button, /*primary=*/false); // R4 按钮 26（A7）
    add->addWidget(add_button);
    add->addStretch(1);
    v->addWidget(wrap_row28(add));

    QObject::connect(add_button, &QPushButton::clicked, q, [this] { add_exif_by_number(); });
    QObject::connect(exif_number, &QLineEdit::returnPressed, q, [this] { add_exif_by_number(); });
    // 选中 → 右值区：挂 selectionModel 的 currentChanged（用户点击与程序化
    // setCurrentItem/setCurrentIndex 都会同步；currentItemChanged 覆盖不到后者）
    QObject::connect(exif_tree->selectionModel(), &QItemSelectionModel::currentChanged, q,
                     [this](const QModelIndex &cur, const QModelIndex &) {
                         on_index_selection(exif_pane, cur);
                     });
    QObject::connect(exif_search, &QLineEdit::textChanged, q,
                     [this](const QString &) { apply_exif_filter(); });
    return page;
}

QWidget *ExifEditor::Impl::make_xmp_tab() {
    auto *page = new QWidget();
    auto *v = new QVBoxLayout(page);

    auto *split = new QSplitter(Qt::Horizontal);
    auto *left = new QWidget();
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    // M2-T7 #29：XMP 页搜索框（与 EXIF 页 exif_search 同构；objectName `xmp_search`）
    xmp_search = new QLineEdit();
    xmp_search->setObjectName(QStringLiteral("xmp_search"));
    xmp_search->setPlaceholderText(T("搜索标签名 / 键 / 值"));
    xmp_search->setClearButtonEnabled(true);
    xmp_search->setFont(theme::font(theme::Typography::body_px));
    xmp_search->setProperty(dialogkit::kFieldProperty, QString::fromLatin1(dialogkit::kFieldInput));
    xmp_search->setAccessibleName(T("搜索属性"));
    lv->addWidget(xmp_search);
    xmp_tree = new QTreeWidget();
    xmp_tree->setObjectName(QStringLiteral("xmp_tree"));
    xmp_tree->setHeaderLabels(QStringList{T("属性")});
    xmp_tree->setUniformRowHeights(true);
    xmp_tree->setAlternatingRowColors(true);
    xmp_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    xmp_tree->setFont(theme::font(theme::Typography::body_px));
    xmp_tree->setAccessibleName(T("XMP 属性树"));
    attach_delegate(xmp_tree);
    lv->addWidget(xmp_tree, 1);
    split->addWidget(left);

    xmp_pane.is_xmp = true;
    split->addWidget(make_value_pane(xmp_pane, QStringLiteral("xmp")));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto *add = new QHBoxLayout();
    xmp_path = new QLineEdit();
    xmp_path->setObjectName(QStringLiteral("add_xmp_path"));
    xmp_path->setPlaceholderText(T("Xmp.xxx.yyy 自定义路径"));
    xmp_path->setFont(theme::font(theme::Typography::body_px));
    xmp_path->setProperty(dialogkit::kFieldProperty, QString::fromLatin1(dialogkit::kFieldInput));
    xmp_path->setToolTip(T("完整 XMP 路径，例如 Xmp.dc.title"));
    add->addWidget(xmp_path, 1);
    auto *add_button = new QPushButton(T("添加标签"));
    add_button->setObjectName(QStringLiteral("add_xmp_button"));
    hook_button(add_button, /*primary=*/false);
    add->addWidget(add_button);
    v->addWidget(wrap_row28(add));

    QObject::connect(add_button, &QPushButton::clicked, q, [this] { add_xmp_by_path(); });
    QObject::connect(xmp_path, &QLineEdit::returnPressed, q, [this] { add_xmp_by_path(); });
    // M2-T7 #29：XMP 搜索过滤（与 exif_search 同构）
    QObject::connect(xmp_search, &QLineEdit::textChanged, q,
                     [this](const QString &) { apply_xmp_filter(); });
    // 同 EXIF 页：selectionModel::currentChanged（含程序化 setCurrentIndex）
    QObject::connect(
        xmp_tree->selectionModel(), &QItemSelectionModel::currentChanged, q,
        [this](const QModelIndex &cur, const QModelIndex &) { on_index_selection(xmp_pane, cur); });
    return page;
}

void ExifEditor::Impl::update_time_mode() {
    // R-199：三态/二态读回走 itemData（原 currentIndex()==1/2 魔法索引）
    time_params->setEnabled(tri_of(time_mode) == TriMode::Override);
    const bool delta = method_of(time_method) == TimeMethod::Delta;
    time_delta_box->setVisible(delta);
    time_tz_box->setVisible(!delta);
    refresh_effective_values();
}

pp::TimeShift ExifEditor::Impl::time_shift_from_widgets() const {
    pp::TimeShift ts;
    if (method_of(time_method) == TimeMethod::TimeZone) { // R-199
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
    gps_params->setEnabled(tri_of(gps_mode) == TriMode::Override); // R-199
    refresh_effective_values();
}

void ExifEditor::Impl::update_dms() {
    bool ok_lat = false, ok_lon = false;
    const double lat = gps_lat->text().trimmed().toDouble(&ok_lat);
    const double lon = gps_lon->text().trimmed().toDouble(&ok_lon);
    if (ok_lat && ok_lon && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
        gps_dms->setText(pp::map::deg_to_dms(lat, true) + QStringLiteral(" ") +
                         pp::map::deg_to_dms(lon, false)); // R-099：第三轨统一走 deg_to_dms
    } else {
        gps_dms->setText(QStringLiteral("—"));
    }
    refresh_effective_values();
}

// M4-T12（§5.2 / §9.2 .mod）：本文件例外的**生效值**回显 —— 数据源 = 与写路径同源的
// `pp::preview_effective(meta, batch, 当前例外)`（不另算）；改动值走 theme::set_mod
// （强调边框+强调色字+700）。显示口径与元数据页一致：隐私剥除 → 「将被移除」；
// GPS 清除 → 「将清除 GPS」；无值 → 「（无）」。
// M4-T15 收尾（mockup meta-dark.html:155-158 的 .tline 逐字口径）：
//   * 生效值行补「原（删除线）→ 生效」两端 + 右端来源字段（P3），与 ui/page_meta.cpp 时间卡同形；
//   * 本页自己的**改动值**一并挂 .mod：Δ 非零的 6 个 spin、时区 from≠to 的两个 combo
//     （mockup `.spin.mod` = 强调边框+强调色字+700；判据与 page_meta.cpp 的 refresh_time_inputs
//      逐条同款 —— 两页对"改动值"的定义必须一致）。
void ExifEditor::Impl::refresh_effective_values() {
    if (time_effective == nullptr || gps_effective == nullptr) {
        return;
    }
    const pp::MetadataOverride ov = build_override(nullptr);
    const pp::EffectivePreview preview = pp::preview_effective(meta, batch, ov);
    const auto dt_text = [](const std::optional<pp::DateTimeVal> &v, bool strip) {
        if (strip) {
            return T("将被移除");
        }
        if (!v.has_value()) {
            return T("（无）");
        }
        QString text = QString::fromStdString(v->value);
        if (text.size() >= 10) {
            QString date = text.left(10);
            date.replace(QLatin1Char(':'), QLatin1Char('-'));
            text = date + text.mid(10);
        }
        return text;
    };
    if (time_effective_old != nullptr) {
        time_effective_old->set_full_text(dt_text(preview.datetime_original.original, false));
    }
    time_effective->set_full_text(
        T("生效：") + dt_text(preview.datetime_original.effective, preview.strip_privacy));
    theme::set_mod(time_effective, preview.datetime_original.changed);
    if (time_effective_src != nullptr) {
        // 生效值行的来源字段（写路径读的同一字段；与元数据页首行 DateTimeOriginal 一致）
        time_effective_src->set_full_text(QStringLiteral("DateTimeOriginal"));
    }
    QString gps_text;
    if (preview.strip_privacy) {
        gps_text = T("将被移除");
    } else if (ov.gps_clear) {
        gps_text = T("将清除 GPS");
    } else if (preview.gps.effective.has_value()) {
        gps_text =
            QStringLiteral("%1, %2").arg(QString::number(preview.gps.effective->lat, 'f', 6),
                                         QString::number(preview.gps.effective->lon, 'f', 6));
    } else {
        gps_text = T("（无）");
    }
    if (gps_effective_old != nullptr) {
        gps_effective_old->set_full_text(
            preview.gps.original.has_value()
                ? QStringLiteral("%1, %2").arg(QString::number(preview.gps.original->lat, 'f', 6),
                                               QString::number(preview.gps.original->lon, 'f', 6))
                : T("（无）"));
    }
    gps_effective->set_full_text(T("生效：") + gps_text);
    theme::set_mod(gps_effective, preview.gps.changed);
    // —— 本页改动值的 .mod（§9.2 / mockup .spin.mod）——
    for (QSpinBox *spin : time_spin) {
        if (spin != nullptr) {
            theme::set_mod(spin, spin->value() != 0);
        }
    }
    if (tz_from != nullptr && tz_to != nullptr) {
        const bool tz_changed = tz_from->currentData().toInt() != tz_to->currentData().toInt();
        theme::set_mod(tz_from, tz_changed);
        theme::set_mod(tz_to, tz_changed);
    }
}

QWidget *ExifEditor::Impl::make_time_gps_tab() {
    auto *page = new QWidget();
    auto *outer = new QVBoxLayout(page);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget();
    auto *v = new QVBoxLayout(inner);

    auto *hint = new ElidedLabel();
    hint->set_full_text(T("以下三态只作用于当前文件的例外：继承批量规则 / 覆盖 / 清除。"));
    hint->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote)); // A6：令牌 txt3
    hint->setFont(theme::font(theme::Typography::hint_px));
    v->addWidget(hint);

    // 1) 时间三态
    auto *time_group = new QGroupBox(T("时间"));
    time_group->setFont(theme::font(theme::Typography::small_px, QFont::DemiBold));
    auto *tv = new QVBoxLayout(time_group);
    auto *time_row = new QHBoxLayout();
    auto *time_label = new QLabel(T("时间偏移"));
    time_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    time_label->setFont(theme::font(theme::Typography::small_px));
    time_row->addWidget(time_label);
    time_mode = make_combo(QStringLiteral("time_mode"), T("时间偏移"),
                           T("本文件的时间处理：继承批量规则 / 覆盖 / 清除时间标签"));
    add_tri_items(time_mode, T("继承批量规则"), T("覆盖"),
                  T("清除")); // R-199：itemData = TriMode（唯一真相）
    time_row->addWidget(time_mode);
    time_row->addStretch(1);
    tv->addWidget(wrap_row28(time_row));

    time_params = new QWidget();
    auto *tp = new QVBoxLayout(time_params);
    tp->setContentsMargins(0, 0, 0, 0);
    auto *method_row = new QHBoxLayout();
    auto *method_label = new QLabel(T("方式"));
    method_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    method_label->setFont(theme::font(theme::Typography::small_px));
    method_row->addWidget(method_label);
    time_method = make_combo(QStringLiteral("time_method"), T("方式"),
                             T("Δ 偏移 = 直接加减时间差；时区语义 = 按「从/到」时区换算"));
    time_method->addItem(T("Δ 偏移（时钟拨错）"), static_cast<int>(TimeMethod::Delta)); // R-199
    time_method->addItem(T("时区语义（旅行照片）"), static_cast<int>(TimeMethod::TimeZone));
    method_row->addWidget(time_method);
    method_row->addStretch(1);
    tp->addWidget(wrap_row28(method_row));

    time_delta_box = new QWidget();
    auto *grid = new QGridLayout(time_delta_box);
    grid->setContentsMargins(0, 0, 0, 0);
    const QString labels[6] = {T("年"), T("月"), T("日"), T("时"), T("分"), T("秒")};
    const char *names[6] = {"time_years", "time_months",  "time_days",
                            "time_hours", "time_minutes", "time_seconds"};
    for (int i = 0; i < 6; ++i) {
        auto *field_label = new QLabel(labels[i]);
        field_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
        field_label->setFont(theme::font(theme::Typography::small_px));
        field_label->setAlignment(Qt::AlignCenter);
        grid->addWidget(field_label, 0, i);
        time_spin[i] = new QSpinBox();
        time_spin[i]->setObjectName(QString::fromLatin1(names[i]));
        time_spin[i]->setRange(-9999, 9999);
        dress_spin(time_spin[i], labels[i]);
        grid->addWidget(time_spin[i], 1, i);
    }
    time_delta_box->setFixedHeight(formkit::kRowPitch * 2); // UX-6③：双行栅格 = 2×28
    tp->addWidget(time_delta_box);

    time_tz_box = new QWidget();
    auto *tz = new QHBoxLayout(time_tz_box);
    tz->setContentsMargins(0, 0, 0, 0);
    tz_from = make_combo(QStringLiteral("tz_from"), T("从"),
                         T("↑↓ 选择原时区（整点 / 半小时）；与「到」不同即视为改动"));
    tz_to = make_combo(QStringLiteral("tz_to"), T("到"), T("↑↓ 选择目标时区（整点 / 半小时）"));
    for (int m = -12 * 60; m <= 14 * 60; m += 30) { // 整点 + 半小时时区全表
        tz_from->addItem(offset_label(m), m);
        tz_to->addItem(offset_label(m), m);
    }
    const int zero_from = tz_from->findData(0);
    if (zero_from >= 0)
        tz_from->setCurrentIndex(zero_from);
    const int zero_to = tz_to->findData(0);
    if (zero_to >= 0)
        tz_to->setCurrentIndex(zero_to);
    auto *tz_from_label = new QLabel(T("从"));
    tz_from_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    tz_from_label->setFont(theme::font(theme::Typography::small_px));
    auto *tz_to_label = new QLabel(T("到"));
    tz_to_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    tz_to_label->setFont(theme::font(theme::Typography::small_px));
    tz->addWidget(tz_from_label);
    tz->addWidget(tz_from);
    tz->addWidget(tz_to_label);
    tz->addWidget(tz_to);
    tz->addStretch(1);
    time_tz_box->setFixedHeight(formkit::kRowPitch); // UX-6③：行高 28
    tp->addWidget(time_tz_box);
    tv->addWidget(time_params);
    // M4-T15（§5.2 + mockup .tline）：原（删除线）→ 生效（强调色，改动值另加 .mod 边框）
    // + 右端来源字段。删除线在 QFont 侧落地（Qt QSS 无 text-decoration，与 page_meta 同款）。
    {
        auto *eff_row = new QWidget();
        auto *eh = new QHBoxLayout(eff_row);
        eh->setContentsMargins(0, 0, 0, 0);
        eh->setSpacing(10);
        time_effective_old = new ElidedLabel();
        time_effective_old->setObjectName(QStringLiteral("time_effective_old"));
        QFont old_font = time_effective_old->font();
        old_font.setStrikeOut(true);
        time_effective_old->setFont(old_font);
        auto *arrow = new QLabel(QStringLiteral("→"));
        time_effective = new ElidedLabel();
        time_effective->setObjectName(QStringLiteral("time_effective"));
        QFont new_font = time_effective->font();
        new_font.setBold(true);
        time_effective->setFont(new_font);
        time_effective_src = new ElidedLabel();
        time_effective_src->setObjectName(QStringLiteral("time_effective_src"));
        time_effective_src->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        eh->addWidget(time_effective_old, 0);
        eh->addWidget(arrow, 0);
        eh->addWidget(time_effective, 0);
        eh->addStretch(1);
        eh->addWidget(time_effective_src, 0);
        eff_row->setFixedHeight(formkit::kRowPitch); // UX-6③：行高 28
        tv->addWidget(eff_row);
    }
    v->addWidget(time_group);

    // 2) GPS 三态
    auto *gps_group = new QGroupBox(T("GPS"));
    gps_group->setFont(theme::font(theme::Typography::small_px, QFont::DemiBold));
    auto *gv = new QVBoxLayout(gps_group);
    auto *gps_row = new QHBoxLayout();
    auto *gps_label = new QLabel(T("GPS"));
    gps_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    gps_label->setFont(theme::font(theme::Typography::small_px));
    gps_row->addWidget(gps_label);
    gps_mode = make_combo(QStringLiteral("gps_mode"), T("GPS"),
                          T("本文件的 GPS 处理：继承批量规则 / 覆盖 / 清除"));
    add_tri_items(gps_mode, T("继承批量规则"), T("覆盖"),
                  T("清除 GPS")); // R-199：itemData = TriMode（唯一真相）
    gps_row->addWidget(gps_mode);
    gps_row->addStretch(1);
    gv->addWidget(wrap_row28(gps_row));

    gps_params = new QWidget();
    auto *gp = new QVBoxLayout(gps_params);
    gp->setContentsMargins(0, 0, 0, 0);
    auto *coord = new QFormLayout();
    gps_lat = new QLineEdit();
    gps_lat->setObjectName(QStringLiteral("gps_lat"));
    gps_lat->setPlaceholderText(QStringLiteral("31.230400"));
    auto *lat_validator = new QDoubleValidator(-90.0, 90.0, 6, gps_lat);
    lat_validator->setNotation(QDoubleValidator::StandardNotation);
    lat_validator->setLocale(QLocale::c());
    gps_lat->setValidator(lat_validator);
    dress_edit(gps_lat, T("纬度"), T("十进制纬度（-90..90，6 位小数）"));
    gps_lon = new QLineEdit();
    gps_lon->setObjectName(QStringLiteral("gps_lon"));
    gps_lon->setPlaceholderText(QStringLiteral("121.473700"));
    auto *lon_validator = new QDoubleValidator(-180.0, 180.0, 6, gps_lon);
    lon_validator->setNotation(QDoubleValidator::StandardNotation);
    lon_validator->setLocale(QLocale::c());
    gps_lon->setValidator(lon_validator);
    dress_edit(gps_lon, T("经度"), T("十进制经度（-180..180，6 位小数）"));
    coord->addRow(T("纬度"), wrap_field28(gps_lat));
    coord->addRow(T("经度"), wrap_field28(gps_lon));
    gps_dms = new QLabel(QStringLiteral("—"));
    gps_dms->setObjectName(QStringLiteral("gps_dms"));
    gps_dms->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote)); // A6：令牌 txt3
    gps_dms->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    coord->addRow(T("DMS"), wrap_field28(gps_dms));
    gp->addLayout(coord);

    gps_more = new QGroupBox(T("更多字段"));
    gps_more->setObjectName(QStringLiteral("gps_more"));
    gps_more->setCheckable(true);
    gps_more->setChecked(false);
    gps_more->setToolTip(T("收起时不写入可选字段"));
    auto *more_form = new QFormLayout(gps_more);
    gps_alt = new QLineEdit();
    gps_alt->setObjectName(QStringLiteral("gps_alt"));
    gps_dir = new QLineEdit();
    gps_dir->setObjectName(QStringLiteral("gps_dir"));
    gps_ts = new QLineEdit();
    gps_ts->setObjectName(QStringLiteral("gps_ts"));
    gps_ts->setPlaceholderText(QStringLiteral("YYYY:MM:DD HH:MM:SS"));
    dress_edit(gps_alt, T("海拔"), T("海拔（米，可空）"));
    dress_edit(gps_dir, T("方位角"), T("方位角（0–359.99 度，可空）"));
    dress_edit(gps_ts, T("时间戳"), T("GPS 时间戳（YYYY:MM:DD HH:MM:SS，可空）"));
    more_form->addRow(T("海拔 (m)"), wrap_field28(gps_alt));
    more_form->addRow(T("方位角 (°)"), wrap_field28(gps_dir));
    more_form->addRow(T("时间戳"), wrap_field28(gps_ts));
    gp->addWidget(gps_more);
    gv->addWidget(gps_params);
    // M4-T15（§5.2 + mockup .tline）：原坐标（删除线）→ 生效坐标（强调色/改动值带 .mod）。
    {
        auto *eff_row = new QWidget();
        auto *eh = new QHBoxLayout(eff_row);
        eh->setContentsMargins(0, 0, 0, 0);
        eh->setSpacing(10);
        gps_effective_old = new ElidedLabel();
        gps_effective_old->setObjectName(QStringLiteral("gps_effective_old"));
        QFont old_font = gps_effective_old->font();
        old_font.setStrikeOut(true);
        gps_effective_old->setFont(old_font);
        auto *arrow = new QLabel(QStringLiteral("→"));
        gps_effective = new ElidedLabel();
        gps_effective->setObjectName(QStringLiteral("gps_effective"));
        QFont new_font = gps_effective->font();
        new_font.setBold(true);
        gps_effective->setFont(new_font);
        eh->addWidget(gps_effective_old, 0);
        eh->addWidget(arrow, 0);
        eh->addWidget(gps_effective, 0);
        eh->addStretch(1);
        eff_row->setFixedHeight(formkit::kRowPitch); // UX-6③：行高 28
        gv->addWidget(eff_row);
    }
    v->addWidget(gps_group);

    // 3) 隐私剥除三态
    auto *privacy_group = new QGroupBox(T("隐私剥除"));
    privacy_group->setFont(theme::font(theme::Typography::small_px, QFont::DemiBold));
    auto *pv = new QVBoxLayout(privacy_group);
    auto *privacy_row = new QHBoxLayout();
    auto *privacy_label = new QLabel(T("剥除全部 EXIF / XMP（保留 ICC 与像素）"));
    privacy_label->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkLabel));
    privacy_label->setFont(theme::font(theme::Typography::small_px));
    privacy_row->addWidget(privacy_label);
    privacy_mode = make_combo(QStringLiteral("privacy_mode"), T("隐私剥除"),
                              T("本文件的隐私剥除：继承批量规则 / 强制开 / 强制关"));
    privacy_mode->addItem(T("继承批量规则"), static_cast<int>(PrivacyMode::Inherit)); // R-199
    privacy_mode->addItem(T("强制开"), static_cast<int>(PrivacyMode::ForceOn));
    privacy_mode->addItem(T("强制关"), static_cast<int>(PrivacyMode::ForceOff));
    privacy_row->addWidget(privacy_mode);
    privacy_row->addStretch(1);
    pv->addWidget(wrap_row28(privacy_row));
    auto *priority = new QLabel(T("优先级最高的规则"));
    priority->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkNote)); // A6：令牌 txt3
    priority->setFont(theme::font(theme::Typography::hint_px));
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
    QObject::connect(gps_lat, &QLineEdit::textChanged, q,
                     [this](const QString &) { update_dms(); });
    QObject::connect(gps_lon, &QLineEdit::textChanged, q,
                     [this](const QString &) { update_dms(); });

    // 生效值回显的刷新触发点（任一影响例外的控件变化 → 重算显示）
    for (QSpinBox *spin : time_spin) {
        QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), q,
                         [this](int) { refresh_effective_values(); });
    }
    for (QComboBox *combo : {tz_from, tz_to}) {
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                         [this](int) { refresh_effective_values(); });
    }
    for (QLineEdit *edit : {gps_alt, gps_dir, gps_ts}) {
        QObject::connect(edit, &QLineEdit::textChanged, q,
                         [this](const QString &) { refresh_effective_values(); });
    }
    QObject::connect(gps_more, &QGroupBox::toggled, q,
                     [this](bool) { refresh_effective_values(); });
    QObject::connect(privacy_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                     [this](int) { refresh_effective_values(); });

    update_time_mode();
    update_gps_mode();
    update_dms();
    return page;
}

// —— 顶部与收尾 ——

void ExifEditor::Impl::attach_delegate(QTreeWidget *tree) {
    if (tree == nullptr)
        return;
    if (delegate == nullptr) {
        delegate = new TagItemDelegate(tree); // 两棵树共用一个实例（delegate 不取所有权）
        delegate->set_tokens(tokens);
    }
    tree->setItemDelegate(delegate);
}

void ExifEditor::Impl::build() {
    // M5-T10：本弹窗与设置弹窗**同一套语言** —— chrome/控件规格/QSS 走 dialogkit 单实现
    // （M4-T12 的 .mod 规则仍在本对话框自己的作用域里：主窗 styleSheet 不覆盖独立顶层窗口的子树）。
    tokens = theme::tokens(theme::preferred_theme_mode());
    q->setStyleSheet(dialogkit::dialog_qss(tokens));
    q->setWindowTitle(T("元数据编辑器 — ") + QFileInfo(src).fileName());
    q->resize(900, 640);
    q->setModal(true);

    auto *root = new QVBoxLayout(q);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 自绘标题栏 40px（中性底 + 矢量图标 + 关闭 caption；§7.4 同一语言）
    const dialogkit::TitleBar title_bar = dialogkit::make_title_bar(
        q, icons::Name::Image, T("元数据编辑器 — ") + QFileInfo(src).fileName());
    title_bar.title->setToolTip(src); // 全文路径（标题栏窄时仍可读；替代原文件名列）
    root->addWidget(title_bar.widget);

    auto *body = new QWidget();
    auto *body_line = new QVBoxLayout(body);
    body_line->setContentsMargins(theme::Metrics::main_pad_x, theme::Metrics::main_pad_top,
                                  theme::Metrics::main_pad_x, theme::Metrics::main_pad_bottom);
    body_line->setSpacing(theme::Metrics::group_gap);

    auto *top = new QHBoxLayout();
    top->addStretch(1);
    // M5-T10（R4/4-03）：勾选框改 formkit 单实现（13×13 + 矢量勾，与主窗同规格）
    auto *ignore = new formkit::CheckBox(body);
    ignore_check = ignore;
    ignore_check->setObjectName(QStringLiteral("ignore_batch"));
    ignore_check->setText(T("忽略全部批量规则"));
    ignore_check->setToolTip(T("勾选后本文件不使用任何批量规则，只应用下面的例外"));
    ignore_check->setAccessibleName(T("忽略全部批量规则"));
    ignore_check->setChecked(existing.has_value() && existing->ignore_batch);
    ignore->set_tokens(tokens); // formkit 自绘勾选框（13×13 + 矢量勾）
    top->addWidget(ignore_check);
    body_line->addWidget(wrap_row28(top));

    if (!meta.error.empty()) {
        auto *err = new ElidedLabel();
        err->set_full_text(T("无法读取源文件元数据：") + QString::fromStdString(meta.error));
        // M5-T10（A6）：硬编码 0xdd3333 → 令牌 err（§4.1 语义色）
        QPalette err_pal = err->palette();
        err_pal.setColor(QPalette::WindowText, tokens.err);
        err->setPalette(err_pal);
        body_line->addWidget(err);
    }

    tabs = new QTabWidget();
    tabs->setObjectName(QStringLiteral("tabs"));
    tabs->addTab(make_exif_tab(), T("EXIF"));
    tabs->addTab(make_xmp_tab(), T("XMP"));
    tabs->addTab(make_time_gps_tab(), T("时间 / GPS"));
    body_line->addWidget(tabs, 1);

    auto *button_row = new QHBoxLayout();
    button_row->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setObjectName(QStringLiteral("buttons"));
    // R4 按钮 26 + 主/次（A7）：确定 = 实心 acc（主操作居右），取消 = 描边
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(T("确定"));
    hook_button(ok, /*primary=*/true);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(T("取消"));
    hook_button(cancel, /*primary=*/false);
    button_row->addWidget(buttons);
    body_line->addWidget(wrap_row28(button_row));

    root->addWidget(body, 1);

    // §11：自绘焦点环（accent 1px；系统虚线框由 dialog_qss 的 outline:none 关闭）
    auto *ring = new dialogkit::FocusRing(q);
    ring->set_tokens(tokens);

    QObject::connect(buttons, &QDialogButtonBox::accepted, q, [this] { on_accept(); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, q, [this] { q->reject(); });
    QObject::connect(ignore_check, &QCheckBox::toggled, q, [this](bool) {
        refresh_all_rows();
        refresh_effective_values();
    });

    // 无边框接线（拖拽 / 八向缩放；与主窗、设置弹窗同一机制）
    pp::platform::Frameless::attach(q, title_bar.widget, {title_bar.close});
    dialogkit::bind_close_button(title_bar, q); // GUI-R2 修 3：caption ✕ 必须真关窗
}

void ExifEditor::Impl::init_from_existing() {
    if (!existing.has_value())
        return;
    const pp::MetadataOverride &ex = *existing;

    ignore_check->setChecked(ex.ignore_batch);
    exif_edits = ex.exif_edits;
    xmp_edits = ex.xmp_edits;
    for (const pp::TagEdit &e : exif_edits)
        ensure_row(e.key, false, Exiv2::asciiString);
    for (const pp::TagEdit &e : xmp_edits)
        ensure_row(e.key, true, Exiv2::xmpText);

    if (ex.time_shift.has_value()) {
        set_tri(time_mode, TriMode::Override); // R-199：落值走 findData（非魔法索引）
        const pp::TimeShift &ts = *ex.time_shift;
        if (ts.mode == pp::TimeShift::Mode::TimezoneSemantic) {
            set_method(time_method, TimeMethod::TimeZone);
            const int i1 = tz_from->findData(ts.from_offset_min);
            if (i1 >= 0)
                tz_from->setCurrentIndex(i1);
            const int i2 = tz_to->findData(ts.to_offset_min);
            if (i2 >= 0)
                tz_to->setCurrentIndex(i2);
        } else {
            set_method(time_method, TimeMethod::Delta);
            time_spin[0]->setValue(ts.years);
            time_spin[1]->setValue(ts.months);
            time_spin[2]->setValue(ts.days);
            time_spin[3]->setValue(ts.hours);
            time_spin[4]->setValue(ts.minutes);
            time_spin[5]->setValue(ts.seconds);
        }
    }

    if (ex.gps_clear) {
        set_tri(gps_mode, TriMode::Clear); // R-199
    } else if (ex.gps.has_value()) {
        set_tri(gps_mode, TriMode::Override);
        const pp::GpsData &g = *ex.gps;
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

    if (ex.strip_privacy.has_value())
        set_privacy(privacy_mode,
                    *ex.strip_privacy ? PrivacyMode::ForceOn : PrivacyMode::ForceOff); // R-199

    update_dms();
    refresh_all_rows();
}

// —— 校验 / 结果 ——

void ExifEditor::Impl::report_errors(const std::vector<std::string> &errors) {
    QStringList list;
    for (const std::string &e : errors)
        list << QString::fromStdString(e);
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

void ExifEditor::Impl::warn(const QString &title, const QString &text) {
    if (q->property(kSuppressModal).toBool()) {
        q->setProperty(kLastWarning, text);
        return;
    }
    QMessageBox::warning(q, title, text);
}

// R-196（M7-R2.1-F3）：remove 编辑的 upsert 单点（原时间清除 EXIF/XMP 双循环逐字同款）。
void upsert_remove_edit(std::vector<pp::TagEdit> &edits, const std::string &key) {
    pp::TagEdit e;
    e.key = key;
    e.remove = true;
    auto it = std::find_if(edits.begin(), edits.end(),
                           [&](const pp::TagEdit &x) { return x.key == key; });
    if (it != edits.end()) {
        *it = e;
    } else {
        edits.push_back(e);
    }
}

pp::MetadataOverride ExifEditor::Impl::build_override(std::vector<std::string> *errors) const {
    pp::MetadataOverride ov;
    ov.ignore_batch = ignore_check->isChecked();
    ov.exif_edits = exif_edits;
    ov.xmp_edits = xmp_edits;

    // 时间三态：覆盖 = time_shift；清除 = 对时间标签的 remove 编辑（MetadataOverride 无
    // time_clear）。R-199：读回 itemData；R-196/R-197：清除集 = time_tag_keys.h 的
    // wall_clock 子集（5 键，与引擎写路径同集），EXIF/XMP 双循环收敛为 upsert_remove_edit。
    const TriMode tmode = tri_of(time_mode);
    if (tmode == TriMode::Override) {
        ov.time_shift = time_shift_from_widgets();
    } else if (tmode == TriMode::Clear) {
        for (std::size_t i = 0; i < timetags::kTimeTagCount; ++i) {
            const timetags::TimeTag &tag = timetags::kTimeTags[i];
            if (!tag.wall_clock)
                continue;
            upsert_remove_edit(tag.xmp ? ov.xmp_edits : ov.exif_edits, tag.key);
        }
    }

    // GPS 三态：覆盖（校验经纬度与可选字段）/ 清除
    const TriMode gmode = tri_of(gps_mode);
    if (gmode == TriMode::Clear) {
        ov.gps_clear = true;
    } else if (gmode == TriMode::Override) {
        bool ok_lat = false, ok_lon = false;
        const double lat = gps_lat->text().trimmed().toDouble(&ok_lat);
        const double lon = gps_lon->text().trimmed().toDouble(&ok_lon);
        const bool lat_ok = ok_lat && lat >= -90.0 && lat <= 90.0;
        const bool lon_ok = ok_lon && lon >= -180.0 && lon <= 180.0;
        if (!lat_ok && errors)
            errors->push_back("GPS 纬度无效（需为 -90..90 的十进制数）");
        if (!lon_ok && errors)
            errors->push_back("GPS 经度无效（需为 -180..180 的十进制数）");
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
                        if (errors)
                            errors->push_back("GPS 海拔必须是数字");
                    } else {
                        g.altitude = v;
                    }
                }
                const QString dir = gps_dir->text().trimmed();
                if (!dir.isEmpty()) {
                    bool ok = false;
                    const double v = dir.toDouble(&ok);
                    if (!ok || v < 0.0 || v > 359.99) {
                        if (errors)
                            errors->push_back("GPS 方位角需为 0–359.99 的数字");
                    } else {
                        g.direction = v;
                    }
                }
                const QString ts = gps_ts->text().trimmed();
                if (!ts.isEmpty()) {
                    static const QRegularExpression re(
                        QStringLiteral("^\\d{4}:\\d{2}:\\d{2} \\d{2}:\\d{2}:\\d{2}$"));
                    if (!re.match(ts).hasMatch()) {
                        if (errors)
                            errors->push_back("GPS 时间戳格式应为 YYYY:MM:DD HH:MM:SS");
                    } else {
                        g.timestamp = ts.toStdString();
                    }
                }
            }
            ov.gps = g;
        }
    }

    // R-199：隐私三态读回 itemData（Inherit → 不落值，语义逐字不变）
    const PrivacyMode pmode = privacy_of(privacy_mode);
    if (pmode == PrivacyMode::ForceOn) {
        ov.strip_privacy = true;
    } else if (pmode == PrivacyMode::ForceOff) {
        ov.strip_privacy = false;
    }
    return ov;
}

void ExifEditor::Impl::on_accept() {
    // R-081：后台读取未完成时拒绝 accept —— meta 空基底上 apply_edits 会把其余元数据写丢
    // （数据破坏面）；读完自动恢复可提交（模态下用户重按即可）。
    if (meta_loading) {
        report_errors({"元数据读取中，请稍候再提交"}); // 中文界面口径（§1.4）
        return;
    }
    std::vector<std::string> errors;
    const pp::MetadataOverride ov = build_override(&errors);
    // 校验只作用于 read_metadata 的副本；源文件与源元数据永不修改
    Exiv2::ExifData exif = meta.exif;
    Exiv2::XmpData xmp = meta.xmp;
    pp::apply_edits(exif, xmp, ov.exif_edits, ov.xmp_edits, errors);
    if (!errors.empty()) {
        report_errors(errors);
        return; // 保持对话框打开
    }
    q->accept();
}

// ===========================================================================
// ExifEditor
// ===========================================================================

ExifEditor::ExifEditor(const QString &src, const pp::BatchRules &batch,
                       const std::optional<pp::MetadataOverride> &existing, QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>()) {
    impl_->q = this;
    impl_->src = src;
    impl_->batch = batch;
    impl_->existing = existing;
    impl_->build();

    // R-081（M7-R2.1-F4）：构造期 read_metadata 后台化 —— 大文件/慢盘上同步读会卡 GUI；
    // 读取在 QtConcurrent 后台完成，回填回调里填两棵树 + 应用单文件例外。PP_UI_NO_STATE
    // （smoke/dev harness）下保持同步 —— 探针无等待点，异步会成竞态（R2D1-A2 同口径）。
    const std::filesystem::path path(src.toStdString());
    if (qEnvironmentVariableIsSet("PP_UI_NO_STATE")) {
        impl_->meta = pp::read_metadata(path);
        impl_->populate_exif_tree();
        impl_->populate_xmp_tree();
        impl_->init_from_existing();
        impl_->refresh_effective_values(); // build() 时尚无 meta，读完补刷（生效值回显）
        return;
    }
    impl_->meta_loading = true;
    auto *watcher = new QFutureWatcher<pp::SourceMeta>(this);
    QObject::connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        watcher->deleteLater();
        impl_->meta = watcher->result();
        impl_->meta_loading = false;
        impl_->populate_exif_tree();
        impl_->populate_xmp_tree();
        impl_->init_from_existing();
        impl_->refresh_effective_values(); // build() 时尚无 meta，读完补刷（生效值回显）
    });
    watcher->setFuture(QtConcurrent::run([path] { return pp::read_metadata(path); }));
}

ExifEditor::~ExifEditor() = default;

pp::MetadataOverride ExifEditor::result() const {
    std::vector<std::string> ignored;
    return impl_->build_override(&ignored);
}

} // namespace pp::ui
