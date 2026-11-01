// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M5-T9 / W-G3）— 运行页 v6 化：总览（合成条 + 指标单源 kv + 操作行）/
// 任务进度（表：分组行 + 逐输出缩进行） / 日志尾（等宽 4 行真实尾部）。
//
// 规格 = docs/qt-gui-design-spec.md §7.3（运行页卡表）+ §2.5（数据单源）+ §8（状态四档/空态/
//   tooltip）+ §5 R1–R4（行 pitch 28 / 标签轨 80 / 控件度量）+ §10（Qt 实现映射）+ §11（无障碍）；
//   问题台账 docs/gui-review-0.3.0.md 3-01…3-16 与 K4 裁定（要求改·满足）；
//   开发期裁定 docs/m5-adjudications.md A7（按钮高 26）/ A8（字形禁令：状态一律矢量图标）/
//   A9（RunMarkLabel 的屏幕/DPI 变化失效缓存）/ A29（RunSummary::peak_bytes 加性解冻 →
//   「峰值内存」kv 接**真实读数**，量级换档 B→KB→MB→GB；0 = 未采集 → 「—」）。
//
// v6 口径（逐条对应 K4「运行页数据真实性」）：
//   * **指标单源**（修 3-03/3-04）：总览 kv 只有 §7.3 的 8 项（总耗时/吞吐 · 平均/并行 ·
//     交错启动/线程预算 · 汇总/峰值内存），没有第二个"已用时"；"总耗时"运行中走本页时钟、
//     结束时切到引擎的 `RunSummary::total_ms`（同一指标的两个时刻，不是两个数）。未发生的量
//     显示 "—"（§2.5），数值一律正文对比度（3-04 的"整片浅灰像失效面板"）。
//   * **实现性文案出界面**（修 3-05）：任务卡 hint =「实心=已写盘 · 斜纹=合成中」（mockup 逐字），
//     "阶段权重 + 行级子进度"作废；"交错启动/线程预算/并行"的算法口径（§8.1 步距、§8.2 分配）
//     进 tooltip（§2.6「实现细节进 tooltip」）。
//   * **三级动作层级**（修 3-08 + A39 ②/W-R-T13）：导航（查看日志…/打开输出目录）= accent
//     文字链；破坏性（取消剩余）= **红文字链**（红色表达破坏语义，不再是描边危险钮）；结果
//     （再次运行）= 描边次钮 —— 三者高 26（R4 表 + A7），不再"同一层级两副面孔"。
//   * **0 字节 = 异常**（修 3-10）：完成的输出若 0 字节 → 进度条 = 琥珀（**禁绿色成功条**）、
//     状态标记 = 琥珀 Warn 矢量图标（不再是绿勾）、状态胶囊 = 琥珀「0 字节」、大小列显真实
//     「0 B」（不做 KB 四舍五入 —— 3-10 的"若为四舍五入应显示真实字节"）。
//
// 任务表（§7.3「表格：源文件分组行 + 逐输出缩进行 × 进度条(实心/斜纹) × 大小/耗时(右对齐)
//   × 评分 × 状态胶囊」）：列 = [状态标记 16][名称(剩余宽)][进度 88][大小 56][耗时 44][评分 44]
//   [状态 96] —— **C10/C11（M10-W1）**：删速度列（压缩比迁入大小列 tooltip）、列宽**单源**
//   = `RunTableColumns`（docs/v0.4.0-design.md §5.1；行骨架/表头/探针共引一张表），名称列 =
//   页面宽 − 固定预算（587 → 153px、520 → 86px）、squeeze=true + **ElideMiddle**（中间省略
//   保留扩展名）+ tooltip 完整路径（组行附 宽×高 · 大小；输出行附格式名）。
//   （**评分列** = M8-W2 需求3 加性列：把「评估结果」从输出行下的隐藏子行改成表格一列，逐输出行
//   显示 SSIMULACRA 2 两位小数 + 四档语义色，组行「—」不平均；Off/仅元数据留空，Unavailable/
//   Failed 显示「—」+ tooltip 原因），
//   行高 = R1 的 28（表头 24）；缩进（逐输出行）只作用于名称列（mockup padding-left:18px）。
//   完成态**唯一形态** = formkit::PillBadge 绿胶囊「已完成」（修 3-12：文件行文字"完成"/面板头
//   蓝胶囊/任务行"✓" 三套词汇收敛为 formkit 一处实现）；失败行带「└ 原因：…」行（§7.3 逐字）。
//   **计数单源**（修 3-13）：输出总数只出现在总览卡头 hint「N / M 个输出」，逐文件「N 个输出」
//   作废（逐输出行就在其下）；状态计数只在「汇总」kv。
//
// 日志尾卡（§7.3「等宽字体 4 行真实尾部（warn/err 着色）+ 查看完整日志…」）：等宽（Consolas）
//   4 行、warn/err 整行着色；**有日志才显示日志名**（修 3-14：面板头显示 run-*.log 而内容却是
//   "尚无日志"的自相矛盾）；空态文案去括号（修 3-15），卡体按 4 行信息量收紧（修 3-16）。
//
// 三级进度（§7.1）与事件折算口径**语义零改动**（M4-T14 落地，T9 只改呈现）：
//   * 运行级 = Σ 输出完成度 / (文件数 × 格式数)；输出完成度 = (0.5·enc + 0.1·meta) / 0.6
//     （= core/progress.cpp `ProgressMux::output_frac()` 的归一），终态输出按"已结算"计 1.0；
//   * 源文件级（分组行）= 共享段（probe/decode/orient/color，§7.2 只算一次）加权；
//   * 输出级（子行）= `shared + 0.5·enc_i + 0.1·meta_i`；合成进度（§7.3）= 斜纹（115°/6px/12px）。
//
// 合成条（总览「进度」）= `RunProgressBar`：QProgressBar 子类，**无障碍角色保持 progressbar、
//   值 = 显示的百分比**（修 3-07：不再把 0..N×1000 的内部量暴露给辅助技术），自绘分段
//   （完成=绿 / 0 字节=琥珀 / 失败=红 / 其余终态=中性 / 进行中=强调）——与 mockup
//   `.pbar i.done/i.warn/i.fail` 同语义。
//
// 节流（§7.4 / M10-W1-C15，T9a 起）＝**双通道 flush**：事件回调只写内存行状态并置脏位；
//   状态翻转（阶段切换/评分开始/完成/跳过/终态）→ `dirty_state` 由 QTimer::singleShot(0)
//   立即刷一帧；数值/百分比 → `dirty_progress` 由 60ms 定时器合并（行控件对同值早退）。
//   **dirty-row**：`RunState::row_dirty` 逐文件标脏，flush 只渲染脏文件 + 总览（O(脏行) 而非
//   O(总行数)）；begin_run/reset/end_run/set_tokens 全量。运行期锁定（G5）：本页运行中只有
//   「取消剩余」可交互（**唯一取消入口**不变）；跳转入口（查看日志…/打开输出目录）保持可用；
//   结束态取消钮消失。左栏/步骤钮/设置/预设/预览/分类的置灰在 `MainWindow::lock_for_run()`
//   （T9 零改动）。
//
// 状态机（M10-W1-C12；T9a = 现有事件面，T9b = OutputDone 消费；§5.3）：事件面 = ProgressMux
//   的 Progress 事件 + 引擎逐输出终态事件 `FileState::OutputDone`（`FileEvent::output`，
//   C13/C14）：
//   * 格式行=「编码 NN%」（行数进 tooltip「真实行级 · R/H 行」；合成=斜纹
//   +「合成进度（编码器无回调）」）；
//     Encode 1.0（on_output_encode_done 强制点）且无 MetaWrite →「评分中」（Accent + 斜纹）；
//     MetaWrite 0 →「写入」；MetaWrite 1.0（强制点）→「已完成」；首事件即 MetaWrite 1.0（无 Encode
//     段，on_output_skipped）→「跳过」。
//   * **OutputDone（T9b）**：到达即结算该输出行（状态 Done/Failed/Skipped/Cancelled + 大小/耗时/
//     压缩比 tooltip/失败原因/warnings，不等文件终态）。首次事件 metric=Off：行「已完成」 +
//     评分列「评分中」（仅本批启用评分时；否则留空）；第二次事件（含 metric）→ 同帧填两位小数
//     + 既有四档色阶。文件终态事件只做**幂等校准**且不得用其 metric Off 覆盖已显示分数
//     （引擎异步评分 → 终态 FileResult 的 metric 恒 Off；引擎移交事项①）。
//   * 组行 Running 聚合 = 「编码中 k/n」/「评分中 k/n」/「写入中 k/n」（k=已结算输出数；
//     已完成/跳过中间态计入 k）—— 共享段（探测/解码→色彩/旋转/合成）只在**真正进行中**显示
//     （无输出已启动时），不再回显过期「色彩」。
//
// 页内状态保存在本翻译单元的注册表（键 = PageRun*，随 QObject::destroyed 释放）—— page_run.h
// 是 PP-FROZEN 头文件（0.3.0 追加面：T14 的 RunPlan/读数 + T9 的 `run_again_requested()`），
// 不含私有成员（§2.13）。本文件无 Q_OBJECT 类（行内控件不发信号），故不含 *.moc 包含
// （与 page_meta.cpp / page_output.cpp 同口径）。
#include "ui/page_run.h"
#include "core/error_text.h" // R-070/273：失败原因行中文映射单源（M7-R2B；本包接线）
#include "ui/contextmenu.h"  // UX-1：公共右键菜单件（一卡一 actions 构造函数）
#include "ui/term_words.h"   // R-139：终态词表 term_words 单源（列表/运行页/状态栏共用一张）

#include <algorithm>
#include <cmath>
#include <map> // R-185：注册表 pimpl（与 page_output 同款 std::map + unique_ptr）
#include <memory>
#include <thread>

#include <QApplication> // theme.h 的 style_mirrors_system_accent() 用 qApp->style()
#include <QClipboard>   // UX-1：「复制日志 / 复制失败原因」
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu> // UX-1：卡片右键菜单
#include <QPainter>
#include <QPen>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QtGlobal>
#include <cstdint>
#include <vector>

#include "core/pipeline.h"      // M8-C2：OutputResult 的加性评分字段（MetricState）
#include "core/progress.h"      // StageWeights（§7.2 单源）
#include "ui/formkit.h"         // M5-T9：行构造器（C2）+ 胶囊/进度条（C8）/令牌（C1）
#include "ui/icons.h"           // M5-T3：状态标记/取消钮 ■ 一律矢量图标（C3）
#include "ui/leading_indent.h"  // R-218（M7-R2E2）：逻辑边距（行首缩进）单源
#include "ui/run_rows_model.h"  // M7-WP9（B.2）：行模型纯语义（runpage）
#include "ui/run_status_text.h" // M7-WP9（B.2）：文案/数值格式纯函数（runpage）
#include "ui/theme.h"

namespace pp::ui {
namespace {

using pp::ProgressInfo;
using pp::Stage;
using pp::StageWeights;

namespace fk = formkit;

constexpr int kFlushMs = 60;     // §7.4：GUI 节流 60ms 刷新
constexpr int kLogTailLines = 4; // §7.3：日志尾 4 行（warn/err 着色）

// ---------------------------------------------------------------------------
// 任务表列宽模型（C10/C11；**单源**，docs/v0.4.0-design.md §5.1）—— 行骨架 / 表头 / 探针
// 共引一份。名称列 = 页面宽 − 固定预算：扣竖滚动条（常驻）+ 卡体内距 ×2 + 行内距 ×2 +
// 固定列之和 + 6 个列距。587 → 153px（探针阈值 ≥150）；520 → 86px（≥80）。
// 本包回收：删速度列 54px + 状态 132→96 回收 36px + 大小/耗时各 −4px → 名称列 55 → 153px。
// ---------------------------------------------------------------------------
struct RunTableColumns {
    static constexpr int mark = 16;            // 状态标记盒宽（矢量图标 12 居中）
    static constexpr int bar = 88;             // 进度列
    static constexpr int size = 56;            // 大小列（原 60；压缩比迁入本列 tooltip，C11）
    static constexpr int time = 44;            // 耗时列（原 48）
    static constexpr int score = 44;           // 评分列（M8-W2 需求3：SSIMULACRA 2）
    static constexpr int status = 96;          // 状态列（原 132；§5.1 收缩）
    static constexpr int gap = fk::kInlineGap; // 列距 6（= formkit 行内距）
    static constexpr int pad_x = theme::Metrics::card_pad_x; // 卡体内距/行内距 12（同值）
    static constexpr int scroll = theme::Metrics::scroll_w;  // 竖滚动条宽 6（常驻）
    static constexpr int fixed() { return mark + bar + size + time + score + status; }
    // 名称列宽（页面宽口径）：行骨架里名称列是**唯一 stretch 项** → 实际宽 = 余量 = 此式。
    static constexpr int name_width(int page_w) {
        return std::max(0, page_w - scroll - 2 * pad_x - 2 * pad_x - fixed() - gap * 6);
    }
};
// 验收阈值进编译期（§12.2 name_w：587 ≥150 / 520 ≥80）—— 列宽模型改动不得跌破验收线。
static_assert(RunTableColumns::name_width(theme::Metrics::right_width) >= 150,
              "docs/v0.4.0-design.md §5.1：587 宽页面名称列 ≥150px");
static_assert(RunTableColumns::name_width(theme::Metrics::right_width_min) >= 80,
              "docs/v0.4.0-design.md §5.1：520 宽页面名称列 ≥80px");
static_assert(RunTableColumns::gap == 6 && RunTableColumns::scroll == 6, "列距/滚动条宽 6");

constexpr int kMarkPx = 12;                      // 标记图标边长
constexpr int kCellGap = RunTableColumns::gap;   // 表内列距（探针 run-geom 按同一张表取证）
constexpr int kRowPadX = RunTableColumns::pad_x; // 表左右内距（UX-6④：= 卡内边距 12）
constexpr int kOutIndent = 18;                   // 逐输出行名称缩进（mockup padding-left:18px）
constexpr int kReasonIndent = 36;                // 失败原因行缩进（缩进行之下再一级）
constexpr int kTableHeadH = fk::kTableHeadH; // 表头行高（UX-6①：§5 R1 唯一例外，表头不是信息行）

// 页内动态属性（页面 QSS 的锚；只锚本页控件，不误伤它页）
constexpr const char *kRunLabelProperty = "ppRun";
constexpr const char *kRunHead = "thead";      // 表头/空态
constexpr const char *kRunGName = "gname";     // 分组行名称
constexpr const char *kRunOName = "oname";     // 逐输出行名称
constexpr const char *kRunReason = "reason";   // 失败原因行
constexpr const char *kRunSummary = "summary"; // 卡底摘要句

// ===========================================================================
// 小控件（页内；不进头文件）
// ===========================================================================

// 单行省略标签（§9.3「全界面禁止文案折行」；与 page_meta/page_output 同口径）
// R-020/R-320（M7-WP4）：ElidedLabel 的 7 份逐字拷贝收敛到 formkit 唯一实现
// （本文件原 30+ 行自拷类删除；行为逐位等价 —— 全文 + ppFullText + tooltip + 省略号）。
using ElidedLabel = formkit::ElidedLabel;

// R-218（M7-R2E2）：行首缩进 = 逻辑边距（leading 在左）—— 单源在 ui/leading_indent.h
// （LTR 落左 / RTL 落右 + LayoutDirectionChange 重放；LTR 初值逐字节不变）。

// 总览合成条（§7.3「进度(合成条)」）：QProgressBar 子类 —— **无障碍角色仍是 progressbar、
// 暴露值 = 显示的百分比**（修 3-07），自绘分段（mockup `.pbar i.done|i.warn|i.fail|i.comp`）。
// 段占比按"每输出一格"折算（与运行级完成度同一分母），进行中段 = 剩余部分。
class RunProgressBar : public QProgressBar {
public:
    explicit RunProgressBar(QWidget *parent = nullptr) : QProgressBar(parent) {
        setTextVisible(false);
        setFixedHeight(theme::Metrics::progress_h); // R4 进度条高 6
        setRange(0, 100);                           // 值域 = 百分比（与显示字面量一致）
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        update();
    }
    // 五段占比（0..1；和 ≤ 1，余量 = 轨道）：完成 / 0 字节异常 / 失败 / 其余终态 / 进行中
    void set_segments(double done, double warn, double failed, double other, double active) {
        if (done == done_ && warn == warn_ && failed == failed_ && other == other_ &&
            active == active_) {
            return; // 同值早退
        }
        done_ = done;
        warn_ = warn;
        failed_ = failed;
        other_ = other;
        active_ = active;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::Tokens &t = tokens_;
        const qreal r = height() / 2.0; // R4 r99 → 半高
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(t.control_bd, theme::Metrics::progress_border_px));
        p.setBrush(t.control);
        p.drawRoundedRect(box, r, r);
        const qreal inset = theme::Metrics::progress_border_px;
        const QRectF track = box.adjusted(inset, inset, -inset, -inset);
        if (track.width() <= 0.0 || track.height() <= 0.0)
            return;
        p.setPen(Qt::NoPen);
        const double seg[5] = {done_, warn_, failed_, other_, active_};
        const QColor colors[5] = {t.ok, t.warn, t.err, t.text3, t.accent};
        // R-218（M7-R2E2）：段从行首（leading）铺起 —— LTR=左缘、RTL=右缘；LTR 路径逐字节不变。
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        qreal x = rtl ? track.right() : track.left();
        for (int i = 0; i < 5; ++i) {
            const qreal w = track.width() * std::clamp(seg[i], 0.0, 1.0);
            if (w <= 0.0)
                continue;
            p.setBrush(colors[i]);
            p.drawRect(QRectF(rtl ? x - w : x, track.top(), w, track.height()));
            x += rtl ? -w : w;
        }
    }

private:
    theme::Tokens tokens_{};
    double done_ = 0.0, warn_ = 0.0, failed_ = 0.0, other_ = 0.0, active_ = 0.0;
};

// 状态标记位点（16px 盒，固定尺寸无省略需求 → 普通 QLabel + 矢量 pixmap）：
// ElidedLabel 的 resizeEvent 会 setText（清掉 pixmap），故标记不能用它；同 (图标, 色) 早退，
// 避免 60ms flush 的高频路径反复光栅化。
// A9：屏幕/DPI 变化（跨屏拖动、缩放变更）后缓存 pixmap 的 devicePixelRatio 已过期 →
// 事件当拍起失效，下一拍（dpr 已是新屏的值）按新 dpr 重光栅化（否则标记发虚/糊）。
class RunMarkLabel : public QLabel {
public:
    explicit RunMarkLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setFixedWidth(RunTableColumns::mark);
        setAlignment(Qt::AlignCenter);
    }
    void set_mark(icons::Name name, const QColor &color) {
        if (name == name_ && color == color_ && !pixmap().isNull())
            return;
        name_ = name;
        color_ = color;
        refresh();
    }
    // R-113（M7-WP4）：死访问器 mark_name() 已删（全仓零调用）

protected:
    bool event(QEvent *e) override {
        // Qt 6 里"屏幕变化"事件 = ScreenChangeInternal（A9 的 QEvent::ScreenChange 即其公开语义）；
        // DevicePixelRatioChange 覆盖不换屏也改变 dpr 的场景（系统缩放在另一显示器上变更）。
        const QEvent::Type type = e->type();
        if (type == QEvent::ScreenChangeInternal || type == QEvent::DevicePixelRatioChange) {
            QTimer::singleShot(0, this, [this]() { refresh(); }); // 下一拍：dpr 已更新
        }
        return QLabel::event(e);
    }
    void paintEvent(QPaintEvent *event) override {
        // 兜底（A9）：绘制前自查 dpr —— 即便平台未投递 ScreenChange 类事件，也保证缓存 pixmap
        // 始终按当前屏的 dpr 光栅化（跨屏拖动/系统缩放变更后不残留旧 dpr 的模糊图标）。
        if (!pixmap().isNull() && dpr_ != devicePixelRatioF())
            refresh();
        QLabel::paintEvent(event);
    }

private:
    void refresh() {
        dpr_ = devicePixelRatioF();
        setPixmap(icons::pixmap(name_, kMarkPx, color_, dpr_));
    }
    icons::Name name_ = icons::Name::Pending;
    QColor color_;
    qreal dpr_ = 0.0; // 缓存 pixmap 的光栅化 dpr（A9 的失效判据）
};

// 日志尾（§7.3「等宽字体 4 行真实尾部（warn/err 着色）」）：等宽字族（Consolas，令牌单源）+
// warn/err 整行着色 + 逐行省略号（§9.3 不折行）；宽度变化后重渲染以保持省略点正确。
class LogLines : public QLabel {
public:
    explicit LogLines(QWidget *parent = nullptr) : QLabel(parent) {
        setTextFormat(Qt::RichText);
        setWordWrap(false);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setFont(mono_font(theme::Typography::mono_px));
        // 2026-10 修复：日志行很长且 setWordWrap(false)，QLabel 的 sizeHint 会按**整行原文**
        // 计算宽度 → 运行页右栏被日志卡撑出窗口（bug1 截图右侧列被裁）。日志尾是省略号
        // 单行展示，宽度应由布局分配，不能反过来决定父卡宽度。
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

    QSize sizeHint() const override {
        QSize hint = QLabel::sizeHint();
        hint.setWidth(std::min(hint.width(), 320));
        return hint;
    }
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }
    void set_tokens(const theme::Tokens &t) {
        tokens_ = t;
        render();
    }
    void set_lines(const QStringList &lines) {
        if (lines_ == lines)
            return;
        lines_ = lines;
        render();
    }
    QStringList lines() const { return lines_; }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        render();
    }

private:
    // 日志尾等宽字族（§7.3；族名 = Typography::family_mono，本页不写死）
    static QFont mono_font(double px) {
        QFont f = theme::font(px);
        f.setFamilies({QString::fromLatin1(theme::Typography::family_mono), QStringLiteral("Menlo"),
                       QStringLiteral("Courier New")});
        return f;
    }

    void render() {
        if (lines_.isEmpty()) {
            // 空态：去括号的自然语（修 3-15；此时面板头也不显示日志名 —— 见 set_log_tail）
            setText(span(tokens_.text3, PageRun::tr("尚无日志 · 开始运行后在此显示最新记录")));
            return;
        }
        QStringList html;
        html.reserve(lines_.size());
        for (const QString &raw : lines_) {
            const QString shown =
                width() > 0 ? fontMetrics().elidedText(raw, Qt::ElideRight, std::max(0, width()))
                            : fontMetrics().elidedText(raw, Qt::ElideRight, 300);
            html << colorize(shown);
        }
        setText(html.join(QStringLiteral("<br>")));
    }

    // 行形态（core/logger.cpp 的 `%H:%M:%S.%e [%l] [%t] %v`）：
    //   * warn 行 → 整行琥珀；err/critical 行 → 整行红（§7.3「warn/err 着色」）；
    //   * 其余 → 时间戳 txt3 + 级别 acc + 正文 txt2。
    QString colorize(const QString &line) const {
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0)
            return span(tokens_.text2, line);
        const QString stamp = line.left(space);
        QString rest = line.mid(space + 1);
        QString level;
        if (rest.startsWith(QLatin1Char('['))) {
            const int close = rest.indexOf(QStringLiteral("] "));
            if (close > 0) {
                level = rest.mid(1, close - 1);
                rest = rest.mid(close + 2);
            }
        }
        const QString lower = level.toLower();
        if (lower.contains(QStringLiteral("warn")))
            return span(tokens_.warn, line);
        if (lower.contains(QStringLiteral("err")) || lower.contains(QStringLiteral("crit")))
            return span(tokens_.err, line);
        if (level.isEmpty())
            return span(tokens_.text3, stamp) + span(tokens_.text2, rest);
        return span(tokens_.text3, stamp) + QStringLiteral(" ") +
               span(tokens_.accent, QStringLiteral("[") + level + QStringLiteral("] ")) +
               span(tokens_.text2, rest);
    }

    static QString span(const QColor &color, const QString &text) {
        return QStringLiteral("<span style=\"color:%1\">%2</span>")
            .arg(theme::css_color(color), text.toHtmlEscaped());
    }

    theme::Tokens tokens_{};
    QStringList lines_;
};

// ===========================================================================
// §7.2 阶段算术 / 文案词汇表（M4-T14 语义零改动）
// ===========================================================================

// 共享段（源文件级，一次）的单阶段权重；非共享阶段 → 0（§7.2 前三行）
float shared_weight(Stage s) {
    switch (s) {
    case Stage::Probe:
        return StageWeights::probe_acquire;
    case Stage::Decode:
        return StageWeights::decode;
    case Stage::Orient:
        return StageWeights::orient;
    case Stage::Color:
        return StageWeights::color;
    default:
        return 0.f;
    }
}

// M7-WP9（B.2 run_status_text / run_rows_model）：阶段文案与数值格式族、终态判定抽入
// ui/run_status_text.h + ui/run_rows_model.h（可单测单元，R-109）；此处 using/转发，
// 调用点零改动（转发 = 冻结面「加性 + 追认」的旧签名保留形态）。
using runpage::fmt_bytes;
using runpage::fmt_duration;
using runpage::fmt_mb_s;
using runpage::fmt_mem_bytes;
using runpage::fmt_secs;
using runpage::format_name;
using runpage::is_terminal_state;
using runpage::row_name;
using runpage::shared_stage_text;
using runpage::write_stage_text;

int hardware_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 0 ? static_cast<int>(hw) : 1;
}

// is_terminal_state / format_name 已抽入 ui/run_status_text.h（见上方 using）

// 行状态（结算判定单源 = runpage::OutKind / runpage::settled_kind；ui/run_rows_model.h）
using runpage::OutKind;
using runpage::settled_kind;

// 数值列缓存（文本 + 色；§4.2 数值档 = 右对齐 + tabular）
struct CellCache {
    QString text;
    QColor color;
    void apply(ElidedLabel *label, const QString &value, const QColor &tint) {
        if (text != value) {
            text = value;
            label->set_full_text(value);
        }
        if (!(color.isValid() && color == tint)) {
            color = tint;
            label->setStyleSheet(QStringLiteral("color:%1").arg(theme::css_color(tint)));
        }
    }
};

// R-187（M7-R2.1-F3）：行控件束单源 —— 组行（FileRow）与逐输出行（OutRow）原各带一份逐字
// 同款的「7 控件 + 数值缓存 + 应用缓存」束（deep-review R-187/E3-08「双 struct 双份控件
// 束」）。收敛为唯一 RowBundle，两 struct 嵌入（既有成员名逐字可用，消费点零改动；仅组行
// 数值缓存命名归一 size_cell → size_cache 3 处）。行容器不入束（组行叫 group、输出行叫
// row），失败原因行是输出行专属。
struct RowBundle {
    RunMarkLabel *mark = nullptr;
    ElidedLabel *name = nullptr;
    fk::ProgressStrip *bar = nullptr;
    ElidedLabel *size = nullptr;
    ElidedLabel *time = nullptr;
    // M8-W2（需求3）：评分列（任务表第 6 列「评分」）—— 定宽单元格 + 右对齐数值文本。
    // 旧「输出行下隐藏子行」形态退役（C12 由本需求改列为 C12′）；单元格/文本沿用旧探针钩子
    // `pp-run-score-<i>-<j>` / `pp-run-score-text-<i>-<j>`（组行 = `pp-run-gscore-<i>` 系）。
    QWidget *score_cell = nullptr;
    ElidedLabel *score = nullptr;
    fk::PillBadge *status = nullptr;
    // 数值列缓存（文本 + 色；§4.2 数值档 = 右对齐 + tabular）；M10-W1 删速度列 → 无 speed_cache
    CellCache size_cache, time_cache, score_cache;
    // 已应用缓存（避免同值重写触发 style/repaint）
    QString applied_status;
    fk::PillBadge::Tone applied_tone = fk::PillBadge::Tone::Neutral;
    bool applied_icon_set = false;
    icons::Name applied_icon = icons::Name::Pending;
};

struct OutRow : RowBundle {
    QWidget *row = nullptr;
    QWidget *reason_row = nullptr; // 失败行的「└ 原因：…」行（默认隐藏）
    ElidedLabel *reason = nullptr;
    QWidget *warn_row = nullptr; // 成功但带 warning 的「⚠ …」行（默认隐藏；独立 objectName）
    ElidedLabel *warn = nullptr;
    QString applied_warning;
    bool warn_shown = false;
    // —— M8-W2（需求3）：评分终态事实（从 OutputResult 的加性字段复制；文件组行不做平均）——
    // 评分呈现 = 任务表「评分」列（RowBundle::score_cell/score），不再有隐藏子行。
    pp::MetricState metric_state = pp::MetricState::Off;
    double metric_score = -1.0;
    QString metric_note;
    // 进度（§7.2 段内 frac）
    float enc = 0.f, meta = 0.f;
    bool started = false, synthetic = false;
    Stage last_stage = Stage::Probe;
    bool has_stage = false;
    int rows_now = 0; // 行级回调折算的当前行（progress_max_row 同口径）
    // ---- M10-W1（T9a；C12 §5.3）现有事件面的状态机中间态（不依赖 W1-T7 新事件）----
    // 事件序列真源 = core/progress.cpp ProgressMux：正常 = Encode 0/…/Encode 1.0（强制）→
    // MetaWrite 0（写入）→ MetaWrite 1.0（强制）；冲突跳过 = **首个输出事件即 MetaWrite 1.0**
    // （on_output_skipped，encode 段不单独发射）；仅元数据 = MetaWrite 0
    // 起（on_output_no_encode）。
    bool got_output_event = false; // 是否收到过本输出的 phase 事件（「首事件」判定）
    bool seen_encode = false;      // 是否进入过 Encode 段（跳过判定：无 Encode 段）
    bool score_pending = false;    // Encode frac>=1.0 且尚无 MetaWrite → 「评分中」
    bool meta_done_seen = false;   // MetaWrite frac=1.0 → 「已完成」（终端事件前的中间态）
    bool skip_seen = false;        // 首事件即 MetaWrite 1.0（无 Encode 段）→ 「跳过」
    // M10-W1（T9b；C14/§5.3）：首次 OutputDone（metric Off）后评分列显示「评分中」，等评分池
    // 补发的第二次 OutputDone（含 metric）填分。仅「本批启用评分 + 该行成功」时置位；
    // 收到任何非 Off 的 metric（OutputDone 或终态兜底）即清零。
    bool score_wait = false;
    // 终态事实（字节 · 压缩比 · 耗时；失败原因）
    OutKind kind = OutKind::Queued;
    qint64 bytes = 0;
    double ratio = 0.0, ms = 0.0;
    QString error;
    QString warnings; // 非失败行的降档/丢元数据/拍平等告警（第二轮：warning 必须有 UI 出口）
    QString applied_reason;
    bool reason_shown = false;
};

struct FileRow : RowBundle {
    QWidget *group = nullptr;
    QVector<OutRow> outs;
    int index = -1; // 注册表下标（C15：dirty-row 行脏标记的键）
    // 共享段（§7.2 前三行；一次）
    float shared_probe = 0.f, shared_decode = 0.f, shared_orient = 0.f, shared_color = 0.f;
    Stage shared_stage = Stage::Probe;
    bool started = false, terminal = false;
    pp::FileState state = pp::FileState::Queued;
    int width = 0, height = 0;
    qint64 src_bytes = 0;
};

// R-187（M7-R2.1-F3）：OutKind（逐输出）/ pp::FileState（逐文件）平行状态机的**互译单点**
// （原 apply_terminal 内联 switch；deep-review R-187/E3-08）。映射真值表：
//   FileState::Done → Done｜Skipped → Skipped｜Cancelled → Cancelled｜
//   其余（Failed/DoneWithErrors/中途异常等一切未列值）→ Failed。
OutKind out_kind_of_file_state(pp::FileState state) {
    switch (state) {
    case pp::FileState::Done:
        return OutKind::Done;
    case pp::FileState::Skipped:
        return OutKind::Skipped;
    case pp::FileState::Cancelled:
        return OutKind::Cancelled;
    default:
        return OutKind::Failed;
    }
}

struct RunState {
    PageRun *page = nullptr;
    theme::Tokens tokens{};
    PageRun::RunPlan plan;

    // 总览卡（§7.3 第一行：进度合成条 + 百分比；kv 4 行；操作行；摘要句）
    QFrame *overview = nullptr;
    fk::PillBadge *state_pill = nullptr;
    ElidedLabel *progress_hint = nullptr;
    RunProgressBar *progress = nullptr;
    ElidedLabel *percent = nullptr;
    ElidedLabel *elapsed = nullptr;    // 总耗时（**唯一**时间指标；无「已用时」第二出处）
    ElidedLabel *throughput = nullptr; // 吞吐
    ElidedLabel *average = nullptr;    // 平均
    ElidedLabel *workers = nullptr;    // 并行（进行中 / 可并行）
    ElidedLabel *stagger = nullptr;    // 交错启动
    ElidedLabel *budget = nullptr;     // 线程预算
    ElidedLabel *tally = nullptr;      // 汇总（成功/异常/失败/跳过/取消）
    ElidedLabel *memory = nullptr;     // 峰值内存
    ElidedLabel *threads = nullptr;    // 峰值线程（R-078：threads_total_est 估算口径）
    QPushButton *open_logs = nullptr;
    QPushButton *open_output = nullptr;
    QPushButton *cancel = nullptr;
    QPushButton *run_again = nullptr;
    ElidedLabel *summary = nullptr;
    QString applied_state_text;
    fk::PillBadge::Tone applied_state_tone = fk::PillBadge::Tone::Neutral;
    bool applied_state_icon_set = false;
    icons::Name applied_state_icon = icons::Name::Pending;
    bool state_pill_icon = false; // 当前状态胶囊是否已带图标（PillBadge 的图标不可撤）

    // 任务进度卡（表头 + 分组行/逐输出行）
    QFrame *tasks = nullptr;
    ElidedLabel *tasks_hint = nullptr;
    ElidedLabel *task_empty = nullptr;
    QWidget *table_head = nullptr;
    QWidget *list = nullptr;
    QVBoxLayout *list_layout = nullptr;

    // 日志尾卡
    QFrame *logs = nullptr;
    ElidedLabel *log_name = nullptr;
    QPushButton *open_full_log = nullptr;
    LogLines *log_lines = nullptr;
    QStringList log_tail;
    QString log_name_text;

    // 行
    QVector<FileRow> files;

    // 运行态
    bool running = false, finished = false, cancel_requested = false;
    int formats = 1;
    std::size_t terminal_files = 0, started_files = 0;
    std::uint64_t out_bytes = 0;
    QString out_root;
    QElapsedTimer clock;
    QTimer *flush = nullptr;
    // ---- C15（T9a，§5.4）：双通道 flush + dirty-row 渲染 ----
    // dirty_state  = 状态翻转（阶段切换/评分开始/输出完成/评分到达/终态）→ singleShot(0) 立即刷；
    // dirty_progress = 数值/百分比 → 60ms 合并（flush 定时器）；两者的行级落点 = row_dirty。
    bool dirty_progress = false;
    bool dirty_state = false;
    bool state_flush_pending = false; // 已排程一次 singleShot(0)（避免同拍重复排程）
    bool overview_dirty = false;      // 总览卡读数（两通道共用）
    std::vector<bool> row_dirty;      // 逐文件行脏位（与 files 同长；flush 只渲染脏文件）
    // 终态读数（end_run 的引擎汇总；未结束 → 显示走实时折算）
    bool has_summary = false;
    double summary_total_ms = 0.0, summary_mb_s = 0.0, summary_avg_ms = 0.0;
    quint64 summary_peak_bytes = 0; // A29：RunSummary::peak_bytes（像素预算峰值，单源）
    int summary_threads_est = 0;    // R-078：RunSummary::threads_total_est（峰值活跃线程估算）
};

// C15（T9a）：双通道 flush / dirty-row 的标记入口（定义在渲染段之后；apply_progress /
// apply_terminal 与 PageRun 成员函数使用）。contract：file_index = FileRow::index；-1 = 只刷总览。
void mark_progress(RunState &s, int file_index);
void mark_state(RunState &s, int file_index);

// R-185（M7-R2.1-F3）：注册表 pimpl 两套解法统一为 RAII —— 原 page_run 走 QHash<裸指针> +
// 裸 new + destroyed 信号手动 delete/take，page_output 走 unique_ptr（deep-review R-185/E3-06
// 「同一注册表 pimpl 两套解法」）。现两页同款：std::map<页指针, std::unique_ptr<State>> +
// 访问器 *_of() + **析构 erase**（所有权归注册表，无手动 delete）。
// 指针键注册表：析构擦除 → 无悬挂/别名；GUI 线程独占，无需锁。
std::map<const PageRun *, std::unique_ptr<RunState>> &registry() {
    static std::map<const PageRun *, std::unique_ptr<RunState>> store;
    return store;
}

RunState *state_of(const PageRun *page) {
    const auto it = registry().find(page);
    return it == registry().end() ? nullptr : it->second.get();
}

// 0 字节完成的输出 = **异常**（§8/§7.3；修 3-10）：条/标记/胶囊一律琥珀，绝不落绿色成功态
// —— 判定单源 = runpage::out_zero_bytes（ui/run_rows_model.h）。
using runpage::out_zero_bytes;

bool out_zero_bytes(const OutRow &out) { return runpage::out_zero_bytes(out.kind, out.bytes); }

// 状态四档（§8）：标记色与状态词同档（"状态点与状态词同色"）
QColor tone_color(const RunState &s, fk::PillBadge::Tone tone) {
    switch (tone) {
    case fk::PillBadge::Tone::Ok:
        return s.tokens.ok;
    case fk::PillBadge::Tone::Warn:
        return s.tokens.warn;
    case fk::PillBadge::Tone::Err:
        return s.tokens.err;
    case fk::PillBadge::Tone::Accent:
        return s.tokens.accent;
    case fk::PillBadge::Tone::Neutral:
        break;
    }
    return s.tokens.text3;
}

// ===========================================================================
// 进度读数（§7.1/§7.2）
// ===========================================================================

float shared_frac(const FileRow &file) {
    return std::clamp(file.shared_probe + file.shared_decode + file.shared_orient +
                          file.shared_color,
                      0.f, StageWeights::shared);
}

// 输出行填色（§7.2：共享段一次 + 该输出 50% encode + 10% metawrite）
float out_row_frac(const FileRow &file, const OutRow &out) {
    return std::clamp(shared_frac(file) + StageWeights::encode * out.enc +
                          StageWeights::metawrite * out.meta,
                      0.f, 1.f);
}

// §7.1 运行级完成度：Σ 输出完成度 / (文件数 × 格式数)。折算单源 =
// runpage::out_completion / runpage::run_frac（ui/run_rows_model.h，可单测）。
double out_completion(const OutRow &out) {
    return runpage::out_completion(settled_kind(out.kind), out.enc, out.meta);
}

double run_frac(const RunState &s) {
    double sum = 0.0;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs)
            sum += out_completion(out);
    }
    return runpage::run_frac(static_cast<int>(s.files.size()), s.formats, sum);
}

int settled_outputs(const RunState &s) {
    int n = 0;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs) {
            if (settled_kind(out.kind))
                ++n;
        }
    }
    return n;
}

bool any_output_started(const FileRow &file) {
    for (const OutRow &out : file.outs) {
        if (out.started)
            return true;
    }
    return false;
}

int first_unstarted(const FileRow &file) {
    for (int j = 0; j < file.outs.size(); ++j) {
        if (!file.outs.at(j).started)
            return j;
    }
    return -1;
}

// ---- 状态文案（§8 四档；完成态唯一形态 = 绿胶囊「已完成」，修 3-12）----

struct StatusText {
    QString text;
    fk::PillBadge::Tone tone = fk::PillBadge::Tone::Neutral;
    icons::Name icon = icons::Name::Pending;
    bool has_icon = false;
    QString tip; // A39 ③：排队 pill 的「交错启动」语义（§2.6：实现细节进 tooltip）
};

// 排队 pill 的 tooltip（W-R-T13 / A39 ③）：pill 文案收「排队」（mockup run.html 的 .pill 逐字），
// 「交错启动」的实现语义移到这里（§2.6 实现细节进 tooltip）；文案随本批次的交错步距。
QString queue_tip(const RunState &s) {
    return s.plan.stagger_ms > 0
               ? PageRun::tr("等待启动：交错启动，每 %1 ms 才启动下一个文件").arg(s.plan.stagger_ms)
               : PageRun::tr("等待启动：前序输出完成后开始");
}

// C12（T9a，§5.3）：格式行主文案 = 「编码 NN%」（阶段内百分比；合成/真实同文案）。
// 行数不再是主文案 —— 真实行级的 R/H 行进 tooltip（out_stage_tip）；合成进度另有斜纹 + tooltip。
QString out_encode_percent_text(const OutRow &out) {
    const int pct = static_cast<int>(std::lround(std::clamp(out.enc, 0.f, 1.f) * 100.f));
    return PageRun::tr("编码 %1%").arg(pct);
}

// 输出行阶段文字（未启动/进行中：排队、共享段承载、编码百分比、合成、写入）
QString out_stage_text(const FileRow &file, const RunState &s, int index) {
    const OutRow &out = file.outs.at(index);
    if (!out.started) {
        // 尚未启动：文件处于共享段且**无输出已启动** → 首个未完成行承载共享段（§7.2 一次）
        if (index == first_unstarted(file) && !any_output_started(file) && !file.terminal) {
            if (file.started)
                return shared_stage_text(file.shared_stage);
            return PageRun::tr("排队"); // A39 ③：pill 收「排队」（交错语义进 tooltip）
        }
        return PageRun::tr("排队"); // A39 ③
    }
    if (out.has_stage && out.last_stage == Stage::MetaWrite)
        return write_stage_text(Stage::MetaWrite);
    if (out.has_stage && out.last_stage == Stage::Flatten)
        return write_stage_text(Stage::Flatten);
    if (s.plan.metadata_only)
        return PageRun::tr("写入");      // 仅元数据模式：同容器零重编码（§3.2/D7）→ 无编码段
    return out_encode_percent_text(out); // §5.3：合成/真实同文案（差异只进 tooltip + 斜纹）
}

// 格式行阶段 tooltip（§5.3）：真实行级 → 「真实行级 · R/H 行」；合成 →
// 「合成进度（编码器无回调）」。
QString out_stage_tip(const FileRow &file, const OutRow &out) {
    if (out.synthetic)
        return PageRun::tr("合成进度（编码器无回调）"); // §7.3 斜纹条口径
    if (file.height > 0)
        return PageRun::tr("真实行级 · %1/%2 行").arg(out.rows_now).arg(file.height);
    return PageRun::tr("真实行级进度（本文件无行数读数）");
}

StatusText out_status(const FileRow &file, const RunState &s, int index) {
    const OutRow &out = file.outs.at(index);
    switch (out.kind) { // 终态（终端事件定稿）：形态/文案零改动
    case OutKind::Done:
        if (out_zero_bytes(out))
            return {PageRun::tr(pp::ui::term_words::kZeroBytes), fk::PillBadge::Tone::Warn,
                    icons::Name::Warn, true}; // R-069/R-139：词表单源
        return {PageRun::tr(pp::ui::term_words::kDone), fk::PillBadge::Tone::Ok, icons::Name::Check,
                true};
    case OutKind::Failed:
        return {PageRun::tr(pp::ui::term_words::kFailed), fk::PillBadge::Tone::Err,
                icons::Name::Error, true};
    case OutKind::Skipped:
        return {PageRun::tr(pp::ui::term_words::kSkipped), fk::PillBadge::Tone::Warn,
                icons::Name::Pending, false};
    case OutKind::Cancelled:
        return {PageRun::tr(pp::ui::term_words::kCancelled), fk::PillBadge::Tone::Neutral,
                icons::Name::Stop, false};
    case OutKind::Active:
    case OutKind::Queued:
        break;
    }
    // ---- M10-W1（T9a，§5.3）现有事件面的中间态（终端事件之前）----
    if (out.skip_seen) { // 首事件即 MetaWrite 1.0（无 Encode 段）→ 跳过
        return {PageRun::tr(pp::ui::term_words::kSkipped), fk::PillBadge::Tone::Warn,
                icons::Name::Pending, false,
                PageRun::tr("跳过：无编码段（冲突跳过 / 元数据已就绪），等待文件终态")};
    }
    if (out.meta_done_seen) { // MetaWrite frac=1.0 → 已完成（评分列仍留空到文件终态）
        return {PageRun::tr(pp::ui::term_words::kDone), fk::PillBadge::Tone::Ok, icons::Name::Check,
                true, PageRun::tr("已完成：输出已写盘（等待文件终态确认）")};
    }
    if (out.score_pending) { // Encode 1.0 且尚无 MetaWrite → 评分中（Accent 档 + 斜纹条）
        return {PageRun::tr("评分中"), fk::PillBadge::Tone::Accent, icons::Name::Active, false,
                PageRun::tr("评分中 · 等待 SSIMULACRA 2 评分完成")};
    }
    // A39 ③：未启动的 pill 文案统一收「排队」（交错语义进 tooltip）；「首个未完成行承载共享段」
    // 的行不是排队态（文本 = 共享段阶段词）→ 不带排队 tooltip。
    const QString text = out_stage_text(file, s, index);
    if (!out.started) {
        const bool queued = text == PageRun::tr("排队");
        return {text, fk::PillBadge::Tone::Neutral, icons::Name::Pending, false,
                queued ? queue_tip(s) : QString()};
    }
    QString tip = out_stage_tip(file, out);
    if (out.has_stage && out.last_stage == Stage::MetaWrite)
        tip = PageRun::tr("写入元数据与文件时间");
    else if (out.has_stage && out.last_stage == Stage::Flatten)
        tip = PageRun::tr("合成带 alpha 的输入（共享段）");
    return {text, fk::PillBadge::Tone::Accent, icons::Name::Active, false, tip};
}

// 组行聚合用的「已结算」= 终态 + 中间态已完成/跳过（§5.3：k = 已结算输出数）。
// 与全局 tally/settled_outputs 分开：后者只认终态事件事实（进度/汇总口径零改动）。
int display_settled_outputs(const FileRow &file) {
    int n = 0;
    for (const OutRow &out : file.outs) {
        if (settled_kind(out.kind) || out.skip_seen || out.meta_done_seen)
            ++n;
    }
    return n;
}

// 分组行状态（文件级；M10-W1/T9a：§5.3 聚合态 —— Running 时不再回显过期 shared_stage「色彩」）
StatusText file_status(const FileRow &file, const RunState &s) {
    bool zero = false;
    bool any_ok = false, any_bad = false;
    for (const OutRow &out : file.outs) {
        zero = zero || out_zero_bytes(out);
        if (out.kind == OutKind::Done)
            any_ok = true;
        if (out.kind == OutKind::Failed)
            any_bad = true;
    }
    // R-076：多输出"有成有败" = 部分成功（不聚合成 Done）—— 调动器侧仍映射 Done，这里按
    // 逐输出事实补判（与 mainwindow::handle_file_event 的补判同口径）。
    const bool partial = file.state == pp::FileState::Done && any_ok && any_bad;
    switch (file.state) {
    case pp::FileState::DoneWithErrors: // R-076：独立档
        return {PageRun::tr(pp::ui::term_words::kPartial), fk::PillBadge::Tone::Warn,
                icons::Name::Warn, true};
    case pp::FileState::Done:
        if (zero)
            return {PageRun::tr("异常"), fk::PillBadge::Tone::Warn, icons::Name::Warn, true};
        if (partial) // R-076
            return {PageRun::tr(pp::ui::term_words::kPartial), fk::PillBadge::Tone::Warn,
                    icons::Name::Warn, true};
        return {PageRun::tr(pp::ui::term_words::kDone), fk::PillBadge::Tone::Ok, icons::Name::Check,
                true};
    case pp::FileState::Failed:
        return {PageRun::tr(pp::ui::term_words::kFailed), fk::PillBadge::Tone::Err,
                icons::Name::Error, true};
    case pp::FileState::Skipped:
        return {PageRun::tr(pp::ui::term_words::kSkipped), fk::PillBadge::Tone::Warn,
                icons::Name::Pending, false};
    case pp::FileState::Cancelled:
        return {PageRun::tr(pp::ui::term_words::kCancelled), fk::PillBadge::Tone::Neutral,
                icons::Name::Stop, false};
    case pp::FileState::Queued:
        return {PageRun::tr(pp::ui::term_words::kQueued), fk::PillBadge::Tone::Neutral,
                icons::Name::Pending, false,
                queue_tip(s)}; // A39 ③：组行 pill 同收「排队」+ tooltip
    default:
        break;
    }
    if (file.terminal) { // 未列终态值兜底（保持现状文案）
        return {shared_stage_text(file.shared_stage), fk::PillBadge::Tone::Accent,
                icons::Name::Active, false};
    }
    // Running（非终态）：共享段**真正进行中**（无输出已启动）→ 保留共享段语义（§5.3 第二行）；
    // 否则按输出阶段聚合：写入中 > 评分中 > 编码中 k/n（k = 已结算输出数，§5.3）。
    if (file.started && !any_output_started(file)) {
        return {shared_stage_text(file.shared_stage), fk::PillBadge::Tone::Accent,
                icons::Name::Active, false};
    }
    bool any_scoring = false;
    bool any_writing = false;
    for (const OutRow &out : file.outs) {
        if (settled_kind(out.kind) || out.skip_seen || out.meta_done_seen)
            continue;
        if (out.score_pending) {
            any_scoring = true;
        } else if (out.has_stage &&
                   (out.last_stage == Stage::MetaWrite || out.last_stage == Stage::Flatten)) {
            any_writing = true;
        }
    }
    const int n = static_cast<int>(file.outs.size());
    const int k = display_settled_outputs(file);
    if (any_writing)
        return {PageRun::tr("写入中 %1/%2").arg(k).arg(n), fk::PillBadge::Tone::Accent,
                icons::Name::Active, false};
    if (any_scoring)
        return {PageRun::tr("评分中 %1/%2").arg(k).arg(n), fk::PillBadge::Tone::Accent,
                icons::Name::Active, false};
    return {PageRun::tr("编码中 %1/%2").arg(k).arg(n), fk::PillBadge::Tone::Accent,
            icons::Name::Active, false};
}

// ---- 数值列（§4.2 数值档 = 右对齐 + tabular）----

QString out_size_text(const OutRow &out) {
    if (out.kind == OutKind::Done)
        return fmt_bytes(static_cast<quint64>(std::max<qint64>(0, out.bytes)));
    return QStringLiteral("—");
}

// C10/C11（M10-W1）：原「速度列」的压缩比文本 `ratio_text` 随列一并退役（压缩比只进大小列
// tooltip = ratio_tip；见下）。

// 大小列 tooltip（C11，M10-W1：压缩比从已删的速度列迁入本列 tooltip）—— **标基线**
// （修 3-11：「0.6×」是相对源文件的压缩倍数，不是一个"速度"）。ratio-tip
// 探针断言含「压缩比」与「×」。
QString ratio_tip(qint64 src_bytes, qint64 out_bytes, double ratio) {
    if (ratio <= 0.0 || out_bytes <= 0)
        return PageRun::tr("压缩比 · 相对源文件（源文件字节 ÷ 输出字节）");
    return PageRun::tr("压缩比 · 相对源文件：%1 ÷ %2 = %3×")
        .arg(fmt_bytes(static_cast<quint64>(std::max<qint64>(0, src_bytes))),
             fmt_bytes(static_cast<quint64>(std::max<qint64>(0, out_bytes))))
        .arg(ratio, 0, 'f', 2);
}

// 名称列 tooltip（C10，M10-W1）：**完整路径**（组行附 宽×高 · 大小；输出行附格式名）。
// 显示文本 = 文件名（组行）/ 格式名（输出行），中间省略；语义全文仍在 ppFullText（探针钩子）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
QString group_name_tip(const QString &path, int width, int height, qint64 src_bytes) {
    QStringList parts;
    if (width > 0 && height > 0)
        parts << QStringLiteral("%1×%2").arg(width).arg(height);
    if (src_bytes > 0)
        parts << fmt_bytes(static_cast<quint64>(std::max<qint64>(0, src_bytes)));
    return parts.isEmpty() ? path : path + QLatin1Char('\n') + parts.join(QStringLiteral(" · "));
}

QString out_name_tip(const QString &path, const QString &format_id) {
    return path + QLatin1Char('\n') + PageRun::tr("输出格式：%1").arg(format_name(format_id));
}

// 分组读数（Σ 字节 / Σ 耗时 / 源 ÷ Σ 输出）
struct GroupFacts {
    qint64 bytes = 0;
    double ms = 0.0;
    double ratio = 0.0;
};

GroupFacts group_facts(const FileRow &file) {
    GroupFacts facts;
    for (const OutRow &out : file.outs) {
        facts.bytes += std::max<qint64>(0, out.bytes);
        facts.ms += std::max(0.0, out.ms);
    }
    if (facts.bytes > 0 && file.src_bytes > 0)
        facts.ratio = static_cast<double>(file.src_bytes) / static_cast<double>(facts.bytes);
    return facts;
}

// 汇总（kv「汇总」；按**输出**计 —— 与卡头「N / M 个输出」同一分母）
struct Tally {
    int ok = 0, warn = 0, failed = 0, skipped = 0, cancelled = 0;

    int settled() const { return ok + warn + failed + skipped + cancelled; }
};

Tally run_tally(const RunState &s) {
    Tally t;
    for (const FileRow &file : s.files) {
        for (const OutRow &out : file.outs) {
            switch (out.kind) {
            case OutKind::Done:
                (out_zero_bytes(out) ? t.warn : t.ok) += 1;
                break;
            case OutKind::Failed:
                t.failed += 1;
                break;
            case OutKind::Skipped:
                t.skipped += 1;
                break;
            case OutKind::Cancelled:
                t.cancelled += 1;
                break;
            case OutKind::Queued:
            case OutKind::Active:
                break;
            }
        }
    }
    return t;
}

QString tally_text(const Tally &t) {
    if (t.settled() == 0)
        return QStringLiteral("—"); // §2.5：未发生的量不显示
    QStringList parts;
    if (t.ok > 0)
        parts << PageRun::tr("成功 %1").arg(t.ok);
    if (t.warn > 0)
        parts << PageRun::tr("异常 %1").arg(t.warn);
    if (t.failed > 0)
        parts << PageRun::tr("失败 %1").arg(t.failed);
    if (t.skipped > 0)
        parts << PageRun::tr("跳过 %1").arg(t.skipped);
    if (t.cancelled > 0)
        parts << PageRun::tr("取消 %1").arg(t.cancelled);
    return parts.join(QStringLiteral(" · "));
}

// ===========================================================================
// 事件 → 行状态（§7.2/§7.3；M4-T14 语义零改动；M10-W1/T9a：C12 中间态 + C15 双通道标记）
// ===========================================================================

// C12（T9a）：现有事件面的阶段状态推进。事件序列真源 = core/progress.cpp 的 ProgressMux：
//   * 正常输出：Encode 0/…/Encode 1.0（on_output_encode_done 强制点）→ MetaWrite 0（写入）→
//     MetaWrite 1.0（on_output_metawrite_done 强制点）；
//   * 冲突跳过：**首个输出事件即 MetaWrite 1.0**（on_output_skipped，无 Encode 段）；
//   * 仅元数据：MetaWrite 0 起（on_output_no_encode）→ MetaWrite 1.0。
// 状态翻转（阶段切换/评分开始/完成/跳过/合成标识变化）走 dirty_state（singleShot(0) 立即刷），
// 其余（百分比/行数等数值）走 dirty_progress（60ms 合并）—— §5.4 双通道。
void apply_progress(RunState &s, FileRow &file, const ProgressInfo &pi) {
    if (pi.output_index >= 0 && pi.output_index < file.outs.size()) {
        OutRow &out = file.outs[pi.output_index];
        bool flip = false;
        const bool first_output_event = !out.got_output_event;
        if (first_output_event)
            flip = true;
        out.got_output_event = true;
        if (!out.has_stage || out.last_stage != pi.stage || out.synthetic != pi.synthetic)
            flip = true;
        out.started = true;
        if (!settled_kind(out.kind))
            out.kind = OutKind::Active; // 进行中（终态由 apply_terminal 定稿，不被进度回退）
        out.synthetic = pi.synthetic;   // §7.3：true → 斜纹
        out.last_stage = pi.stage;
        out.has_stage = true;
        if (pi.stage == Stage::Encode) {
            out.seen_encode = true;
            out.enc = std::max(out.enc, pi.stage_frac);
            if (file.height > 0) {
                out.rows_now = std::max(out.rows_now,
                                        static_cast<int>(std::lround(pi.stage_frac * file.height)));
            }
            // Encode frac=1.0（编码完成强制点）且尚无 MetaWrite 事件 → 「评分中」（§5.3）
            if (pi.stage_frac >= 1.0f && !out.score_pending && !out.meta_done_seen &&
                !out.skip_seen) {
                out.score_pending = true;
                flip = true;
            }
        } else if (pi.stage == Stage::MetaWrite) {
            if (out.score_pending) { // 评分结束 → 进入写入
                out.score_pending = false;
                flip = true;
            }
            // 「跳过」：首个输出事件即 MetaWrite 1.0（无 Encode 段）—— on_output_skipped 的形状
            if (first_output_event && !out.seen_encode && pi.stage_frac >= 1.0f) {
                out.skip_seen = true;
                flip = true;
            }
            out.meta = std::max(out.meta, pi.stage_frac);
            // 「已完成」中间态：MetaWrite frac=1.0（终端事件前；评分列仍留空到文件终态）
            if (!out.skip_seen && out.seen_encode && pi.stage_frac >= 1.0f && !out.meta_done_seen) {
                out.meta_done_seen = true;
                flip = true;
            }
        }
        if (!file.started) {
            file.started = true;
            ++s.started_files;
            flip = true;
        }
        if (flip)
            mark_state(s, file.index);
        else
            mark_progress(s, file.index);
        return;
    }
    if (pi.output_index < 0) {
        const float weight = shared_weight(pi.stage);
        const bool stage_changed = !file.started || file.shared_stage != pi.stage;
        const float contribution = weight * pi.stage_frac;
        switch (pi.stage) {
        case Stage::Probe:
            file.shared_probe = std::max(file.shared_probe, contribution);
            break;
        case Stage::Decode:
            file.shared_decode = std::max(file.shared_decode, contribution);
            break;
        case Stage::Orient:
            file.shared_orient = std::max(file.shared_orient, contribution);
            break;
        case Stage::Color:
            file.shared_color = std::max(file.shared_color, contribution);
            break;
        default:
            // Flatten（合成）/其余共享段：只承载阶段文案（权重 0，不进进度算术；§5.3 共享段语义）
            break;
        }
        file.shared_stage = pi.stage;
        // 共享段事件也可能承担「首个事件」：阶段切换/首次共享事件 → 状态翻转（排队 → 探测/色彩…）
        if (stage_changed || weight <= 0.f)
            mark_state(s, file.index);
        else
            mark_progress(s, file.index);
    }
}

// M10-W1（T9b；C12/C13/C14）：逐输出终态事件 → 该输出行**立即**结算（不等文件终态）。
// 事实来源 = `FileEvent::output`（引擎在"编码 + metawrite 完成/跳过/失败/取消"时发出的行快照；
// 评分池完成后再发一次**含 metric** 的新对象）。幂等口径 = 后到覆盖先到、同值早退。
void apply_output_done(RunState &s, FileRow &file, int index, const pp::OutputResult &row) {
    if (index < 0 || index >= file.outs.size())
        return;
    OutRow &out = file.outs[index];
    if (!file.started) {
        file.started = true;
        ++s.started_files;
    }
    // ---- 行事实（大小 / 耗时 / 压缩比 / 失败原因 / warnings）----
    out.bytes = static_cast<qint64>(row.out_bytes);
    out.ratio = (out.bytes > 0 && file.src_bytes > 0)
                    ? static_cast<double>(file.src_bytes) / static_cast<double>(out.bytes)
                    : 0.0;
    out.ms = row.t.encode_ms + row.t.metawrite_ms;
    out.error = QString::fromStdString(row.error);
    QStringList wl;
    for (const pp::Warning &w : row.warnings)
        wl << QString::fromStdString(w.detail);
    out.warnings = wl.join(QStringLiteral("；"));
    // ---- 行终态（R-172 显式 OutputState；Pending = 旧行尚未迁移 → 按 ok/skipped/error 兜底）----
    OutKind kind = OutKind::Active;
    switch (row.state) {
    case pp::OutputState::Ok:
        kind = OutKind::Done;
        break;
    case pp::OutputState::Skipped:
        kind = OutKind::Skipped;
        break;
    case pp::OutputState::Failed:
        kind = OutKind::Failed;
        break;
    case pp::OutputState::Cancelled:
        kind = OutKind::Cancelled;
        break;
    case pp::OutputState::Pending:
        if (!row.error.empty())
            kind = OutKind::Failed;
        else if (row.skipped)
            kind = OutKind::Skipped;
        else if (row.ok)
            kind = OutKind::Done;
        break;
    }
    if (kind != OutKind::Active)
        out.kind = kind;
    // 输出级终态事实已到 → 「评分中」/「已完成」/「跳过」中间态标记退役
    out.score_pending = false;
    out.meta_done_seen = false;
    out.skip_seen = false;
    // ---- 评分列（C14）----
    if (row.metric_state != pp::MetricState::Off) {
        // 第二次 OutputDone（评分池补发，含 metric）：立即填分；Unavailable/Failed → 「—」+ 原因
        out.metric_state = row.metric_state;
        out.metric_score = row.metric_ssimulacra2;
        out.metric_note = QString::fromStdString(row.metric_note);
        out.score_wait = false;
    } else if (out.metric_state == pp::MetricState::Off) {
        // 首次 OutputDone（异步评分尚未回）：成功行 + 本批启用评分 → 评分列「评分中」；
        // 否则留空（§2.5：未发生的量不显示）。已有分数不被 metric Off 的迟到事件覆盖。
        out.score_wait = s.plan.quality_score && !s.plan.metadata_only && kind == OutKind::Done;
    }
    mark_state(s, file.index);
}

void apply_terminal(RunState &s, FileRow &file, const pp::FileEvent &ev) {
    file.terminal = true;
    file.state = ev.state;
    ++s.terminal_files;
    if (!file.started) {
        file.started = true;
        ++s.started_files;
    }
    const pp::FileResult *result = ev.result;
    const QString file_error =
        result != nullptr ? QString::fromStdString(result->error) : QString();
    if (result != nullptr) {
        s.out_bytes += result->out_bytes;
        if (file.height == 0 && result->info.height > 0)
            file.height = result->info.height; // 兜底：plan 未给高度时用终态信息补
        if (file.width == 0 && result->info.width > 0)
            file.width = result->info.width;
    }
    for (int j = 0; j < file.outs.size(); ++j) {
        OutRow &out = file.outs[j];
        // §5.3：文件终态 = 幂等校准 —— 已由 OutputDone 定稿的行**不再改状态**（只校事实），
        // 否则终态行的 error 文本会把「已取消」重判成「失败」等（同值早退口径）。
        const bool settled_by_output = settled_kind(out.kind);
        const pp::OutputResult *row =
            (result != nullptr && j < static_cast<int>(result->outputs.size()))
                ? &result->outputs[static_cast<std::size_t>(j)]
                : nullptr;
        if (row != nullptr) {
            out.bytes = static_cast<qint64>(row->out_bytes);
            out.ratio = (out.bytes > 0 && file.src_bytes > 0)
                            ? static_cast<double>(file.src_bytes) / static_cast<double>(out.bytes)
                            : 0.0;
            out.ms = row->t.encode_ms + row->t.metawrite_ms;
            out.error = QString::fromStdString(row->error);
            QStringList wl;
            for (const pp::Warning &w : row->warnings)
                wl << QString::fromStdString(w.detail);
            out.warnings = wl.join(QStringLiteral("；"));
            // M8-C2（C11/C12）+ M10-W1（T9b）：评分终态事实（加性字段；Off = 不显示）。
            // 引擎的评分是异步的 → 文件终态 FileResult 的行 metric 恒 Off（引擎移交事项①），
            // 故**只在尚无任何 metric 时**用终态值兜底；已由 OutputDone 填好的分数/「评分中」
            // 绝不被终态的 metric Off 清空（§5.3「文件终态 = 幂等校准，不覆盖已定稿行」）。
            if (out.metric_state == pp::MetricState::Off) {
                out.metric_state = row->metric_state;
                out.metric_score = row->metric_ssimulacra2;
                out.metric_note = QString::fromStdString(row->metric_note);
                if (out.metric_state != pp::MetricState::Off)
                    out.score_wait = false;
            }
            if (!settled_by_output) {
                if (out.error.isEmpty() && row->skipped)
                    out.kind = OutKind::Skipped;
                else if (out.error.isEmpty() && row->ok)
                    out.kind = OutKind::Done;
                else if (!out.error.isEmpty())
                    out.kind = OutKind::Failed;
            }
        }
        if (!settled_kind(out.kind)) {
            // R-187：平行状态机互译单点（原内联 switch 抽为 out_kind_of_file_state）
            out.kind = out_kind_of_file_state(ev.state);
        }
        if (out.kind == OutKind::Failed && out.error.isEmpty())
            out.error = file_error; // 文件级失败（plan/decode）→ 逐输出行回落到聚合原因
        // 中间态标记退役（终端事实定稿；幂等校准由 settled 分支接管）
        out.score_pending = false;
        out.meta_done_seen = false;
        out.skip_seen = false;
    }
    mark_state(s, file.index);
}

// ===========================================================================
// 渲染（60ms flush 的统一入口）
// ===========================================================================

void apply_pill(fk::PillBadge *pill, const StatusText &st, QString &text_cache,
                fk::PillBadge::Tone &tone_cache, bool &icon_set, icons::Name &icon_cache) {
    if (text_cache != st.text) {
        text_cache = st.text;
        pill->setText(st.text);
    }
    if (tone_cache != st.tone) {
        tone_cache = st.tone;
        pill->set_tone(st.tone);
    }
    // PillBadge 的图标"一经设置不可清除"（A8 契约）；行程内状态单调 → 首次挂上即可，
    // 仅图标本身变化时重设。需要"有图标 ↔ 无图标"往返的场景走 swap_pill（见状态胶囊）。
    if (st.has_icon && (!icon_set || icon_cache != st.icon)) {
        icon_set = true;
        icon_cache = st.icon;
        pill->set_icon(st.icon);
    }
}

// 状态胶囊的"有图标 ↔ 无图标"往返：PillBadge 没有清图标接口（formkit 是 W-G2 地基，不改），
// 故换一枚同 objectName 的新胶囊（就绪 → 进行中 → 已完成 → 再运行 的真实路径）。
fk::PillBadge *swap_pill(fk::PillBadge *old_pill, const theme::Tokens &tokens) {
    QWidget *host = old_pill->parentWidget();
    auto *layout = host != nullptr ? qobject_cast<QHBoxLayout *>(host->layout()) : nullptr;
    auto *fresh = new fk::PillBadge(host);
    fresh->setObjectName(old_pill->objectName());
    fresh->setText(old_pill->text());
    fresh->set_tone(old_pill->tone());
    fresh->set_tokens(tokens);
    if (layout != nullptr) {
        layout->insertWidget(layout->indexOf(old_pill), fresh);
        layout->removeWidget(old_pill);
    }
    old_pill->hide();
    old_pill->deleteLater();
    return fresh;
}

// M8-W2（需求3）：评分刻度 tooltip 单源（四档阈值 + 参考口径；取代 C12 子行的旧刻度文案，
// 阈值口径 = 本需求「≥95 极好 / ≥90 好 / ≥80 ok / <80 不ok」）。
QString score_scale_tip() {
    return PageRun::tr("SSIMULACRA 2（0–100，越高越接近编码输入）：≥95 极好、≥90 好、≥80 ok、"
                       "<80 不ok。参考 = 编码器输入（量化后样本），失真 = 解码后产物；"
                       "本分数评价编码/量化损失，无损目标应为 100。");
}

// M8-W2（需求3）：四档语义色（theme token 单源；无新增令牌）——
// ≥95 极好 = ok（绿）/ ≥90 好 = accent（蓝，与「极好」绿明确区分）/ ≥80 ok = warn（琥珀）/
// <80 不ok = err（红）。双主题均取各自令牌值（暗 #4cc2ff / 亮 #0067c0），布局零差异。
QColor score_tint(const theme::Tokens &t, double score) {
    if (score >= 95.0)
        return t.ok;
    if (score >= 90.0)
        return t.accent;
    if (score >= 80.0)
        return t.warn;
    return t.err;
}

void render_out_row(RunState &s, FileRow &file, int index) {
    OutRow &out = file.outs[index];
    // 条色：终态按终态色；进行中（含"首个未完成行承载共享段进度"那一行）= accent 实心；
    // 未启动 = 轨道（无填充）。0 字节 = 琥珀（修 3-10：禁绿色成功条）。
    // M10-W1（T9a，§5.3）：中间态 —— 「评分中」= Accent 斜纹（进度不可知）；「已完成」中间态 =
    // 绿满格；「跳过」中间态 = 琥珀（C8 无跳过档）。
    const bool carries_shared = !out.started && file.started && !file.terminal &&
                                !any_output_started(file) && index == first_unstarted(file);
    const bool settled = settled_kind(out.kind);
    bool synth = out.synthetic;
    fk::ProgressStrip::Kind kind = fk::ProgressStrip::Kind::Pending;
    if (settled) {
        switch (out.kind) {
        case OutKind::Done:
            kind =
                out_zero_bytes(out) ? fk::ProgressStrip::Kind::Warn : fk::ProgressStrip::Kind::Done;
            break;
        case OutKind::Failed:
            kind = fk::ProgressStrip::Kind::Failed;
            break;
        case OutKind::Skipped:
        case OutKind::Cancelled:
            kind = fk::ProgressStrip::Kind::Warn; // C8 无"跳过"档：无产出/未完成 → 琥珀
            break;
        case OutKind::Active:
        case OutKind::Queued:
            break;
        }
    } else if (out.skip_seen) {
        kind = fk::ProgressStrip::Kind::Warn;
    } else if (out.meta_done_seen) {
        kind = fk::ProgressStrip::Kind::Done; // MetaWrite 1.0 → 已完成（终端确认前）
    } else if (out.started || carries_shared) {
        kind = fk::ProgressStrip::Kind::Active;
        if (out.score_pending)
            synth = true; // 评分中：阶段进度不可知 → 斜纹（§5.3 / §8 语义兼容）
    }
    out.bar->set_state(kind, out_row_frac(file, out), synth);

    const StatusText st = out_status(file, s, index);
    out.mark->set_mark(st.icon, tone_color(s, st.tone));
    apply_pill(out.status, st, out.applied_status, out.applied_tone, out.applied_icon_set,
               out.applied_icon);
    out.status->setToolTip(st.tip); // A39 ③：排队 pill 的 tooltip（其余状态清空）

    // 数值列（完成 = 正文对比度；未发生 = 注释档；0 字节 = 琥珀，修 3-10）
    out.size_cache.apply(out.size, out_size_text(out),
                         out_zero_bytes(out)
                             ? s.tokens.warn
                             : (out.kind == OutKind::Done ? s.tokens.text : s.tokens.text3));
    // C11（M10-W1）：压缩比不再是独立列 —— 作为大小列的 tooltip（基线口径不变，修 3-11）
    out.size->setToolTip(ratio_tip(file.src_bytes, out.bytes, out.ratio));
    out.time_cache.apply(out.time, out.ms > 0.0 ? fmt_secs(out.ms) : QStringLiteral("—"),
                         out.ms > 0.0 ? s.tokens.text : s.tokens.text3);

    // 失败行：下方「└ 原因：…」行；成功行若有 warning（降档/丢元数据/拍平/丢 ICC），
    // 同样出一行「⚠ …」——这是 WarningKind 在 UI 的唯一可见出口（第二轮 X-COMPLIANCE-08）。
    const bool failed = out.kind == OutKind::Failed;
    const bool has_warning = !failed && !out.warnings.isEmpty();
    if (failed) {
        // R-070/273：错误分类映射中文（core/error_text.h 单源，R2B 备表）；未命中回退引擎原文
        // （不编造文案），原始串进 tooltip（分类中文 + 原文进 tooltip/日志口径）。
        const std::string err = out.error.toStdString();
        const std::string zh = err.empty() ? std::string() : pp::error_text_zh(err);
        const QString shown = QString::fromStdString(zh.empty() ? err : zh);
        const QString text =
            PageRun::tr("└ 原因：%1").arg(shown.isEmpty() ? PageRun::tr("未报告") : shown);
        if (out.applied_reason != text) {
            out.applied_reason = text;
            out.reason->set_full_text(text);
            out.reason->setToolTip(err.empty() ? text : QString::fromStdString(err));
        }
    }
    if (out.reason_shown != failed) {
        out.reason_shown = failed;
        out.reason_row->setVisible(failed);
    }
    // 成功行的 warning（降档/丢元数据/拍平/丢 ICC）单独一行；失败行不重复显示。
    if (has_warning) {
        const QString text = PageRun::tr("⚠ %1").arg(out.warnings);
        if (out.applied_warning != text) {
            out.applied_warning = text;
            out.warn->set_full_text(text);
            out.warn->setToolTip(text);
        }
    }
    if (out.warn_shown != has_warning) {
        out.warn_shown = has_warning;
        out.warn_row->setVisible(has_warning);
    }
    // M8-W2（需求3）：评分列 —— Ok = 「92.31」（两位小数 + 四档语义色）；Unavailable/Failed =
    // 「—」+ tooltip 原因；Off/仅元数据 = 空（未发生的量不显示）。文件组行不做平均（「—」）。
    QString score_text;
    QColor score_col = s.tokens.text3;
    QString score_tip;
    if (out.score_wait && out.metric_state == pp::MetricState::Off) {
        // M10-W1（T9b；C14/§5.3）：首次 OutputDone 已结算、评分池结果未到 →「评分中」（Accent
        // 档）。
        // 这是**诚实中间态**：行已写盘（状态列「已完成」），分数在评（不虚增、不显示占位分数）。
        score_text = PageRun::tr("评分中");
        score_col = s.tokens.accent;
        score_tip = PageRun::tr("SSIMULACRA 2 评分中 · 评分池结果到达后立即填入本列");
    } else {
        switch (out.metric_state) {
        case pp::MetricState::Ok:
            score_text = QString::number(out.metric_score, 'f', 2); // 2 位小数（实数，不 clamp）
            score_col = score_tint(s.tokens, out.metric_score);
            score_tip = score_scale_tip();
            if (!out.metric_note.isEmpty())
                score_tip += PageRun::tr("\n备注：%1").arg(out.metric_note);
            break;
        case pp::MetricState::Unavailable:
        case pp::MetricState::Failed:
            // 「—」为简洁形态；完整原因进 tooltip（列宽固定 → 不在单元格内塞长文案导致裁切）
            score_text = QStringLiteral("—");
            score_tip =
                PageRun::tr("SSIMULACRA 2 不可用：%1")
                    .arg(out.metric_note.isEmpty() ? PageRun::tr("未报告原因") : out.metric_note);
            break;
        case pp::MetricState::Off:
        default:
            break; // 空 = 未发生的量不显示（评分关闭/仅元数据）
        }
    }
    out.score_cache.apply(out.score, score_text, score_col);
    out.score->setToolTip(score_tip);
}

void render_file_row(RunState &s, FileRow &file) {
    const StatusText st = file_status(file, s);
    file.mark->set_mark(st.icon, tone_color(s, st.tone));
    apply_pill(file.status, st, file.applied_status, file.applied_tone, file.applied_icon_set,
               file.applied_icon);
    file.status->setToolTip(st.tip); // A39 ③：组行排队 pill 的 tooltip（其余状态清空）

    const GroupFacts facts = group_facts(file);
    file.size_cache.apply(file.size,
                          facts.bytes > 0 ? fmt_bytes(static_cast<quint64>(facts.bytes))
                                          : QStringLiteral("—"),
                          facts.bytes > 0 ? s.tokens.text : s.tokens.text3);
    // C11（M10-W1）：组行压缩比（源 ÷ Σ 输出）同样并入大小列 tooltip
    file.size->setToolTip(ratio_tip(file.src_bytes, facts.bytes, facts.ratio));
    file.time_cache.apply(file.time, facts.ms > 0.0 ? fmt_secs(facts.ms) : QStringLiteral("—"),
                          facts.ms > 0.0 ? s.tokens.text : s.tokens.text3);
    // M8-W2（需求3）：文件组行不显示评分（不做平均）——「—」+ tooltip 说明（未发生的量不显示）。
    file.score_cache.apply(file.score, QStringLiteral("—"), s.tokens.text3);
    file.score->setToolTip(PageRun::tr("评分按输出计算（SSIMULACRA 2）；文件组行不做平均"));

    // 分组条：失败 > 0 字节异常 > 终态 > 进行中/排队（C8 档位）
    bool any_failed = false;
    bool any_zero = false;
    for (const OutRow &out : file.outs) {
        any_failed = any_failed || out.kind == OutKind::Failed;
        any_zero = any_zero || out_zero_bytes(out);
    }
    fk::ProgressStrip::Kind kind = fk::ProgressStrip::Kind::Pending;
    if (any_failed) {
        kind = fk::ProgressStrip::Kind::Failed;
    } else if (file.terminal) {
        if (any_zero || file.state == pp::FileState::Skipped ||
            file.state == pp::FileState::Cancelled) {
            kind = fk::ProgressStrip::Kind::Warn;
        } else if (file.state == pp::FileState::Done) {
            kind = fk::ProgressStrip::Kind::Done;
        }
    } else if (file.started) {
        kind = fk::ProgressStrip::Kind::Active;
    }
    double frac = 0.0;
    if (!file.outs.isEmpty()) {
        for (const OutRow &out : file.outs)
            frac += out_row_frac(file, out);
        frac /= static_cast<double>(file.outs.size());
    }
    file.bar->set_state(kind, frac, false);
}

// R-188（M7-R2.1-F3）：refresh_widgets 原 160 行单函数吃 7 个语义段（deep-review R-188/E3-09
// 「60ms 节流核心无法局部推理」）→ 按语义拆 7 个段函数（同 R-253 mainwindow 语义分段先例）：
// ① 进度合成条 ② kv 时间/吞吐/平均 ③ 调度读数（并行/交错/线程预算）④ 汇总/峰值
// ⑤ 状态胶囊 ⑥ 操作行 ⑦ 任务表 + 日志尾。共享读数一次算好传入、段调用顺序与原文逐字
// 一致 —— 行为零变化。入口 refresh_widgets 在段后定义（60ms flush 的统一入口不变）。

// 段①（R-188）：总览 —— 进度合成条 + 百分比 + 计数提示
void refresh_overview_progress(RunState &s, int total_outputs, double frac, const Tally &tally,
                               int settled) {
    const int percent_now = s.finished ? 100 : static_cast<int>(std::lround(frac * 100.0));
    if (s.progress->value() != percent_now)
        s.progress->setValue(percent_now);
    const double denom = total_outputs > 0 ? static_cast<double>(total_outputs) : 1.0;
    s.progress->set_segments(static_cast<double>(tally.ok) / denom,
                             static_cast<double>(tally.warn) / denom,
                             static_cast<double>(tally.failed) / denom,
                             static_cast<double>(tally.skipped + tally.cancelled) / denom,
                             std::max(0.0, frac - static_cast<double>(settled) / denom));
    s.percent->set_full_text(QStringLiteral("%1%").arg(percent_now));
    // 计数单源（修 3-13）：输出总数只出现在这里；逐文件「N 个输出」已作废
    s.progress_hint->set_full_text(
        total_outputs > 0 ? PageRun::tr("%1 / %2 个输出").arg(settled).arg(total_outputs)
                          : QString());
}

// 段②（R-188）：总览 kv —— 总耗时 / 吞吐 / 平均（指标单源 + 未发生的量显示 "—"）
void refresh_overview_metrics(RunState &s) {
    const qint64 live_ms = s.clock.isValid() ? s.clock.elapsed() : 0;
    const qint64 total_ms = (s.finished && s.has_summary) ? static_cast<qint64>(s.summary_total_ms)
                                                          : (live_ms > 0 ? live_ms : 0);
    s.elapsed->set_full_text(total_ms > 0 ? fmt_duration(total_ms) : QStringLiteral("—"));
    s.elapsed->setToolTip(PageRun::tr("本次运行的墙钟总耗时（运行中实时累计，结束时取引擎汇总）"));
    const double mb_s = (s.finished && s.has_summary)
                            ? s.summary_mb_s
                            : (live_ms > 0 ? (static_cast<double>(s.out_bytes) / 1.0e6) /
                                                 (static_cast<double>(live_ms) / 1000.0)
                                           : 0.0);
    s.throughput->set_full_text(mb_s > 0 ? fmt_mb_s(mb_s) : QStringLiteral("—"));
    s.throughput->setToolTip(PageRun::tr("已写盘字节 ÷ 用时"));
    const double avg_ms = (s.finished && s.has_summary)
                              ? s.summary_avg_ms
                              : (s.terminal_files > 0 ? static_cast<double>(live_ms) /
                                                            static_cast<double>(s.terminal_files)
                                                      : 0.0);
    s.average->set_full_text(avg_ms > 0 ? PageRun::tr("%1 ms/文件").arg(avg_ms, 0, 'f', 0)
                                        : QStringLiteral("—"));
    s.average->setToolTip(PageRun::tr("每个文件的平均处理时间"));
}

// 段③（R-188）：总览 kv —— 并行 / 交错启动 / 线程预算（§8.2 分配读数进 tooltip）
void refresh_overview_sched(RunState &s) {
    const int budget = s.plan.thread_budget > 0 ? s.plan.thread_budget : hardware_threads();
    const int pool = s.plan.workers > 0 ? s.plan.workers : budget;
    const int inflight =
        std::max(0, static_cast<int>(s.started_files) - static_cast<int>(s.terminal_files));
    const int remaining =
        std::max(0, static_cast<int>(s.files.size()) - static_cast<int>(s.started_files));
    // 并行 = 进行中 / 可并行（读数与引擎同源：进行中 = 已取件 − 已结算）
    s.workers->set_full_text(s.running || s.finished
                                 ? PageRun::tr("%1 / %2").arg(inflight).arg(pool)
                                 : QStringLiteral("—"));
    // §8.2 的分配读数（实现细节）进 tooltip（修 3-05；§2.6：实现细节进 tooltip）。
    // R-065/R-237（M7-R2D2 文案统一）：并行上限与「线程预算」的耦合如实写明 —— workers=自动时
    // 并行数按线程预算与逻辑核数自适应，不是独立常量。
    if (!s.running || s.files.isEmpty()) {
        s.workers->setToolTip(
            PageRun::tr("正在处理的文件数 / 可并行上限\n"
                        "并行上限随「线程预算」联动（并行=自动时按线程预算与逻辑核数折算）"));
    } else {
        const pp::Alloc alloc = pp::alloc_threads(budget, remaining, inflight);
        s.workers->setToolTip(
            PageRun::tr("正在处理的文件数 / 可并行上限\n当前分配：%1 文件并行 + %2 编码多线程"
                        "\n并行上限随「线程预算」联动（并行=自动时按线程预算与逻辑核数折算）")
                .arg(alloc.files)
                .arg(alloc.encode_threads));
    }
    s.stagger->set_full_text(s.plan.stagger_ms > 0 ? PageRun::tr("%1 ms").arg(s.plan.stagger_ms)
                                                   : PageRun::tr("关闭"));
    s.stagger->setToolTip(
        s.plan.stagger_ms > 0
            ? PageRun::tr("每 %1 ms 才启动下一个文件，避免所有文件同时解码占满处理器")
                  .arg(s.plan.stagger_ms)
            : PageRun::tr("不交错：所有文件立即启动"));
    // 线程预算读数（W-R-T13 / A39 ①；修 4-12「同一设置跨界面同名同词」）：自动档的值 = 设置
    // 弹窗同词「自动」（settings·运行页签的「线程预算」下拉首项 + 行注「0 = 自动」）→ 运行页
    // 值形如「自动（16 核）」；旧「自适应（16 核）」措辞作废（review 3-04 的同值不同说）。
    // R-065/R-237（M7-R2D2）：语义改实 —— T 是**总活跃线程上限**（含编码器内部线程，§8.2），
    // 不是"核心数上限"；值文案与「自动 = 按本机核心数」字样为 ui_smoke run-overview 探针
    // 逐字钉住（不动），语义句并入 tooltip。
    s.budget->set_full_text(s.plan.thread_budget > 0
                                ? PageRun::tr("固定 %1 核").arg(s.plan.thread_budget)
                                : PageRun::tr("自动（%1 核）").arg(hardware_threads()));
    s.budget->setToolTip(
        PageRun::tr("总活跃线程上限 T（§8.2，含编码器内部线程；worker+编码线程峰值可到 2W）；"
                    "自动 = 按本机核心数"));
}

// 段④（R-188）：总览 kv —— 汇总 / 峰值内存 / 峰值线程
void refresh_overview_counts(RunState &s, const Tally &tally) {
    s.tally->set_full_text(tally_text(tally));
    s.tally->setToolTip(PageRun::tr("按输出计：成功 / 0 字节异常 / 失败 / 跳过 / 取消"));
    // 峰值内存（A29：解冻加性字段 RunSummary::peak_bytes → 接**真实读数**；与引擎
    // `budget report` 行同一统计点，单源）。运行中未采集（终态汇总才有该读数）→ 「—」（§2.5）
    s.memory->set_full_text(s.has_summary ? fmt_mem_bytes(s.summary_peak_bytes)
                                          : QStringLiteral("—"));
    s.memory->setToolTip(PageRun::tr("本次运行的像素预算峰值（解码/合成期的最大并发占用）"));
    // 峰值线程（R-078）：RunSummary::threads_total_est = peak_active_files +
    // peak_sum_encode_threads（估算口径；SynthPump/x265 库内池不入账）；未采集显「—」
    if (s.threads != nullptr) {
        s.threads->set_full_text(s.has_summary ? QString::number(s.summary_threads_est)
                                               : QStringLiteral("—"));
        s.threads->setToolTip(
            PageRun::tr("本次运行的峰值活跃线程估算（worker 主线程 + 编码器内部线程的实测峰值之和；"
                        "SynthPump 采样线程与 x265 库内线程池不入账 —— 估算口径）"));
    }
}

// 段⑤（R-188）：总览 —— 状态胶囊（§8 四档；完成态唯一形态 = 绿胶囊「已完成」）
void refresh_state_pill(RunState &s, const Tally &tally) {
    StatusText pill;
    if (s.running) {
        pill.text = PageRun::tr("进行中");
        if (tally.failed > 0)
            pill.text += PageRun::tr(" · 失败 %1").arg(tally.failed);
        else if (tally.warn > 0)
            pill.text += PageRun::tr(" · 异常 %1").arg(tally.warn);
        pill.tone = tally.failed > 0
                        ? fk::PillBadge::Tone::Err
                        : (tally.warn > 0 ? fk::PillBadge::Tone::Warn : fk::PillBadge::Tone::Ok);
    } else if (s.finished && s.cancel_requested) {
        pill.text = PageRun::tr("已取消");
        pill.tone = fk::PillBadge::Tone::Neutral;
    } else if (s.finished) {
        pill.text = PageRun::tr("已完成");
        if (tally.failed > 0)
            pill.text += PageRun::tr(" · 失败 %1").arg(tally.failed);
        else if (tally.warn > 0)
            pill.text += PageRun::tr(" · 异常 %1").arg(tally.warn);
        pill.tone = tally.failed > 0
                        ? fk::PillBadge::Tone::Err
                        : (tally.warn > 0 ? fk::PillBadge::Tone::Warn : fk::PillBadge::Tone::Ok);
        if (tally.failed == 0 && tally.warn == 0) {
            pill.has_icon = true; // 完成态唯一形态（A8：勾形 = 矢量图标）
            pill.icon = icons::Name::Check;
        }
    } else {
        pill.text = PageRun::tr("就绪");
        pill.tone = fk::PillBadge::Tone::Neutral;
    }
    if (pill.has_icon != s.state_pill_icon) {
        s.state_pill = swap_pill(s.state_pill, s.tokens);
        s.state_pill_icon = pill.has_icon;
        s.applied_state_text.clear();
        s.applied_state_tone = fk::PillBadge::Tone::Neutral;
        s.applied_state_icon_set = false;
        s.applied_state_icon = icons::Name::Pending;
    }
    apply_pill(s.state_pill, pill, s.applied_state_text, s.applied_state_tone,
               s.applied_state_icon_set, s.applied_state_icon);
}

// 段⑥（R-188）：总览 —— 操作行（三级动作 + 运行期语义）
void refresh_action_row(RunState &s) {
    if (s.running && s.cancel_requested) {
        s.cancel->setEnabled(false);
        s.cancel->setVisible(true);
        s.cancel->setText(PageRun::tr("取消中…"));
    } else if (s.running) {
        s.cancel->setEnabled(true);
        s.cancel->setVisible(true);
        s.cancel->setText(PageRun::tr("取消剩余"));
    } else {
        s.cancel->setEnabled(false);
        s.cancel->setVisible(false); // visible ∧ enabled ⇔ is_running()
    }
    s.run_again->setEnabled(!s.running);
    s.run_again->setToolTip(s.running ? PageRun::tr("运行中：结束后可用")
                                      : PageRun::tr("按当前设置再运行一批"));
}

// 段⑦-a（R-188 / M10-W1-C15）：任务卡表头/空态 + 日志尾（只在全量刷新/日志变化时走）
void refresh_task_chrome(RunState &s) {
    const bool has_rows = !s.files.isEmpty();
    s.table_head->setVisible(has_rows);
    s.task_empty->setVisible(!has_rows);

    // ---- 日志尾（有日志才有名字：修 3-14 的自相矛盾状态）----
    s.log_lines->set_lines(s.log_tail);
    s.log_name->set_full_text(s.log_tail.isEmpty() ? QString() : s.log_name_text);
    s.open_full_log->setEnabled(!s.log_tail.isEmpty() || !s.log_name_text.isEmpty());
}

// 段⑦-b（R-188 / C15）：逐行渲染。dirty-row：只渲染 row_dirty 标脏的文件；full=true 时全量。
// 返回本次渲染的文件数（页面动态属性 ppRenderedRows 供 flush-state/dirty-row 探针取证）。
int refresh_rows(RunState &s, bool full) {
    int rendered = 0;
    const int count = static_cast<int>(s.files.size());
    for (int i = 0; i < count; ++i) {
        const bool dirty = full || (i < static_cast<int>(s.row_dirty.size()) && s.row_dirty[i]);
        if (!dirty)
            continue;
        FileRow &file = s.files[i];
        render_file_row(s, file);
        for (int j = 0; j < file.outs.size(); ++j)
            render_out_row(s, file, j);
        if (i < static_cast<int>(s.row_dirty.size()))
            s.row_dirty[i] = false;
        ++rendered;
    }
    if (s.page != nullptr)
        s.page->setProperty("ppRenderedRows", rendered); // 探针钩子（只读取值，不改行为）
    return rendered;
}

// C15 / §5.4：总览卡读数（段①…⑥）—— 共享读数一次计算（行为与 R-188 版逐字一致）。
void refresh_overview(RunState &s) {
    const int total_outputs = static_cast<int>(s.files.size()) * s.formats;
    const double frac = s.finished ? 1.0 : run_frac(s);
    const Tally tally = run_tally(s);
    const int settled = settled_outputs(s);
    refresh_overview_progress(s, total_outputs, frac, tally, settled); // 段①
    refresh_overview_metrics(s);                                       // 段②
    refresh_overview_sched(s);                                         // 段③
    refresh_overview_counts(s, tally);                                 // 段④
    refresh_state_pill(s, tally);                                      // 段⑤
    refresh_action_row(s);                                             // 段⑥
}

// R-188：全量刷新入口（begin_run/reset/end_run/set_tokens/set_log_tail 用）—— 段①…⑦ 全走。
void refresh_widgets(RunState &s) {
    refresh_overview(s);
    refresh_task_chrome(s);
    refresh_rows(s, /*full=*/true);
}

// ---- C15（§5.4）双通道 flush ----
// dirty_state：singleShot(0) 立即刷一帧（状态翻转不让用户等到 60ms 节拍）；
// dirty_progress：60ms 定时器合并（数值/百分比高频，避免每事件重绘）。
void flush_rows_now(RunState &s) {
    if (s.overview_dirty)
        refresh_overview(s);
    s.overview_dirty = false;
    refresh_rows(s, /*full=*/false);
}

void flush_state(RunState &s) {
    s.state_flush_pending = false;
    if (!s.dirty_state)
        return;
    s.dirty_state = false;
    flush_rows_now(s);
    s.dirty_progress = false; // 本次状态帧已含最新行状态（进度通道无需再补一帧）
}

void flush_progress(RunState &s) {
    if (!s.dirty_progress)
        return;
    s.dirty_progress = false;
    flush_rows_now(s);
}

void mark_progress(RunState &s, int file_index) {
    s.dirty_progress = true;
    s.overview_dirty = true;
    if (file_index >= 0 && file_index < static_cast<int>(s.row_dirty.size()))
        s.row_dirty[static_cast<std::size_t>(file_index)] = true;
}

void mark_state(RunState &s, int file_index) {
    s.dirty_state = true;
    s.overview_dirty = true;
    if (file_index >= 0 && file_index < static_cast<int>(s.row_dirty.size()))
        s.row_dirty[static_cast<std::size_t>(file_index)] = true;
    if (s.state_flush_pending || s.page == nullptr)
        return; // 同拍合并：一帧状态刷即可（dirty_state 仍累积）
    s.state_flush_pending = true;
    QTimer::singleShot(0, s.page, [&s]() { flush_state(s); });
}

// ===========================================================================
// 页面 QSS（tokens 单源 = ui/theme.h；选择器只锚 ppRun/ppField/本页 objectName）
// ===========================================================================

constexpr const char *kFieldProperty =
    theme::kFkProperty; // UX-6⑤/R-054：三条钩子轨并入 ppFk（旧 ppField 轨退役）

QString page_qss(const theme::Tokens &t) {
    // UX-6⑥（M7-WP4）：跨页同形规则 = theme::field_qss（按钮/文字链三级动作），页专规则
    // （ppRun 五档语义色/表头/日志尾框/容器透明）= theme::page_run_qss —— 页内不再拼 QSS。
    return theme::field_qss(t) + theme::page_run_qss(t);
}

// ===========================================================================
// 界面构建
// ===========================================================================

// ---------------------------------------------------------------------------
// UX-1（M7-WP6）：卡片右键菜单 —— 公共件 ui/contextmenu（单一构造点）。
// items **惰性**构造（右键时才求值）→ 运行中置灰实时反映 lock_for_run 状态。
// 一卡一 actions 构造函数：运行总览 = run_overview_actions、任务进度 = task_row_actions、
// 日志尾 = log_tail_actions（docs/ux-review-14.md §1 表 ⑩⑪⑫ 行）。
// ---------------------------------------------------------------------------
void attach_card_menu(QWidget *host, const std::function<std::vector<contextmenu::Item>()> &items) {
    if (host == nullptr)
        return;
    host->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(host, &QWidget::customContextMenuRequested, host,
                     [host, items](const QPoint &pos) {
                         QMenu *m = contextmenu::menu(host, items());
                         m->setAttribute(Qt::WA_DeleteOnClose);
                         m->popup(host->mapToGlobal(pos));
                     });
}

// R-021/R-053 余量（M7-R2D2）：卡壳/卡头/卡体**单源** = formkit::make_card（本文件原
// make_card / card_head / make_card_body 三件套自拷删除；几何逐值等价 —— 卡头 12,9,12,7 +
// 间距 8、卡体 12,10,12,12，均走 formkit_layout 常量）。调用点各持一份 CardParts。

// 数值列（§4.2 数值档：右对齐 + tabular）
ElidedLabel *make_num_cell(QWidget *parent, const QString &object_name, int width) {
    auto *label = new ElidedLabel(parent);
    label->setObjectName(object_name);
    label->setFont(theme::font(theme::Typography::small_px, QFont::Normal, /*tabular=*/true));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setFixedWidth(width);
    return label;
}

// M8-W2（需求3）：评分列单元格 —— 定宽容器（保证各行/表头同列对齐）+ 右对齐数值文本。
// 单元格 = `pp-run-score-<i>-<j>`（组行 `pp-run-gscore-<i>`）、文本 = 同名 `-text-` 变体
// （沿用旧隐藏子行的探针钩子；探针按单元格 x 断言列对齐、按文本读分数/色阶）。
struct ScoreCell {
    QWidget *cell = nullptr;
    ElidedLabel *label = nullptr;
};

ScoreCell make_score_cell(QWidget *parent, const QString &cell_name, const QString &text_name) {
    ScoreCell out;
    out.cell = new QWidget(parent);
    out.cell->setObjectName(cell_name);
    out.cell->setFixedWidth(RunTableColumns::score);
    auto *line = new QHBoxLayout(out.cell);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(0);
    line->addStretch(1);
    out.label = new ElidedLabel(out.cell);
    out.label->setObjectName(text_name);
    out.label->setFont(theme::font(theme::Typography::small_px, QFont::Normal, /*tabular=*/true));
    out.label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    line->addWidget(out.label);
    return out;
}

// 状态列（胶囊右对齐；列宽固定 → 各列不错位）
struct StatusCell {
    QWidget *cell = nullptr;
    fk::PillBadge *pill = nullptr;
};

StatusCell make_status_cell(QWidget *parent, const QString &object_name,
                            const theme::Tokens &tokens) {
    StatusCell out;
    out.cell = new QWidget(parent);
    out.cell->setFixedWidth(RunTableColumns::status);
    auto *line = new QHBoxLayout(out.cell);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(0);
    line->addStretch(1);
    out.pill = new fk::PillBadge(out.cell);
    out.pill->setObjectName(object_name);
    out.pill->set_tokens(tokens);
    line->addWidget(out.pill);
    return out;
}

// 表行（R1：定高 28）
QWidget *make_table_row(QWidget *parent, const QString &object_name, int height) {
    auto *row = new QWidget(parent);
    row->setObjectName(object_name);
    row->setFixedHeight(height);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(kRowPadX, 0, kRowPadX, 0);
    line->setSpacing(kCellGap);
    return row;
}

// 行命名（探针钩子；test/ui_smoke.py 的 run-geom 与 mainwindow 的 T14 探针按这些名字取证）
// M10-W1（A11 收缩）：删除速度列 → `pp-run-speed-*` / `pp-run-gspeed-*` 钩子族**一并删除**
// （见 docs/m10-adjudications.md A11；no-speed 探针对其做否定断言）。
struct RowNames {
    QString row, mark, name, bar, size, time, score, score_text, status;
};

RowNames out_names(int i, int j) {
    const QString tag = QStringLiteral("%1-%2").arg(i).arg(j);
    const auto n = [&tag](const char *prefix) { return QString::fromLatin1(prefix) + tag; };
    // M8-W2（需求3）：评分列单元格/文本沿用旧隐藏子行的探针钩子名（pp-run-score-* / -text-）。
    return {n("pp-run-row-"),   n("pp-run-mark-"),       n("pp-run-oname-"),
            n("pp-run-bar-"),   n("pp-run-size-"),       n("pp-run-time-"),
            n("pp-run-score-"), n("pp-run-score-text-"), n("pp-run-status-")};
}

RowNames group_names(int i) {
    const QString tag = QString::number(i);
    const auto n = [&tag](const char *prefix) { return QString::fromLatin1(prefix) + tag; };
    return {n("pp-run-group-"),  n("pp-run-gmark-"),       n("pp-run-gname-"),
            n("pp-run-gbar-"),   n("pp-run-gsize-"),       n("pp-run-gtime-"),
            n("pp-run-gscore-"), n("pp-run-gscore-text-"), n("pp-run-gstatus-")};
}

// 行骨架（名称列 = 弹性列，其余定宽；缩进只作用于名称列）
struct RowSkeleton {
    QWidget *row = nullptr;
    RunMarkLabel *mark = nullptr;
    ElidedLabel *name = nullptr;
    fk::ProgressStrip *bar = nullptr;
    ElidedLabel *size = nullptr;
    ElidedLabel *time = nullptr;
    QWidget *score_cell = nullptr;
    ElidedLabel *score = nullptr;
    fk::PillBadge *status = nullptr;
};

RowSkeleton make_row_skeleton(QWidget *parent, const RowNames &names, const theme::Tokens &tokens,
                              bool output_row) {
    RowSkeleton out;
    out.row = make_table_row(parent, names.row, fk::kRowPitch);
    auto *line = qobject_cast<QHBoxLayout *>(out.row->layout());
    out.mark = new RunMarkLabel(out.row);
    out.mark->setObjectName(names.mark);
    line->addWidget(out.mark);
    // C10（M10-W1）：名称列 squeeze=true + ElideMiddle —— 长文件名**中间省略**（保留扩展名）
    // 而不是把列撑宽；剩余宽度 = RunTableColumns::name_width（唯一 stretch 项）。
    out.name = new ElidedLabel(out.row, /*squeeze=*/true, Qt::ElideMiddle);
    out.name->setObjectName(names.name);
    out.name->setFont(theme::font(theme::Typography::small_px, output_row ? QFont::Normal : 600));
    out.name->setProperty(kRunLabelProperty,
                          QString::fromLatin1(output_row ? kRunOName : kRunGName));
    if (output_row)
        set_leading_indent(out.name, kOutIndent); // mockup：只缩名称列（R-218：逻辑边距）
    line->addWidget(out.name, 1);
    out.bar = new fk::ProgressStrip(out.row);
    out.bar->setObjectName(names.bar);
    out.bar->setFixedWidth(RunTableColumns::bar);
    out.bar->set_tokens(tokens);
    line->addWidget(out.bar);
    out.size = make_num_cell(out.row, names.size, RunTableColumns::size);
    line->addWidget(out.size);
    out.time = make_num_cell(out.row, names.time, RunTableColumns::time);
    line->addWidget(out.time);
    // M8-W2（需求3）：评分列（定宽；组行与输出行同列 → 所有行对齐）
    const ScoreCell score_cell = make_score_cell(out.row, names.score, names.score_text);
    out.score_cell = score_cell.cell;
    out.score = score_cell.label;
    line->addWidget(out.score_cell);
    const StatusCell status = make_status_cell(out.row, names.status, tokens);
    out.status = status.pill;
    line->addWidget(status.cell);
    return out;
}

// 表头行（§7.3 表格列题，docs/v0.4.0-design.md §5.5/spec §7.3：文件 / 输出 · 进度 · 大小 ·
// 耗时 · 评分 · 状态 —— **删速度列**）；列宽与行**同一张表**（RunTableColumns）。
// 探针钩子：pp-run-thead-{name,bar,size,time,score,status}（cols-ok 按与行内列同 x/同宽断言）。
QWidget *make_table_head(QWidget *parent) {
    auto *head = new QFrame(parent);
    head->setObjectName(QStringLiteral("pp-run-thead"));
    head->setFixedHeight(kTableHeadH);
    auto *line = new QHBoxLayout(head);
    line->setContentsMargins(kRowPadX, 0, kRowPadX, 0);
    line->setSpacing(kCellGap);
    const auto label = [head](const QString &text, Qt::Alignment align, int width,
                              const char *object_name) {
        auto *l = new QLabel(text, head);
        l->setObjectName(QString::fromLatin1(object_name));
        l->setProperty(kRunLabelProperty, QString::fromLatin1(kRunHead));
        l->setFont(theme::font(theme::Typography::badge_px, 600));
        l->setAlignment(align | Qt::AlignVCenter);
        l->setWordWrap(false);
        if (width > 0)
            l->setFixedWidth(width);
        return l;
    };
    // 列题前的状态标记列占位（无列题；固定 16px）—— **必须是控件而非 addSpacing**：
    // QBoxLayout 对空 spacer 不追加列距（实测 name 列左移 6px），表头就会与行内列错位；
    // 固定宽控件则与行内 RunMarkLabel 同宽且照常参与列距 → cols-ok（表头 == 行）成立。
    auto *mark_spacer = new QWidget(head);
    mark_spacer->setObjectName(QStringLiteral("pp-run-thead-mark"));
    mark_spacer->setFixedWidth(RunTableColumns::mark);
    mark_spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
    line->addWidget(mark_spacer);
    QLabel *name_head = label(PageRun::tr("文件 / 输出"), Qt::AlignLeft, 0, "pp-run-thead-name");
    name_head->setMinimumWidth(0); // C10/§5.1：窄窗下名称列可压（不靠文字撑宽）
    line->addWidget(name_head, 1);
    line->addWidget(
        label(PageRun::tr("进度"), Qt::AlignLeft, RunTableColumns::bar, "pp-run-thead-bar"));
    line->addWidget(
        label(PageRun::tr("大小"), Qt::AlignRight, RunTableColumns::size, "pp-run-thead-size"));
    line->addWidget(
        label(PageRun::tr("耗时"), Qt::AlignRight, RunTableColumns::time, "pp-run-thead-time"));
    // M8-W2（需求3）：表头「评分」列（按与行内单元格同 x 断言对齐）
    line->addWidget(
        label(PageRun::tr("评分"), Qt::AlignRight, RunTableColumns::score, "pp-run-thead-score"));
    line->addWidget(
        label(PageRun::tr("状态"), Qt::AlignRight, RunTableColumns::status, "pp-run-thead-status"));
    return head;
}

// 总览卡（§7.3 第一行：进度合成条 ｜ kv 八项 ｜ 操作行 + 摘要句）
void build_overview_card(QWidget *parent, RunState *s) {
    const formkit::CardParts parts =
        formkit::make_card(parent, PageRun::tr("运行总览"), QStringLiteral("pp-run-state-pill"),
                           QStringLiteral("pp-run-progress-hint"));
    s->overview = parts.frame;
    s->state_pill = parts.pill_badge();
    s->state_pill->setVisible(true); // card_header 初值隐藏（有内容才画）；状态胶囊常驻
    s->state_pill->set_tokens(s->tokens);
    s->state_pill->setText(PageRun::tr("就绪"));
    s->progress_hint = parts.hint_label();

    QWidget *body = parts.body;
    auto *body_v = qobject_cast<QVBoxLayout *>(body->layout());

    // 行 1：进度（R2 单列行：标签轨 + 轨内 = 合成条 + 百分比读数）
    {
        auto *host = new QWidget(body);
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(8);
        s->progress = new RunProgressBar(host);
        s->progress->setObjectName(QStringLiteral("pp-run-progress"));
        s->progress->set_tokens(s->tokens);
        s->progress->setAccessibleName(PageRun::tr("运行进度")); // §11：progressbar 语义
        hl->addWidget(s->progress, 1);
        s->percent = new ElidedLabel(host);
        s->percent->setObjectName(QStringLiteral("pp-run-percent"));
        s->percent->setFont(theme::font(theme::Typography::body_px, QFont::Normal, true));
        s->percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        s->percent->setFixedWidth(38); // mockup：<span class="kv-v" style="width:38px">
        hl->addWidget(s->percent);
        const fk::Row row = fk::frow(body, PageRun::tr("进度"), host, fk::FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-run-progress-row"));
        body_v->addWidget(row.row);
    }

    // kv 4 行（§7.3 逐字：总耗时/吞吐 · 平均/并行 · 交错启动/线程预算 · 汇总/峰值内存）
    {
        struct KvCell {
            QString key;
            QString object_name;
            ElidedLabel **out;
        };
        struct KvLine {
            KvCell left;
            KvCell right;
        };
        const KvLine lines[4] = {
            {{PageRun::tr("总耗时"), QStringLiteral("pp-run-elapsed"), &s->elapsed},
             {PageRun::tr("吞吐"), QStringLiteral("pp-run-throughput"), &s->throughput}},
            {{PageRun::tr("平均"), QStringLiteral("pp-run-avg"), &s->average},
             {PageRun::tr("并行"), QStringLiteral("pp-run-workers"), &s->workers}},
            {{PageRun::tr("交错启动"), QStringLiteral("pp-run-stagger"), &s->stagger},
             {PageRun::tr("线程预算"), QStringLiteral("pp-run-budget"), &s->budget}},
            {{PageRun::tr("汇总"), QStringLiteral("pp-run-tally"), &s->tally},
             {PageRun::tr("峰值内存"), QStringLiteral("pp-run-memory"), &s->memory}},
        };
        for (const KvLine &line : lines) {
            const auto value_cell = [body](const QString &object_name) {
                auto *value = new ElidedLabel(body);
                value->setObjectName(object_name);
                value->setFont(theme::font(theme::Typography::body_px, QFont::Normal, true));
                value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
                value->setText(QStringLiteral("—"));
                return value;
            };
            fk::FieldCell left;
            left.label = line.left.key;
            left.width = fk::FieldWidth::Flex;
            left.field = value_cell(line.left.object_name);
            fk::FieldCell right;
            right.label = line.right.key;
            right.width = fk::FieldWidth::Flex;
            right.field = value_cell(line.right.object_name);
            const fk::DoubleRow row = fk::double_row(body, left, right);
            row.row->setObjectName(line.left.object_name + QStringLiteral("-row"));
            body_v->addWidget(row.row);
            *line.left.out = static_cast<ElidedLabel *>(left.field);
            *line.right.out = static_cast<ElidedLabel *>(right.field);
        }
        // R-078（M7-R2D2）：峰值线程读数行 —— 消费 RunSummary::threads_total_est（WP8 加性
        // 字段）；未结束/未采集显「—」不编数（M5-A24 口径），估算口径进 tooltip。
        {
            auto *value = new ElidedLabel(body);
            value->setObjectName(QStringLiteral("pp-run-threads"));
            value->setFont(
                theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            value->setProperty(theme::kFkProperty, QString::fromLatin1(theme::kFkValue));
            value->setText(QStringLiteral("—"));
            s->threads = value;
            const fk::Row row =
                fk::frow(body, PageRun::tr("峰值线程"), value, fk::FieldWidth::Flex);
            row.row->setObjectName(QStringLiteral("pp-run-threads-row"));
            body_v->addWidget(row.row);
        }
    }

    // 操作行（§7.3：查看日志…/打开输出目录/取消剩余/再次运行；三级动作层级，R4 高 26）
    {
        auto *host = new QWidget(body);
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(8);
        const auto link = [host](const QString &text, const QString &object_name) {
            auto *button = new QPushButton(text, host);
            button->setObjectName(object_name);
            button->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkLink));
            button->setFont(theme::font(theme::Typography::body_px, 600));
            button->setFixedHeight(theme::Metrics::btn_h);
            button->setCursor(Qt::PointingHandCursor);
            button->setAccessibleName(text); // §11
            button->setToolTip(text);
            return button;
        };
        s->open_logs = link(PageRun::tr("查看日志…"), QStringLiteral("pp-run-open-logs"));
        hl->addWidget(s->open_logs);
        s->open_output = link(PageRun::tr("打开输出目录"), QStringLiteral("pp-run-open-output"));
        hl->addWidget(s->open_output);
        s->cancel = new QPushButton(PageRun::tr("取消剩余"), host);
        s->cancel->setObjectName(QStringLiteral("pp-run-cancel"));
        // W-R-T13（A39 ②）：破坏性动作 = **红文字链**（视觉终稿动作层级：导航链 / 破坏性红链 /
        // 结果次钮三副面孔）。红描边钮形态退役（旧 "danger" 属性值不再出现在本页 QSS）。
        s->cancel->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkLinkDanger));
        s->cancel->setFont(theme::font(theme::Typography::body_px, 600));
        s->cancel->setFixedHeight(theme::Metrics::btn_h); // R4/A7 按钮高 26
        s->cancel->setCursor(Qt::PointingHandCursor);
        s->cancel->setAccessibleName(PageRun::tr("取消剩余")); // §11
        s->cancel->setToolTip(PageRun::tr("取消尚未开始的输出，已完成的输出保留"));
        hl->addWidget(s->cancel);
        hl->addStretch(1);
        s->run_again = new QPushButton(PageRun::tr("再次运行"), host);
        s->run_again->setObjectName(QStringLiteral("pp-run-again"));
        s->run_again->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkButton));
        s->run_again->setFont(theme::font(theme::Typography::body_px, 600));
        s->run_again->setFixedHeight(theme::Metrics::btn_h);
        s->run_again->setCursor(Qt::PointingHandCursor);
        s->run_again->setAccessibleName(PageRun::tr("再次运行")); // §11
        hl->addWidget(s->run_again);
        const fk::Row row = fk::frow(body, PageRun::tr("操作"), host, fk::FieldWidth::Flex);
        row.row->setObjectName(QStringLiteral("pp-run-ops-row"));
        body_v->addWidget(row.row);
    }

    // 摘要句（空态 = 引导；运行中空；结束 = 结果去向 —— **不含任何指标重复**，§2.5）
    s->summary = new ElidedLabel(s->overview);
    s->summary->setObjectName(QStringLiteral("pp-run-summary"));
    s->summary->setFont(theme::font(theme::Typography::small_px));
    s->summary->setProperty(kRunLabelProperty, QString::fromLatin1(kRunSummary));
    s->summary->setContentsMargins(kRowPadX, 6, kRowPadX, 0);
    body_v->addWidget(s->summary);

    QObject::connect(s->cancel, &QPushButton::clicked, s->page, [s]() {
        s->cancel_requested = true;
        // C15：取消是状态翻转（操作行 + 状态胶囊）→ 立即刷一帧 + 60ms 通道兜底
        mark_state(*s, -1);
        refresh_widgets(*s); // 立即反馈（不等 singleShot(0) 事件循环）
        emit s->page->cancel_requested();
    });
    QObject::connect(s->open_logs, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->open_logs_requested(); });
    QObject::connect(s->open_output, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->open_output_requested(s->out_root); });
    QObject::connect(s->run_again, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->run_again_requested(); });
}

// 任务进度卡（§7.3 第二行：表头 + 分组行 + 逐输出行 + 失败原因行）
void build_tasks_card(QWidget *parent, RunState *s) {
    const formkit::CardParts parts = formkit::make_card(parent, PageRun::tr("任务进度"), QString(),
                                                        QStringLiteral("pp-run-tasks-hint"));
    s->tasks = parts.frame;
    s->tasks_hint = parts.hint_label();
    s->tasks_hint->set_full_text(PageRun::tr("实心=已写盘 · 斜纹=合成中")); // mockup 逐字

    QWidget *body = parts.body;
    auto *body_v = qobject_cast<QVBoxLayout *>(body->layout());
    s->table_head = make_table_head(body);
    body_v->addWidget(s->table_head);
    s->list = new QWidget(body);
    s->list->setObjectName(QStringLiteral("pp-run-list"));
    s->list_layout = new QVBoxLayout(s->list);
    s->list_layout->setContentsMargins(0, 0, 0, 0);
    s->list_layout->setSpacing(0);
    body_v->addWidget(s->list);
    s->task_empty = new ElidedLabel(body);
    s->task_empty->setObjectName(QStringLiteral("pp-run-task-empty"));
    s->task_empty->setFont(theme::font(theme::Typography::body_px));
    s->task_empty->setProperty(kRunLabelProperty, QString::fromLatin1(kRunHead));
    s->task_empty->setContentsMargins(kRowPadX, 6, kRowPadX, 0);
    body_v->addWidget(s->task_empty);
}

// 日志尾卡（§7.3 第三行：等宽 4 行 + 查看完整日志…；卡头右置 = 日志名 + 文字链）
void build_logs_card(QWidget *parent, RunState *s) {
    const formkit::CardParts parts = formkit::make_card(parent, PageRun::tr("日志尾"), QString(),
                                                        QStringLiteral("pp-run-log-name"));
    s->logs = parts.frame;
    s->log_name = parts.hint_label();
    s->log_name->setProperty(kRunLabelProperty, QString::fromLatin1(kRunHead));
    QHBoxLayout *head = qobject_cast<QHBoxLayout *>(parts.head->layout());
    s->open_full_log = new QPushButton(PageRun::tr("查看完整日志…"), s->logs);
    s->open_full_log->setObjectName(QStringLiteral("pp-run-open-full-log"));
    s->open_full_log->setProperty(kFieldProperty, QString::fromLatin1(theme::kFkLink));
    s->open_full_log->setFont(theme::font(theme::Typography::body_px, 600));
    s->open_full_log->setFixedHeight(theme::Metrics::btn_h);
    s->open_full_log->setCursor(Qt::PointingHandCursor);
    s->open_full_log->setAccessibleName(PageRun::tr("查看完整日志…")); // §11
    s->open_full_log->setToolTip(PageRun::tr("在文件管理器中打开日志文件"));
    head->addWidget(s->open_full_log);

    QWidget *body = parts.body;
    auto *body_v = qobject_cast<QVBoxLayout *>(body->layout());
    auto *box = new QFrame(body);
    box->setObjectName(QStringLiteral("pp-run-log-box"));
    auto *box_v = new QVBoxLayout(box);
    box_v->setContentsMargins(10, 8, 10, 8);
    box_v->setSpacing(0);
    s->log_lines = new LogLines(box);
    s->log_lines->setObjectName(QStringLiteral("pp-run-log-lines"));
    s->log_lines->set_tokens(s->tokens);
    box_v->addWidget(s->log_lines);
    body_v->addWidget(box);

    QObject::connect(s->open_full_log, &QPushButton::clicked, s->page,
                     [s]() { emit s->page->open_logs_requested(); });
}

// 行控件（分组行 + 逐输出子行 + 失败原因行）：先建后填（N 格式 N 行；§7.3）
void build_rows(RunState *s, const QStringList &names) {
    const int formats = s->formats;
    s->row_dirty.assign(static_cast<std::size_t>(names.size()), true); // C15：新行全脏（首帧全量）
    for (int i = 0; i < names.size(); ++i) {
        const RowSkeleton skel =
            make_row_skeleton(s->list, group_names(i), s->tokens, /*output_row=*/false);
        FileRow file;
        file.index = i; // C15：dirty-row 行键（apply_progress/apply_terminal 标记用）
        file.group = skel.row;
        file.mark = skel.mark;
        file.name = skel.name;
        file.bar = skel.bar;
        file.size = skel.size;
        file.time = skel.time;
        file.score_cell = skel.score_cell;
        file.score = skel.score;
        file.status = skel.status;
        file.name->set_full_text(row_name(names.at(i))); // 修 3-13：不再重复「N 个输出」
        s->list_layout->addWidget(file.group);

        if (i < s->plan.heights.size())
            file.height = s->plan.heights.at(i);
        if (i < s->plan.widths.size())
            file.width = s->plan.widths.at(i);
        if (i < s->plan.bytes.size())
            file.src_bytes = s->plan.bytes.at(i);
        // C10（M10-W1）：名称列 tooltip = **完整路径**（组行附 宽×高 · 大小）；显示文本仍是
        // 文件名（中间省略），语义全文挂 ppFullText（ElidedLabel::set_full_text 已写）。
        file.name->setToolTip(group_name_tip(names.at(i), file.width, file.height, file.src_bytes));

        file.outs.reserve(formats);
        for (int j = 0; j < formats; ++j) {
            const RowSkeleton orow =
                make_row_skeleton(s->list, out_names(i, j), s->tokens, /*output_row=*/true);
            OutRow out;
            out.row = orow.row;
            out.mark = orow.mark;
            out.name = orow.name;
            out.bar = orow.bar;
            out.size = orow.size;
            out.time = orow.time;
            out.score_cell = orow.score_cell;
            out.score = orow.score;
            out.status = orow.status;
            const QString fmt_id =
                j < s->plan.format_ids.size() ? s->plan.format_ids.at(j) : QString();
            out.name->set_full_text(QStringLiteral("└ %1").arg(format_name(fmt_id)));
            // C10（M10-W1）：输出行名称列 tooltip = 源文件完整路径 + 输出格式（格式名本身不会长，
            // 但仍统一走 ElidedLabel/tooltip 口径）
            out.name->setToolTip(out_name_tip(names.at(i), fmt_id));
            s->list_layout->addWidget(out.row);

            // 失败行原因行（默认隐藏；§7.3「失败行含 └ 原因：… 行」）
            out.reason_row = make_table_row(
                s->list, QStringLiteral("pp-run-reason-%1-%2").arg(i).arg(j), fk::kRowPitch);
            out.reason = new ElidedLabel(out.reason_row);
            out.reason->setObjectName(QStringLiteral("pp-run-reason-text-%1-%2").arg(i).arg(j));
            out.reason->setFont(theme::font(theme::Typography::small_px));
            out.reason->setProperty(kRunLabelProperty, QString::fromLatin1(kRunReason));
            set_leading_indent(out.reason, kReasonIndent); // R-218：逻辑边距（LTR=左/RTL=右）
            auto *reason_line = qobject_cast<QHBoxLayout *>(out.reason_row->layout());
            reason_line->addWidget(out.reason, 1);
            out.reason_row->setVisible(false);
            s->list_layout->addWidget(out.reason_row);

            // 成功但带 warning 的提示行（独立 objectName，避免与失败原因探针混面）
            out.warn_row = make_table_row(
                s->list, QStringLiteral("pp-run-warn-%1-%2").arg(i).arg(j), fk::kRowPitch);
            out.warn = new ElidedLabel(out.warn_row);
            out.warn->setObjectName(QStringLiteral("pp-run-warn-text-%1-%2").arg(i).arg(j));
            out.warn->setFont(theme::font(theme::Typography::small_px));
            out.warn->setProperty(kRunLabelProperty, QString::fromLatin1(kRunReason));
            set_leading_indent(out.warn, kReasonIndent);
            auto *warn_line = qobject_cast<QHBoxLayout *>(out.warn_row->layout());
            warn_line->addWidget(out.warn, 1);
            out.warn_row->setVisible(false);
            s->list_layout->addWidget(out.warn_row);

            file.outs.append(out);
        }
        s->files.append(file);
    }
}

} // namespace

// ===========================================================================
// PageRun（公开面）
// ===========================================================================

// R-185：析构擦除（与 PageOutput::~PageOutput 同款）——unique_ptr 自动释放 RunState；
// 释放时点在 ~PageRun 体内（原 destroyed 信号 delete/take 在 ~QObject 段；RunState 析构
// 无副作用、全部访问点对 state_of 判空，两时点等价）。
PageRun::~PageRun() { registry().erase(this); }

PageRun::PageRun(QWidget *parent) : QWidget(parent) {
    Q_ASSERT(registry().find(this) == registry().end());
    // R-185：RAII 注册表 —— unique_ptr 所有权入表（原裸 new + destroyed 信号 delete/take；
    // 释放点收敛到 ~PageRun 的 registry().erase，与 PageOutput 同款）
    auto owned = std::make_unique<RunState>();
    RunState *s = owned.get();
    s->page = this;
    registry().emplace(this, std::move(owned));

    // R-009（M7-WP4）：构造期 tokens 取**当前生效档**（原硬编码 Dark → 浅色主题整页深色绘制）
    s->tokens = theme::tokens(theme::preferred_theme_mode());
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pp-run-scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 竖滚动条常驻：内容高随运行态变化会把滚动条"顶"出来/收回去，内容宽随之 ±15px，
    // 行内等分列整体横移（与 page_output / page_meta 同口径）
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    outer->addWidget(scroll);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("pp-run-content"));
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(theme::Metrics::gap); // 卡间距 8（§4.3）
    scroll->setWidget(content);

    build_overview_card(content, s);
    build_tasks_card(content, s);
    build_logs_card(content, s);
    v->addWidget(s->overview);
    v->addWidget(s->tasks);
    v->addWidget(s->logs);
    v->addStretch(1);
    // UX-11（M7-R2D2）：卡片防压缩 —— 滚动区(widgetResizable) 把 Preferred 件压向
    // minimumSizeHint；三卡按当前内容 sizeHint 钉下限，滚动条只在卡片整体超出时出现。
    formkit::lock_card_size(s->overview);
    formkit::lock_card_size(s->tasks);
    formkit::lock_card_size(s->logs);

    // ---- UX-1（M7-WP6）：三张卡各一个右键菜单（公共件 = ui/contextmenu）----
    attach_card_menu(s->overview, [this, s] {
        contextmenu::RunOverviewIntents in;
        in.running = s->running;
        in.open_log = [this] { emit open_logs_requested(); };
        in.open_output_dir = [this, s] { emit open_output_requested(s->out_root); };
        in.cancel_remaining = [this] { emit cancel_requested(); };
        in.run_again = [this] { emit run_again_requested(); };
        return contextmenu::run_overview_actions(in);
    });
    attach_card_menu(s->tasks, [s] {
        contextmenu::TaskRowIntents in;
        in.running = s->running;
        // 复制失败原因 = 当前可见的全部「└ 原因：…」行（R-144 的下钻替代面：失败清单可带走）
        in.copy_failure = [s] {
            QStringList reasons;
            for (const FileRow &file : s->files) {
                for (const OutRow &out : file.outs) {
                    if (out.reason == nullptr || !out.reason->isVisible())
                        continue;
                    // ElidedLabel 的完整文本在 ppFullText 动态属性（省略号版在 text()）
                    const QVariant full = out.reason->property("ppFullText");
                    const QString text = full.isValid() ? full.toString() : out.reason->text();
                    if (!text.isEmpty())
                        reasons << text;
                }
            }
            if (QClipboard *clip = QGuiApplication::clipboard())
                clip->setText(reasons.join(QLatin1Char('\n')));
        };
        // 「跳到该文件 / 重试该文件」需要行级上下文（逐输出行的右键）；本轮未接线 → 置灰，
        // 见 build/m7-wp6/handoff.md 残余项。
        return contextmenu::task_row_actions(in);
    });
    attach_card_menu(s->logs, [s] {
        contextmenu::LogTailIntents in;
        in.running = s->running;
        in.copy_log = [s] {
            if (QClipboard *clip = QGuiApplication::clipboard())
                clip->setText(s->log_lines != nullptr
                                  ? s->log_lines->lines().join(QLatin1Char('\n'))
                                  : QString());
        };
        in.open_log_file = [s] {
            if (s->page != nullptr)
                emit s->page->open_logs_requested();
        };
        in.clear_display = [s] {
            if (s->log_lines != nullptr)
                s->log_lines->set_lines({});
        };
        return contextmenu::log_tail_actions(in);
    });

    setStyleSheet(page_qss(s->tokens));

    s->flush = new QTimer(this);
    s->flush->setInterval(kFlushMs); // §7.4：GUI 节流 60ms
    // C15（T9a）：60ms 定时器 = **进度通道**（数值/百分比合并）；状态翻转走 mark_state 的
    // singleShot(0) 立即刷（见 flush_state/flush_progress）。
    connect(s->flush, &QTimer::timeout, this, [s]() { flush_progress(*s); });
    reset(); // 空闲态（就绪 + 空任务表 + 引导文案）
}

void PageRun::set_plan(const RunPlan &plan) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->plan = plan;
    if (s->files.isEmpty())
        s->formats = plan.format_ids.isEmpty() ? 1 : plan.format_ids.size();
}

void PageRun::set_tokens(const theme::Tokens &tokens) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->tokens = tokens;
    setStyleSheet(page_qss(tokens));
    // W-R-T13（A39 ②）：取消剩余已是**文字链**（mockup run.html 的 .btn.lnk 逐字：与「查看日志…」
    // 「打开输出目录」同形、仅换色）→ 钮上不再挂矢量图标（旧 danger 钮时代的 ■ 已随形态一并退役；
    // 取色交给 QSS 的 link-danger 档）。
    s->progress->set_tokens(tokens);
    s->state_pill->set_tokens(tokens);
    for (FileRow &file : s->files) {
        file.bar->set_tokens(tokens);
        file.status->set_tokens(tokens);
        for (OutRow &out : file.outs) {
            out.bar->set_tokens(tokens);
            out.status->set_tokens(tokens);
        }
    }
    s->log_lines->set_tokens(tokens);
    // 浅色卡阴影（§4.1 shadow 0 1px 4px rgba(16,24,40,.07)；深色无阴影）
    const qreal dpr = devicePixelRatioF() > 0.01 ? devicePixelRatioF() : 1.0;
    QFrame *const cards[] = {s->overview, s->tasks, s->logs};
    for (QFrame *card : cards) {
        if (card == nullptr)
            continue;
        if (tokens.shadow_blur > 0) {
            auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
            if (shadow == nullptr) {
                shadow = new QGraphicsDropShadowEffect(card);
                card->setGraphicsEffect(shadow);
            }
            shadow->setColor(tokens.shadow_color);
            shadow->setBlurRadius(double(tokens.shadow_blur) * dpr);
            shadow->setXOffset(0);
            shadow->setYOffset(double(tokens.shadow_dy) * dpr);
        } else if (card->graphicsEffect() != nullptr) {
            card->setGraphicsEffect(nullptr); // 删旧效果（含析构）
        }
    }
    refresh_widgets(*s);
}

void PageRun::set_log_tail(const QStringList &lines, const QString &log_name) {
    RunState *s = state_of(this);
    if (!s)
        return;
    // §7.3：只渲染末 4 行（真实尾部）；**有日志才有日志名**（修 3-14 的自相矛盾状态）
    s->log_tail = lines.mid(std::max(0, static_cast<int>(lines.size()) - kLogTailLines));
    s->log_name_text = s->log_tail.isEmpty() ? QString() : log_name;
    refresh_widgets(*s);
}

int PageRun::outputs_total() const {
    const RunState *s = state_of(this);
    return s != nullptr ? static_cast<int>(s->files.size()) * s->formats : 0;
}

int PageRun::outputs_done() const {
    const RunState *s = state_of(this);
    return s != nullptr ? settled_outputs(*s) : 0;
}

int PageRun::files_started() const {
    const RunState *s = state_of(this);
    return s != nullptr ? static_cast<int>(s->started_files) : 0;
}

int PageRun::files_terminal() const {
    const RunState *s = state_of(this);
    return s != nullptr ? static_cast<int>(s->terminal_files) : 0;
}

int PageRun::progress_percent() const {
    const RunState *s = state_of(this);
    if (s == nullptr || s->files.isEmpty())
        return -1;
    return static_cast<int>(std::lround(run_frac(*s) * 100.0));
}

int PageRun::eta_seconds() const {
    const RunState *s = state_of(this);
    if (s == nullptr || !s->running)
        return -1;
    const double frac = run_frac(*s);
    if (frac <= 0.001)
        return -1;
    const double elapsed_s = static_cast<double>(s->clock.elapsed()) / 1000.0;
    return static_cast<int>(std::lround(elapsed_s * (1.0 - frac) / frac));
}

void PageRun::begin_run(std::size_t total, const QStringList &names) {
    RunState *s = state_of(this);
    if (!s)
        return;
    reset();
    s->formats = s->plan.format_ids.isEmpty() ? 1 : s->plan.format_ids.size();
    s->running = true;
    build_rows(s, names);
    if (static_cast<std::size_t>(names.size()) != total) {
        qWarning("PageRun::begin_run: total=%llu but names=%lld",
                 static_cast<unsigned long long>(total), static_cast<long long>(names.size()));
    }
    s->progress->setRange(0, 100); // 值 = 百分比（无障碍暴露值与显示一致，修 3-07）
    s->progress->setValue(0);
    s->summary->set_full_text(QString());
    s->open_logs->setEnabled(true);    // 运行期可跳日志（§9.3：跳转入口保持可用）
    s->open_output->setEnabled(false); // 输出目录在运行结束后开放（避免半成品目录误导）
    s->clock.start();
    s->flush->start();
    refresh_widgets(*s);
    s->dirty_progress = false;
    s->dirty_state = false;
    s->overview_dirty = false;
}

void PageRun::on_event(const pp::FileEvent &ev) {
    RunState *s = state_of(this);
    if (!s)
        return;
    const int index = static_cast<int>(ev.index);
    if (index < 0 || index >= s->files.size())
        return;
    FileRow &file = s->files[index];
    // ---- M10-W1（T9b；C13/C14）：逐输出终态事件 ----
    // OutputDone 是**输出级**事实（payload = 该行快照，见 pipeline.h 的 A2 注释），不是文件级
    // 阶段：直接结算该行并返回，不喂 apply_progress（否则快照的 stage=Done 会覆写行内阶段）。
    if (ev.state == pp::FileState::OutputDone) {
        if (ev.output) {
            int out_index = ev.progress.output_index;
            if (out_index < 0 || out_index >= file.outs.size()) {
                // 兜底：按格式 id 匹配配置顺序（正常路径 progress.output_index 恒 = cfg_index）
                out_index = -1;
                const QString fmt = QString::fromStdString(ev.output->format_id);
                for (int j = 0; j < s->plan.format_ids.size() && j < file.outs.size(); ++j) {
                    if (s->plan.format_ids.at(j) == fmt) {
                        out_index = j;
                        break;
                    }
                }
            }
            apply_output_done(*s, file, out_index, *ev.output);
        }
        return;
    }
    // ---- 状态机修复 S-1：迟到过程事件不得覆写**文件终态** ----
    // 评分池的看门狗心跳（FileState::Progress + heartbeat=true）与取消后残余过程事件可能晚于
    // 文件终态事件到达。旧实现会继续走 apply_progress 与非终态分支的 `file.state = ev.state`，
    // 把 Done/Failed/Skipped/Cancelled 打回 Progress ⇒ 组行胶囊从「已完成/失败/跳过」退回
    // 「写入/色彩」进行中、完成态唯一形态丢失（用户可观察的状态回退）。
    // 终态是文件级定稿事实：终态后的一切**过程**事件直接丢弃；OutputDone（输出级事实）已在
    // 上方分支放行 —— 它的用途是补评分列，不是文件级过程态。
    if (file.terminal && !is_terminal_state(ev.state))
        return;
    apply_progress(*s, file, ev.progress); // C15：内部按「状态翻转/数值」入双通道并标行脏
    if (ev.state == pp::FileState::Queued)
        return;
    if (!is_terminal_state(ev.state)) {
        if (!file.started) {
            file.started = true;
            ++s->started_files;
            mark_state(*s, index);
        }
        file.state = ev.state;
        return;
    }
    apply_terminal(*s, file, ev); // 终态：内部 mark_state（行 + 总览）
}

void PageRun::end_run(const pp::RunSummary &sum, const QString &out_root) {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->flush->stop();
    s->running = false;
    s->finished = true;
    s->out_root = out_root;
    s->out_bytes = sum.out_bytes;
    s->has_summary = true;
    s->summary_total_ms = sum.total_ms;
    s->summary_mb_s = sum.throughput_mb_s;
    s->summary_avg_ms = sum.avg_file_ms;
    s->summary_peak_bytes = sum.peak_bytes;         // A29（加性字段；M4 前恒 0 → 读数「—」）
    s->summary_threads_est = sum.threads_total_est; // R-078（WP8 加性字段；估算口径见 tooltip）
    // 状态机修复 S-2：结束态的「已取消」= **实际取消效果**（引擎汇总的 cancelled 文件数），
    // 不是「点过取消按钮」。用户在收尾阶段点了「取消剩余」但全部文件已成功时，旧实现恒显示
    // 「已取消」（与汇总「成功 N」/摘要「可打开输出目录」同屏矛盾）；现在如实回落「已完成」。
    s->cancel_requested = sum.cancelled > 0;
    // 防御性收尾：未收到终态事件的行按结果定稿（正常路径调度器对每文件恰好发一次终态）
    // R-141/R-225：**同时回写 file.state** —— 原实现只定稿 out.kind，组行胶囊仍停在"排队/处理中"，
    // 与其下输出行"失败/已取消"同屏矛盾。
    for (FileRow &file : s->files) {
        if (file.terminal)
            continue;
        file.terminal = true;
        for (OutRow &out : file.outs) {
            if (!settled_kind(out.kind))
                out.kind = sum.cancelled > 0 ? OutKind::Cancelled : OutKind::Failed;
        }
        // 组行终态与输出行同判：全部取消 → Cancelled；否则按"有失败 = Failed、否则 Done"。
        bool any_failed = false, any_cancelled = false, any_ok = false;
        for (const OutRow &out : file.outs) {
            any_failed = any_failed || out.kind == OutKind::Failed;
            any_cancelled = any_cancelled || out.kind == OutKind::Cancelled;
            any_ok = any_ok || out.kind == OutKind::Done;
        }
        file.state = any_cancelled && !any_ok && !any_failed ? pp::FileState::Cancelled
                     : any_failed && any_ok                  ? pp::FileState::DoneWithErrors
                     : any_failed                            ? pp::FileState::Failed
                                                             : pp::FileState::Done;
    }
    // M10-W1（T9b；C14）：评分列收尾 —— 外壳已在 end_run 前等评分池收敛并排干在途事件
    // （ScorePool::wait_idle + processEvents；见 mainwindow_run.cpp）。仍未回分数的成功行
    // 如实显示「—」+ 原因（超时/取消：不留永远转不完的「评分中」）。
    for (FileRow &file : s->files) {
        for (OutRow &out : file.outs) {
            if (!out.score_wait)
                continue;
            out.score_wait = false;
            if (out.metric_state == pp::MetricState::Off) {
                out.metric_state = pp::MetricState::Unavailable;
                out.metric_note = tr("运行结束前未收到评分结果");
            }
        }
    }
    s->progress->setRange(0, 100);
    s->open_output->setEnabled(!out_root.isEmpty());
    s->open_logs->setEnabled(true);
    // R-006：取消文案与**实际语义**一致 —— 「再次运行」现在是补齐语义（只提交缺件、
    // 已存在且非空的输出跳过），故这句不再是空头支票。
    // R-073：结束语按结果分档，但**只在全批失败**时换失败档 —— 原句"可打开输出目录查看结果"
    // 对"有产出"的批次依然成立（ui_smoke 的 single-source 探针逐字钉住该句，见 T14）。
    if (sum.cancelled > 0)
        s->summary->set_full_text(tr("已取消 · 剩余文件未处理（可再次运行补齐缺件）"));
    else if (sum.failed > 0 && sum.ok == 0 && sum.skipped == 0)
        s->summary->set_full_text(
            tr("运行结束 · 全部 %1 个失败，无可查看的输出（可再次运行补齐缺件）").arg(sum.failed));
    else
        s->summary->set_full_text(tr("运行结束 · 可打开输出目录查看结果"));
    refresh_widgets(*s); // 终态恒满（含失败；口径 = 全部输出已结算）—— 全量刷新（C15）
    s->dirty_progress = false;
    s->dirty_state = false;
    s->overview_dirty = false;
}

void PageRun::reset() {
    RunState *s = state_of(this);
    if (!s)
        return;
    s->flush->stop();
    s->clock.invalidate();
    s->running = false;
    s->finished = false;
    s->cancel_requested = false;
    s->dirty_progress = false;
    s->dirty_state = false;
    s->state_flush_pending = false;
    s->overview_dirty = false;
    s->row_dirty.clear(); // 行随 files.clear() 一并销毁（C15：脏位表与 files 同长）
    s->started_files = 0;
    s->terminal_files = 0;
    s->out_bytes = 0;
    s->out_root.clear();
    s->has_summary = false;
    s->summary_total_ms = 0.0;
    s->summary_mb_s = 0.0;
    s->summary_avg_ms = 0.0;
    s->summary_peak_bytes = 0;
    s->summary_threads_est = 0;
    for (FileRow &file : s->files) {
        delete file.group;
        for (OutRow &out : file.outs) {
            delete out.row;
            delete out.reason_row;
            delete out.warn_row;
        }
    }
    s->files.clear();
    s->progress->setRange(0, 100);
    s->progress->setValue(0);
    s->progress->set_segments(0.0, 0.0, 0.0, 0.0, 0.0);
    s->percent->set_full_text(QStringLiteral("0%"));
    s->summary->set_full_text(tr("点击底栏「开始运行」开始批处理"));
    s->task_empty->set_full_text(tr("尚未开始 · 运行中逐输出行在此展开"));
    s->open_output->setEnabled(false);
    s->open_logs->setEnabled(false);
    refresh_widgets(*s);
}

bool PageRun::is_running() const {
    const RunState *s = state_of(this);
    return s != nullptr && s->running;
}

// M10-W1（T9b；A10 追加面）：本批是否真的会走异步评分（RunPlan::quality_score 且非仅元数据）。
// 外壳据此决定取消/收尾是否接评分池（cancel_pending / wait_idle）—— 无评分的运行不建池线程。
bool PageRun::scoring_enabled() const {
    const RunState *s = state_of(this);
    return s != nullptr && s->plan.quality_score && !s->plan.metadata_only;
}

} // namespace pp::ui
