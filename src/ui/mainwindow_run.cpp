// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — mainwindow_run.cpp（M7-WP9 / R-049·R-252-260 按数据所有权拆 TU）。
//
// 域：运行控制（开始/再次运行/取消/收尾/锁面板）+ 运行事件与日志尾 + 读盘预检。
// 数据所有权 = Impl 的运行状态面（running/run_total/run_rows/…）与 Scheduler 生命周期。
// 拆分口径 = 行为零变化：实现自 mainwindow.cpp 逐字搬入；objectName / 信号 / 方法签名
// 逐位不变（金样 20 对 + UI-SMOKE 冻结行背书，见 build/m7-wp9/handoff.md）。
#include "ui/formkit.h" // M5-T7：meta-geom 探针读回卡头胶囊（formkit::PillBadge）与 R1/R2 几何
#include "ui/icons.h"
#include "ui/mainwindow.h"

#include <QAbstractButton> // M6-T11：图层 Seg 的段（QAbstractButton）驱动回填
#include <QAbstractItemView>
#if QT_CONFIG(accessibility)
#include <QAccessible> // §11/A18：ShellNav 的 tab/button group 语义（自建接口 + 工厂）
#include <QAccessibleActionInterface>
#include <QAccessibleWidget>
#endif
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QClipboard> // UX-1：「复制路径」等菜单动作
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime> // R-144：失败清单时间戳文件名
#include <QDesktopServices>
#include <QDialog> // M6-T13：meta-map-expand 探针在模态循环内取证大地图对话框
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics> // M5-T14：offline-banner 环带取样的文字盒（QFontMetricsF 同头）
#include <QFrame>
#include <QFutureWatcher> // R-081：日志尾异步读回 GUI 线程
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget> // M6-T9：meta-loop 探针读结果列表（可点行/占位行）
#include <QMargins>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QProxyStyle> // M5-T14：文件列表系统焦点框抑制（QTreeView::drawRow 的焦点框调用不带
#include <QtConcurrent/QtConcurrentRun> // R-081：日志尾 IO 下后台
// widget → QSS outline:none 无效；见 FileViewFocusFrameStyle 说明）
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker> // R-153：filter_clear 一次 apply_filter（屏蔽两枚开关的 toggled）
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStorageInfo> // A19（T11）：底栏「剩余磁盘」段的卷可用空间读数
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/fsops.h" // R-006/R-073：补齐预检的路径渲染 + 输出根试建/试写探针
#include "core/logger.h"
#include "core/params.h"   // R-006：find_format（补齐预检取目标扩展名）
#include "core/pipeline.h" // 仅元数据：源格式解析 / path_ctx_for 单源
#include "core/settings.h"
#include "core/version.h" // R-150：版本徽标取 PP_VERSION_STRING（单源 = CMakeLists.txt:19 生成链）
#include "decode/oiio_reader.h" // M4-T12：探针的 probe 摘要（与列表模型同一条 oiio probe 通道）
#include "mapwidget/mapwidget.h"
#include "platform/frameless.h"
#include "platform/mica.h"
#include "platform/paths.h"
#include "ui/classify_panel.h"
#include "ui/contextmenu.h" // UX-1：公共右键菜单件（单一构造点 + 12 类卡 actions）
#include "ui/exif_editor.h"
#include "ui/filelistmodel.h"
#include "ui/page_meta.h"
#include "ui/page_output.h"
#include "ui/page_run.h"
#include "ui/paramform.h"
#include "ui/preset_io.h"
#include "ui/presets_dialog.h"
#include "ui/preview_panel.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"

#include "ui/mainwindow_impl.h" // Impl/壳层件共享层

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <winnt.h>
#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif
#ifndef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT __declspec(dllimport)
#endif
#include <shellapi.h>
// clang-format on
#endif

namespace pp::ui {
namespace {
using theme::preferred_theme_mode;

// ---------------------------------------------------------------------------
// M7-WP6 / R-055：错误弹窗文案抽助手
//   原状：`T("无法开始")` ×5（on_start 五个分支）、`T("未勾选任何文件")` 双源（on_start /
//   refresh_status）、预设成败标题各 2 处（load_preset_file / save_preset_named /
//   delete_preset_file）。同一文案不再散落多处 —— 改一处即全改。
// ---------------------------------------------------------------------------
void warn_cannot_start(QWidget *parent, const QString &reason) {
    QMessageBox::warning(parent, T("无法开始"), reason);
}

#ifdef _WIN32
// 2026-10：输出目录权限不足时先**申请权限**（UAC + 只给该目录授予当前用户修改权），
// 用户拒绝/申请失败后才回落到原错误。避免"直接报错、用户不知道还能授权"。
bool error_looks_like_permission(const std::string &error) {
    const QString e = QString::fromStdString(error).toLower();
    // "not writable" 来自 probe_output_root 的试写分支（只有路径、不带 OS 错误文本）——
    // 该分支恰好是「目录已存在但当前用户无写权」的主场景，必须触发申请流程，
    // 否则兜底错误里「再次运行并授权」的提示是死路。
    return e.contains(QStringLiteral("access is denied")) ||
           e.contains(QStringLiteral("permission denied")) ||
           e.contains(QStringLiteral("not writable")) || e.contains(QStringLiteral("不可写")) ||
           e.contains(QStringLiteral("read-only")) || e.contains(QStringLiteral("只读")) ||
           e.contains(QStringLiteral("拒绝访问")) || e.contains(QStringLiteral("权限不足")) ||
           e.contains(QStringLiteral("权限"));
}

bool grant_output_dir_permission(QWidget *parent, const std::filesystem::path &root) {
    if (root.empty())
        return false;
    const QString root_text = QString::fromStdWString(root.wstring());
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, T("需要写入权限"),
        T("输出目录当前不可写：\n%1\n\n是否申请管理员权限，仅为该目录授予当前用户"
          "“修改”权限？\n\nUAC 被取消或授权失败时，仍会显示原始错误。")
            .arg(root_text),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return false;

    wchar_t user[256] = {};
    DWORD user_len = static_cast<DWORD>(std::size(user));
    if (!::GetUserNameW(user, &user_len) || user_len == 0)
        return false;
    QString account = QString::fromWCharArray(user);
    wchar_t domain[256] = {};
    if (const DWORD n =
            ::GetEnvironmentVariableW(L"USERDOMAIN", domain, static_cast<DWORD>(std::size(domain)));
        n > 0 && n < std::size(domain)) {
        account = QString::fromWCharArray(domain) + QLatin1Char('\\') + account;
    }

    // 目标目录可能尚不存在：cmd 内先按需创建，再授权；路径整体加引号。
    // 安全边界（审计 R3）：cmd /c 会对 `%VAR%` 做环境变量展开、双引号是唯一分隔符。
    // 路径/账户名含 `%` 时「授权错目录」是理论可达的，故直接放弃提权（回落到原始错误，
    // 用户可改用不含 % 的目录）；双引号从路径剔除，避免参数截断。
    if (root_text.contains(QLatin1Char('%')) || account.contains(QLatin1Char('%'))) {
        pp::log_warn("run", "mainwindow_run.cpp",
                     "permission elevation skipped: '%' in path/account (cmd expansion risk)",
                     {{"out_root", root.string()}});
        return false;
    }
    QString safe_root = root_text;
    safe_root.replace(QLatin1Char('"'), QString());
    const QString params =
        QStringLiteral(
            "/c if not exist \"%1\" mkdir \"%1\" & icacls \"%1\" /grant \"%2:(OI)(CI)M\"")
            .arg(safe_root, account);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.nShow = SW_HIDE;
    if (!::ShellExecuteExW(&sei) || sei.hProcess == nullptr) {
        pp::log_warn(
            "run", "mainwindow_run.cpp", "permission elevation cancelled/failed",
            {{"out_root", root.string()}, {"win_error", std::to_string(::GetLastError())}});
        return false;
    }
    const DWORD wait = ::WaitForSingleObject(sei.hProcess, 120000);
    DWORD code = 1;
    ::GetExitCodeProcess(sei.hProcess, &code);
    ::CloseHandle(sei.hProcess);
    pp::log_warn("run", "mainwindow_run.cpp", "permission elevation finished",
                 {{"out_root", root.string()},
                  {"wait", std::to_string(wait)},
                  {"exit", std::to_string(code)}});
    return wait == WAIT_OBJECT_0 && code == 0;
}
#endif

QString permission_error_text(const std::string &detail) {
#ifdef _WIN32
    return T("输出目录不可用：%1\n\n请更换输出根目录后重试；若该目录需要管理员权限，"
             "可再次点击「开始运行」并在权限提示中选择“是”，然后在 UAC 窗口中允许。")
        .arg(QString::fromStdString(detail));
#else
    return T("输出目录不可用：%1\n\n请更换有写入权限的输出根目录后重试。")
        .arg(QString::fromStdString(detail));
#endif
}

// ---------------------------------------------------------------------------
// M7-WP6 / R-006：「再次运行」补齐语义的**只读预检**
//   逐输出渲染目标路径（渲染口径与 pipeline::plan_targets 同源：relative_dir +
//   render_output_path），全部「已存在且非空」→ 该文件视为已完成、不再提交。
//   0 字节产物按「缺件」处理（会提交；写侧冲突策略见 on_start 的补齐分支）。
//   预检只读磁盘，不写不删（源文件永远只读、输出不得覆盖源文件的红线不受影响）。
// ---------------------------------------------------------------------------
bool entry_outputs_complete(const pp::FileEntry &entry, const pp::RunConfig &cfg) {
    if (cfg.outputs.empty())
        return false;
    for (const pp::OutputFormatSpec &spec : cfg.outputs) {
        const pp::FormatDef *fmt = pp::find_format(spec.format_id);
        if (!cfg.metadata_only && fmt == nullptr)
            return false;
        // 与引擎同源：metadata-only 的路径按**源格式**渲染（$format/扩展名），不认目标格式。
        const pp::PathCtx ctx = pp::path_ctx_for(entry, cfg, fmt);
        const std::filesystem::path desired =
            pp::render_output_path(cfg.output_template, ctx, cfg.out_root);
        if (desired.empty())
            return false;
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(desired, ec);
        if (ec || size == 0)
            return false; // 缺件：不存在，或 0 字节（不算成品）
    }
    return true;
}

// ---------------------------------------------------------------------------
// M7-R2D1 / 0 字节补齐语义（deep-review R-006 余量；WP6 handoff 残余 #6）：
//   预检把「已存在但 0 字节」算缺件（会提交），但 conflict=Skip 下引擎对"已存在"一律跳过 ⇒
//   自家残次品永远不会被重编码。补齐语义 = **跑前移除 0 字节产物**（不是成品，删除即缺件归位），
//   随后引擎正常写入。普通运行的 Skip 语义**不动**（只在「再次运行」分支调用）。
//   红线：源文件只读、输出不得覆盖源文件 —— 与源同一实体的路径绝不触碰（equivalent 识破
//   符号链接/大小写/8.3 短名）；只删 size==0 的产物，非空成品原样跳过。
// ---------------------------------------------------------------------------
std::size_t remove_zero_byte_outputs(const pp::FileEntry &entry, const pp::RunConfig &cfg) {
    std::size_t removed = 0;
    for (const pp::OutputFormatSpec &spec : cfg.outputs) {
        const pp::FormatDef *fmt = pp::find_format(spec.format_id);
        if (!cfg.metadata_only && fmt == nullptr)
            continue;
        const pp::PathCtx ctx = pp::path_ctx_for(entry, cfg, fmt);
        const std::filesystem::path desired =
            pp::render_output_path(cfg.output_template, ctx, cfg.out_root);
        if (desired.empty())
            continue;
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(desired, ec);
        if (ec || size != 0)
            continue; // 不存在 = 正常缺件；非空 = 成品（Skip 语义不动）
        std::error_code eq_ec;
        if (std::filesystem::equivalent(desired, entry.src, eq_ec) && !eq_ec)
            continue; // 红线：目标即源文件 —— 绝不删
        std::error_code rm_ec;
        if (std::filesystem::remove(desired, rm_ec) && !rm_ec) {
            ++removed;
            pp::log_info("ui", entry.src.string(),
                         "rerun: removed 0-byte output " + desired.string());
        } else {
            pp::log_warn("ui", entry.src.string(),
                         "rerun: cannot remove 0-byte output " + desired.string());
        }
    }
    return removed;
}

QString path_text(const std::filesystem::path &p) {
    return QFileInfo(QString::fromStdString(p.string())).fileName();
}

bool is_metadata_only_format(const QString &suffix) {
    // 与引擎同源：扩展名 → 源格式 → 仅元数据能力（不再维护 UI 侧第二份白名单）。
    const std::filesystem::path probe = std::filesystem::path("probe." + suffix.toStdString());
    const pp::FormatDef *fmt = pp::format_for_source_path(probe);
    return fmt != nullptr && pp::format_supports_metadata_only(fmt->id);
}

namespace {

// §M2-T16b(c')：logs_dir() 来自平台字节层，可能含**非 UTF-8 字节**；Qt6 的 QString 恒为
// UTF-8 语义，QString::fromStdString(dir.string()) 会把非法序列替换成 U+FFFD →
// QDir::entryInfoList 直接丢掉全部条目，QUrl 也会指向被改写后的假路径（T14 报告实测）。故：
//   * 枚举一律走 std::filesystem::directory_iterator（目录项名保留原始字节）；
//   * 只有交给 QDesktopServices 时才需要 QString：先做 UTF-8 往返校验，合法 → fromLocalFile；
//     非法 → stderr 明确提示（不静默失败）。
bool bytes_are_valid_utf8(const std::string &bytes) {
    const QByteArray raw(bytes.data(), static_cast<int>(bytes.size()));
    return QString::fromUtf8(raw).toUtf8() == raw;
}

void open_local_path_bytes(const std::filesystem::path &p, const char *what) {
    const std::string bytes = p.string();
    if (!bytes_are_valid_utf8(bytes)) {
        // R-278：诊断输出收 logger（原 std::fprintf 绕过日志面）
        pp::log_warn("ui", bytes,
                     std::string("cannot open ") + what +
                         ": path contains non-UTF-8 bytes; open it from a shell instead");
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromUtf8(bytes.data(), static_cast<int>(bytes.size()))));
}

// 最新 run-*.log：mtime 新者优先；同 mtime 取名字字典序小者（与 QDir::Time 同口径）。
// 无匹配 → 空 path（调用方退回目录本身）。字节级匹配：非 UTF-8 文件名同样命中。
std::filesystem::path newest_run_log(const std::filesystem::path &dir) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec)
            continue;
        const std::string name = it->path().filename().string();
        if (name.size() < 8 || name.compare(0, 4, "run-") != 0 ||
            name.compare(name.size() - 4, 4, ".log") != 0) {
            continue;
        }
        const std::filesystem::file_time_type t = it->last_write_time(fec);
        if (fec)
            continue;
        if (newest.empty() || t > newest_time ||
            (t == newest_time && name < newest.filename().string())) {
            newest = it->path();
            newest_time = t;
        }
    }
    return newest;
}

} // namespace

} // namespace

// ---------------------------------------------------------------------------
// 运行：开始 / 取消 / 结束 / G5 锁定
// ---------------------------------------------------------------------------

void MainWindow::on_start() {
    Impl &d = *impl_;
    const bool run_again = d.run_again_pending; // R-006/R-146：「再次运行」的补齐语义分支
    d.run_again_pending = false;
    if (d.running) {
        warn_cannot_start(this, T("正在运行"));
        return;
    }
    if (d.model->empty()) {
        warn_cannot_start(this, T("没有文件"));
        return;
    }
    // M4-T11（§6.2/D2）：勾选 = 参与运行集合 —— 未勾选任何文件 = 无参与文件
    if (d.model->checked_count() == 0) {
        warn_cannot_start(this, T("未勾选任何文件"));
        return;
    }
    const QString reason = d.page_output->ready_to_start();
    if (!reason.isEmpty()) {
        warn_cannot_start(this, reason);
        // M8-B2（A20 授权）：预检拒绝后定位第一错行（输出页 → 首个有错格式页签 → 首个错误行）。
        d.page_output->focus_first_rule_error();
        return;
    }

    pp::RunConfig cfg = d.page_output->config_base();
    cfg.rules = d.page_meta->rules();
    cfg.workers = d.settings.workers;
    cfg.budget_bytes = static_cast<uint64_t>(d.settings.budget_gb) << 30; // 0 = 自动
    cfg.flatten_gray = d.settings.flatten_gray;
    cfg.rotate_orientation = d.settings.rotate_orientation;
    // M4-T14（机械性接线）：§8.1 交错步距 / §8.2 线程预算 T 的实际生效值来自设置（W1-T7 已持久化；
    // 设置页控件归 T15）。运行总览卡的「交错 / 线程预算 / 当前分配」按这里下发的同一份值显示 ——
    // 读数与引擎同源，不是 UI 自算的另一套数。
    cfg.stagger_ms = d.settings.stagger_ms;
    cfg.thread_budget = d.settings.thread_budget;
    // M8-C2（C11/C12）：GUI 默认开（AppSettings.quality_score，设置页可关）；--dev 默认关。
    cfg.quality_score = d.settings.quality_score;

    // X-METADATA-ONLY-02：仅元数据模式不再静默把冲突策略改成 Skip；下拉显示什么就执行什么。
    // （旧行为会造成“界面选覆盖/改名，实际全部跳过”的两张皮。）

    // R-073：输出根预检（试建 + 试写探针，fsops::probe_output_root）—— 不可建/不可写时
    // 在**开跑之前**拦下，不再"逐文件失败跑到底、结束语还说可打开输出目录"。
    {
        std::string root_err;
        const std::filesystem::path out_root(cfg.out_root);
        bool root_ok = !out_root.empty() && pp::probe_output_root(out_root, &root_err);
        if (!root_ok) {
            // 先申请权限（仅 Windows；用户拒绝/授权失败才报错）。
#ifdef _WIN32
            if (!root_err.empty() && error_looks_like_permission(root_err)) {
                if (grant_output_dir_permission(this, out_root)) {
                    root_err.clear();
                    root_ok = pp::probe_output_root(out_root, &root_err);
                }
            }
#endif
        }
        if (!root_ok) {
            // 0.3.6-alpha1：预检失败原先**只在弹窗里出现、日志零记录**（事后无法定位用户
            // 选了哪个目录、OS 报什么错）。这里先落一条 warn，再给可操作的提示。
            const std::string detail = root_err.empty() ? "output root is empty" : root_err;
            pp::log_warn("run", "mainwindow_run.cpp", "output root unavailable",
                         {{"out_root", out_root.string()}, {"error", detail}});
            warn_cannot_start(this, permission_error_text(detail));
            return;
        }
    }

    // M2-T5 §2.7：交叉参数约束的最后闸门。ParamForm 已实时红字提示，这里对真正要下发的
    // 配置再校验一次；非空 → 列出全部消息并阻止开始（不进入 lock_for_run/Scheduler）。
    // 0.3.0 多输出：逐输出求值（T13 起 outputs 可能 >1，UI 现在恒 1 项）。
    {
        std::vector<std::string> cross;
        if (!cfg.metadata_only) { // 仅元数据：无编码参数，不校验目标格式的参数集
            for (const pp::OutputFormatSpec &spec : cfg.outputs) {
                const std::vector<std::string> msgs =
                    pp::cross_validate(spec.params, spec.format_id, spec.tech_id);
                cross.insert(cross.end(), msgs.begin(), msgs.end());
            }
        }
        if (!cross.empty()) {
            QStringList lines;
            lines.reserve(static_cast<int>(cross.size()));
            for (const std::string &msg : cross)
                lines << QString::fromStdString(msg);
            warn_cannot_start(this, lines.join(QLatin1Char('\n')));
            return;
        }
    }

    // R-075：GPS 批量规则 on_start 预检（形态同"仅元数据预检"）—— 规则坐标非法/时间戳格式
    // 不符时，运行期不会再有任何提示（只在元数据页黄字），用户会在"输出无 GPS"后才发现。
    // 这里在开跑前确认一次；ts 格式校验复用 WP1 的纯函数 pp::is_valid_gps_timestamp。
    if (const pp::BatchRules rules = cfg.rules; rules.gps.has_value()) {
        const pp::GpsData &gps = *rules.gps;
        const bool lat_ok = gps.lat >= -90.0 && gps.lat <= 90.0;
        const bool lon_ok = gps.lon >= -180.0 && gps.lon <= 180.0;
        const bool ts_ok = !gps.timestamp.has_value() || pp::is_valid_gps_timestamp(*gps.timestamp);
        if (!lat_ok || !lon_ok || !ts_ok) {
            QStringList bad;
            if (!lat_ok)
                bad << T("纬度 %1 超出 [-90, 90]").arg(gps.lat);
            if (!lon_ok)
                bad << T("经度 %1 超出 [-180, 180]").arg(gps.lon);
            if (!ts_ok)
                bad << T("时间戳「%1」不是 YYYY:MM:DD HH:MM:SS 形态")
                           .arg(QString::fromStdString(*gps.timestamp));
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, T("GPS 规则预检"),
                T("批量 GPS 规则存在无效字段：\n%1\n\n继续运行将不写入 GPS。仍要继续吗？")
                    .arg(bad.join(QLatin1Char('\n'))),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
    }

    // 参与运行集合（M4-T11：勾选口径；顺序 = 列表行序）
    std::vector<std::size_t> run_rows = d.model->checked_rows();
    std::vector<pp::FileEntry> entries = d.model->checked_entries(); // 含单文件例外（引擎侧生效）

    // X-METADATA-ONLY-01（0.3.6-alpha1 修订）：输出根子树里的输入**不再静默丢弃**。
    // 旧实现把“位于 out_root 下”一律过滤 —— out_root = 输入目录（或它的父目录）时整批
    // files=0（实测 run-20261025-231109：out_root=Downloads，唯一输入被判“在输出根下”），
    // 且界面没有任何提示（只有一行日志）。新行为：先数出可疑项（位于 out_root 下，且扩展名
    // 与本轮输出格式相同 —— 只有这类文件才可能是上一轮产物），一次性问用户：
    //   Yes = 照常处理（同名输出由引擎强制改名「名称 (1).扩展名」，源文件永不被覆盖）
    //   No  = 跳过这些输入（保持上一轮产物不被二次施加规则）
    // 普通源（例如输出 HEIF 时的 .tif）扩展名不同，不再被误判、也不再询问。
    bool skipped_under_out_root = false;
    {
        const std::filesystem::path out_root(cfg.out_root);
        if (!out_root.empty() && !entries.empty()) {
            const auto lower_ascii = [](std::string s) {
                for (char &c : s) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return s;
            };
            std::vector<std::string> out_exts;
            if (!cfg.metadata_only) {
                out_exts.reserve(cfg.outputs.size());
                for (const pp::OutputFormatSpec &spec : cfg.outputs) {
                    if (const pp::FormatDef *fmt = pp::find_format(spec.format_id)) {
                        out_exts.push_back(lower_ascii(fmt->ext));
                    }
                }
            }
            std::vector<std::size_t> suspects;
            for (std::size_t i = 0; i < entries.size() && i < run_rows.size(); ++i) {
                if (!pp::is_inside(entries[i].src, out_root)) {
                    continue;
                }
                // 仅元数据模式的产物保持源格式：任何位于输出根下的输入都可能是上一轮产物。
                bool suspect = cfg.metadata_only;
                if (!suspect) {
                    std::string ext = lower_ascii(entries[i].src.extension().string());
                    if (!ext.empty() && ext.front() == '.') {
                        ext.erase(ext.begin());
                    }
                    suspect = std::find(out_exts.begin(), out_exts.end(), ext) != out_exts.end();
                }
                if (suspect) {
                    suspects.push_back(i);
                }
            }
            if (!suspects.empty()) {
                constexpr std::size_t kShow = 5;
                QStringList shown;
                for (std::size_t k = 0; k < suspects.size() && k < kShow; ++k) {
                    shown << path_text(entries[suspects[k]].src);
                }
                const QString more = suspects.size() > kShow
                                         ? T("\n…（其余 %1 个省略）").arg(suspects.size() - kShow)
                                         : QString();
                const QString body =
                    cfg.metadata_only
                        ? T("有 %1 个输入位于输出根目录内，可能是上一轮产物（仅元数据模式的产物"
                            "保持源格式，无法按扩展名区分）：\n%2%3\n\n"
                            "是：照常处理它们（同名输出会自动改名为「名称 (1).扩展名」，源文件"
                            "不会被覆盖）\n"
                            "否：跳过这些输入（不把上一轮产物当成本轮输入）")
                              .arg(suspects.size())
                              .arg(shown.join(QLatin1Char('\n')), more)
                        : T("有 %1 个输入位于输出根目录内，且扩展名与本轮输出相同，可能是上一轮"
                            "产物：\n%2%3\n\n"
                            "是：照常处理它们（同名输出会自动改名为「名称 (1).扩展名」，源文件"
                            "不会被覆盖）\n"
                            "否：跳过这些输入（不把上一轮产物当成本轮输入）")
                              .arg(suspects.size())
                              .arg(shown.join(QLatin1Char('\n')), more);
                const QMessageBox::StandardButton answer =
                    QMessageBox::question(this, T("输入位于输出目录内"), body,
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (answer != QMessageBox::Yes) {
                    skipped_under_out_root = true;
                    std::vector<bool> drop(entries.size(), false);
                    for (const std::size_t idx : suspects) {
                        drop[idx] = true;
                    }
                    std::vector<std::size_t> kept_rows;
                    std::vector<pp::FileEntry> kept;
                    kept_rows.reserve(run_rows.size());
                    kept.reserve(entries.size());
                    for (std::size_t i = 0; i < entries.size(); ++i) {
                        if (drop[i]) {
                            continue;
                        }
                        kept_rows.push_back(run_rows[i]);
                        kept.push_back(std::move(entries[i]));
                    }
                    run_rows.swap(kept_rows);
                    entries.swap(kept);
                }
                pp::log_info("run", "mainwindow_run.cpp", "inputs under output root",
                             {{"out_root", out_root.string()},
                              {"count", std::to_string(suspects.size())},
                              {"action", answer == QMessageBox::Yes ? "process" : "skip"}});
            }
        }
    }
    if (entries.empty()) {
        QMessageBox::information(
            this, T("没有可运行的文件"),
            skipped_under_out_root
                ? T("所选输入全部位于输出根目录内，且被跳过。请更换输出根目录，或重新选择输入。")
                : T("当前没有参与运行的输入文件（未勾选或全部被过滤）。"));
        return;
    }

    // R-067：过滤视图与运行集语义冲突 —— 过滤只改可见行，勾选/运行集是全局的。
    // 运行前置提示"参与集含 N 个被过滤隐藏的文件"（唯一线索不再是底栏数字）。
    if (d.proxy->filtering() || !d.search->text().isEmpty()) {
        std::size_t hidden = 0;
        for (const std::size_t row : run_rows) {
            const QModelIndex filtered = d.proxy->mapFromSource(d.model->index(int(row), 0));
            if (!filtered.isValid())
                ++hidden;
        }
        if (hidden > 0) {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this, T("参与集含隐藏文件"),
                T("当前筛选条件下参与集含 %1 个被过滤隐藏的文件，它们仍会参与本次运行。\n\n"
                  "仍要继续吗？")
                    .arg(hidden),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer != QMessageBox::Yes)
                return;
        }
    }

    // R-006（补齐语义）：「再次运行」= 只补缺件 —— 逐文件预检目标输出，全部「已存在且非空」
    // 的文件不再提交；本轮冲突策略改 Skip，已存在的输出不再被 Rename 成 "photo (1).jpg" 副本。
    // （预检只读磁盘；0 字节产物按缺件处理，见 entry_outputs_complete。）
    std::size_t complete_dropped = 0;
    if (run_again) {
        std::vector<std::size_t> keep_rows;
        std::vector<pp::FileEntry> keep_entries;
        keep_rows.reserve(run_rows.size());
        keep_entries.reserve(entries.size());
        for (std::size_t k = 0; k < entries.size(); ++k) {
            if (entry_outputs_complete(entries[k], cfg)) {
                ++complete_dropped;
                continue;
            }
            // 0 字节补齐（M7-R2D1）：自家残次品先移除（缺件归位），引擎随后正常重做；
            // 非空成品与源文件绝不触碰（remove_zero_byte_outputs 的守卫）
            remove_zero_byte_outputs(entries[k], cfg);
            keep_rows.push_back(run_rows[k]);
            keep_entries.push_back(std::move(entries[k]));
        }
        run_rows.swap(keep_rows);
        entries.swap(keep_entries);
        cfg.conflict = pp::ConflictPolicy::Skip;
        if (entries.empty()) {
            QMessageBox::information(
                this, T("再次运行"),
                T("没有缺件：全部 %1 个文件的输出已存在且非空，无需重跑。").arg(complete_dropped));
            return;
        }
    }
    const std::size_t total = entries.size();

    if (cfg.metadata_only) {
        QStringList bad;
        for (std::size_t k = 0; k < total; ++k) {
            const QString name = path_text(entries[k].src);
            const QString suffix = QFileInfo(name).suffix().toLower();
            if (!is_metadata_only_format(suffix))
                bad << name;
        }
        if (!bad.isEmpty()) {
            const QString head = bad.mid(0, 20).join(QLatin1Char('\n'));
            const QString more =
                bad.size() > 20 ? T("\n…（其余 %1 个省略）").arg(bad.size() - 20) : QString();
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, T("仅元数据预检"),
                T("以下 %1 个文件的格式不支持仅元数据（输出将保持源格式：JPEG / PNG / TIFF / "
                  "WebP）：\n%2%3\n\n仍要继续吗？")
                    .arg(bad.size())
                    .arg(head, more),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
    }

    QStringList names;
    names.reserve(static_cast<int>(total));
    for (const pp::FileEntry &entry : entries)
        names << path_text(entry.src);

    // M4-T14：运行页运行计划（§3.4/§7.2/§8.1/§8.2）——逐源文件的尺寸/字节（行数文案与压缩比的
    // 输入；取自列表已 probe 的摘要，零额外 IO，未 probe 则 0 = 未知，页面自动退化文案）+
    // 输出格式清单（= 配置顺序，逐输出子行的格式 chip 与行数）+ 调度读数。
    {
        PageRun::RunPlan plan;
        if (cfg.metadata_only) {
            // 仅元数据：产物格式跟随每个源文件（可能是混合批次）——运行页的格式 chip 必须
            // 报真实源格式，不能报 UI 里那个被忽略的目标格式（旧口径会显示 jxl 而产物是 jpg）。
            for (const pp::FileEntry &entry : entries) {
                const pp::FormatDef *src_fmt = pp::format_for_source_path(entry.src);
                const QString id = QString::fromStdString(src_fmt ? src_fmt->id : std::string("?"));
                if (!plan.format_ids.contains(id))
                    plan.format_ids << id;
            }
        } else {
            for (const pp::OutputFormatSpec &spec : cfg.outputs)
                plan.format_ids << QString::fromStdString(spec.format_id);
        }
        plan.widths.reserve(static_cast<int>(total));
        plan.heights.reserve(static_cast<int>(total));
        plan.bytes.reserve(static_cast<int>(total));
        for (std::size_t k = 0; k < total; ++k) {
            const std::size_t row = k < run_rows.size() ? run_rows[k] : k;
            if (row < d.model->size()) {
                const FileRow &fr = d.model->row(row);
                plan.widths << (fr.probe_ok ? fr.info.width : 0);
                plan.heights << (fr.probe_ok ? fr.info.height : 0);
            } else {
                plan.widths << 0;
                plan.heights << 0;
            }
            // 源文件字节：字节层取（std::filesystem），避免非 UTF-8 路径经 QString 往返失真
            std::error_code ec;
            const std::uintmax_t size = std::filesystem::file_size(entries[k].src, ec);
            plan.bytes << (ec ? 0 : static_cast<qint64>(size));
        }
        plan.stagger_ms = cfg.stagger_ms;
        plan.workers = cfg.workers;
        plan.thread_budget = cfg.thread_budget;
        plan.metadata_only = cfg.metadata_only;
        d.page_run->set_plan(plan);
    }

    // 2026-10 修复：只复位本批会让**未勾选**行保留上一轮的「已完成/编码」胶囊，而底栏只统计
    // 本批 → 同一屏两套状态（bug1：文件列表 9 行中仅 .heic 勾选，其余仍显示上一轮状态）。
    // 新批次开始即统一清空所有行的旧状态；未勾选行回到排队，不再冒充本轮结果。
    for (std::size_t i = 0; i < d.model->size(); ++i)
        d.model->set_state(i, pp::FileState::Queued);
    d.run_rows = run_rows; // ev.index（提交序号）→ 列表行号的映射
    // R-256 后半（M7R22G2）：提交时按序号快照源路径（稳定键；与 entries 同序），供
    // handle_file_event 逐事件核对「行序不变」契约 + 漂移时按路径重定位。
    d.run_row_paths.clear();
    d.run_row_paths.reserve(entries.size());
    for (const pp::FileEntry &e : entries)
        d.run_row_paths.push_back(e.src);
    d.run_started = 0; // R-250：单账本
    d.run_seen.assign(run_rows.size(), false);
    d.run_total = total;
    d.run_terminal = 0;
    d.run_done = 0;
    d.run_failed = 0;
    d.run_skipped = 0;
    d.run_partial = 0;           // R-076
    d.run_cancelled = 0;         // R-140
    d.run_failure_lines.clear(); // R-144：本批失败清单重新收集
    d.failure_list_path.clear();

    d.sched = std::make_unique<pp::Scheduler>(cfg, std::move(entries));
    d.sched->set_event_callback([this](const pp::FileEvent &ev) {
        // 回调可能来自任意 worker 线程：立即按值拷贝 FileResult（指针仅发射瞬间有效）
        const bool has_result = ev.result != nullptr;
        const pp::FileResult copy = has_result ? *ev.result : pp::FileResult{};
        QMetaObject::invokeMethod(
            this,
            [this, ev, has_result, copy]() {
                pp::FileEvent forwarded = ev;
                forwarded.result = has_result ? &copy : nullptr;
                impl_->handle_file_event(forwarded);
            },
            Qt::QueuedConnection);
    });
    d.sched->start();
    lock_for_run(true); // G5
    d.page_run->begin_run(total, names);
    d.poll->start();
    d.log_tail->start();
    impl_->push_log_tail(); // 立刻给一次（本次 run 日志刚开写）
    refresh_status();
}

// R-006/R-146：「再次运行」入口。补齐语义 = 只提交缺件（已存在且非空的输出跳过）；
// 上一批含失败/部分成功时先确认（不然一点即失现场，用户想核对失败原因就没了）。
void MainWindow::on_run_again() {
    Impl &d = *impl_;
    if (d.running)
        return;
    if (d.run_failed > 0 || d.run_partial > 0) {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(this, T("再次运行"),
                                  T("上一批有 %1 个失败、%2 个部分成功。再次运行只补跑缺失的输出"
                                    "（已存在且非空的输出跳过，不会重新编码）。\n\n仍要继续吗？")
                                      .arg(d.run_failed)
                                      .arg(d.run_partial),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    d.run_again_pending = true;
    on_start();
}

void MainWindow::on_cancel() {
    if (impl_->sched)
        impl_->sched->cancel(); // 幂等；进行中的文件跑完
}

void MainWindow::on_scheduler_done() {
    Impl &d = *impl_;
    if (!d.sched)
        return;
    d.poll->stop();
    d.log_tail->stop();
    // R-252 时序锚点⑤「sendPostedEvents 后定稿」：停表 → wait → 排空 queued 事件（下一行）
    // → end_run 定稿；颠倒会让迟到事件改写已定稿计数。
    Q_ASSERT(d.poll != nullptr && !d.poll->isActive());
    d.sched->wait();
    // 排空在途 queued 事件后再定稿，避免 end_run 之后又被迟到事件改写计数
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    const pp::RunSummary sum = d.sched->summary();
    d.page_run->end_run(sum, d.page_output->out_root());
    d.sched.reset();
    lock_for_run(false);
    impl_->on_content_changed();
    impl_->write_failure_list(); // R-144：失败清单落盘（状态栏 tooltip 给出处）
    impl_->push_log_tail();      // 结束态再读一次（末几行含 summary/收尾日志）
    refresh_status();
}

// R-144（M7-R2D1）：失败清单落盘 —— logs/failed-<时间戳>.txt，一行一条「路径\t原因」。
// 只写日志目录（源文件只读红线不受影响）；logs 不可用 = 静默放弃（状态栏 tooltip 显示「未落盘」）。
void MainWindow::Impl::write_failure_list() {
    if (run_failure_lines.isEmpty())
        return;
    const std::filesystem::path dir = pp::platform::logs_dir();
    if (dir.empty()) {
        pp::log_warn("ui", "failure-list", "logs dir unavailable; failure list not written");
        return;
    }
    const std::string stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString();
    const std::filesystem::path file = dir / ("failed-" + stamp + ".txt");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        pp::log_warn("ui", file.string(), "cannot write failure list");
        return;
    }
    for (const QString &line : run_failure_lines)
        out << line.toStdString() << '\n';
    out.close();
    failure_list_path = QString::fromStdString(file.string());
    pp::log_info("ui", file.string(),
                 "failure list written: " + std::to_string(run_failure_lines.size()) + " item(s)");
}

void MainWindow::lock_for_run(bool lock) {
    Impl &d = *impl_;
    d.running = lock;
    // GUI-R2 修 4（防御）：「开始运行」在运行期必须**立即**禁用 —— 原先只靠末尾 refresh_status()
    // 的 setEnabled(can_start) 落地，中间存在可点窗口期；重入会弹「正在运行」模态（用户看到的
    // "概率性错误"）。这里先掐掉，末尾的 refresh_status 再按完整闸门校准。
    if (d.start != nullptr)
        d.start->setEnabled(!lock);
    // R-071/R-154：运行期关掉窗口级拖放入口（handle_event 里另有拒收兜底）
    setAcceptDrops(!lock);
    d.nav_meta->setEnabled(!lock);
    d.nav_output->setEnabled(!lock);
    d.settings_action->setEnabled(!lock);
    d.presets_action->setEnabled(!lock);
    // QToolButton 与 defaultAction 的图标同步是**单向覆盖**：action 的 icon 为空时，使能态一变化
    // 就会用空图标覆盖按钮自己的矢量图标（实测：置灰瞬间 icon().isNull() 0→1），随后 QCommonStyle
    // 因"图标空 + 文本非空"回落成**文本**渲染（§3.3 图标不得退化为字形/文本）。⇒ 使能态切换后
    // 立即重放一次骨架图标（单源 = apply_chrome_icons）。
    d.apply_chrome_icons();
    d.panel->setEnabled(!lock);
    d.page_meta->setEnabled(!lock);
    d.page_output->setEnabled(!lock);
    // G5「预览只读、分类只读」：预览面板运行时锁**打标热键**（分类只读）；翻图/缩放仍可用
    // （预览本身无修改语义，读图不受运行影响）。
    if (d.preview_panel != nullptr)
        d.preview_panel->set_locked(lock);
    // M4-T11：左栏分类面板同样运行期只读；卡头 hint 按 mockup 切换（"运行中只读"；非运行态为空
    // —— M5-T5/1-14：快捷键提示「1–9 键打标」已移入 tooltip）
    if (d.classify_panel != nullptr)
        d.classify_panel->set_locked(lock);
    if (d.classify_hint != nullptr)
        d.classify_hint->set_full_text(lock ? T("运行中只读") : QString());
    // M5-T5（1-12）：教学文案已入空态；卡头 hint 只保留运行期锁定反馈
    if (d.file_hint != nullptr)
        d.file_hint->set_full_text(lock ? T("运行中锁定") : QString());
    if (d.group_mode != nullptr)
        d.group_mode->setEnabled(!lock); // mockup run-dark：分组下拉置灰
    if (lock)
        set_current_page(3); // stack 锁到运行页
    refresh_status();
}

void MainWindow::Impl::open_logs() {
    const std::filesystem::path dir = pp::platform::logs_dir();
    if (dir.empty()) {
        // R-278：诊断输出收 logger（原 std::fprintf 绕过日志面）
        pp::log_warn("ui", "logs", "cannot open logs: data_dir()/logs is unavailable (empty path)");
        return;
    }
    const std::filesystem::path newest = newest_run_log(dir);
    open_local_path_bytes(newest.empty() ? dir : newest, "logs");
}

namespace {

// R-081：日志尾读取（目录枚举 + 8KB 尾窗）—— 纯 IO 载荷，可在 worker 线程执行
struct LogTailPayload {
    QStringList lines;
    QString name;
};

LogTailPayload read_log_tail(const std::filesystem::path &dir) {
    // §7.3「日志尾卡（等宽 4 行真实尾部 + 查看完整日志…）」读法 = 尾部 8KB 窗口（避免整读大
    // 日志）→ 按行切 → **丢掉窗口首行的半截行** → 取末 4 行。日志格式 = core/logger.cpp 的
    // `%H:%M:%S.%e [%l] [%t] %v`（与 mockup .loglines 同形）。
    LogTailPayload payload;
    if (dir.empty())
        return payload;
    const std::filesystem::path file = newest_run_log(dir);
    if (file.empty())
        return payload;
    QStringList lines;
    std::ifstream in(file, std::ios::binary);
    if (in) {
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size > 0) {
            constexpr std::streamoff kWindow = 8192;
            const std::streamoff start = size > kWindow ? size - kWindow : 0;
            in.seekg(start, std::ios::beg);
            std::string buf(static_cast<std::size_t>(size - start), '\0');
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            buf.resize(static_cast<std::size_t>(in.gcount()));
            QString text = QString::fromUtf8(buf.data(), static_cast<int>(buf.size()));
            if (start > 0) {
                const int first_break = static_cast<int>(text.indexOf(QLatin1Char('\n')));
                text = first_break >= 0 ? text.mid(first_break + 1) : QString();
            }
            const QStringList all = text.split(QLatin1Char('\n'));
            for (const QString &raw : all) {
                const QString line = raw.trimmed();
                if (!line.isEmpty())
                    lines << line;
            }
            while (lines.size() > 4)
                lines.removeFirst();
        }
    }
    payload.lines = lines;
    payload.name = QString::fromStdString(file.filename().string());
    return payload;
}

} // namespace

void MainWindow::Impl::push_log_tail() {
    // R-081（M7-R2D1）：500ms 节奏的目录枚举 + 8KB 读搬出 GUI 线程（网络盘可卡 poll/取消）。
    // 结果经 QFutureWatcher 回 GUI 线程喂 page_run（页只渲染，不做文件 IO 的口径不变）。
    // 单在途 + 补读：读在途时到来的请求只记一笔，归并后补一轮（收尾兜底读不会被吃掉）。
    if (log_tail_inflight) {
        log_tail_rerun = true;
        return;
    }
    log_tail_inflight = true;
    const std::filesystem::path dir = pp::platform::logs_dir();
    auto *watcher = new QFutureWatcher<LogTailPayload>(w);
    QObject::connect(watcher, &QFutureWatcherBase::finished, w, [this, watcher] {
        watcher->deleteLater();
        log_tail_inflight = false;
        const LogTailPayload payload = watcher->result();
        page_run->set_log_tail(payload.lines, payload.name);
        if (log_tail_rerun) {
            log_tail_rerun = false;
            push_log_tail();
        }
    });
    // M7R21F4-0（NOLINT 处置翻案）：原 R2G 对本 lambda bugprone-exception-escape 的行级
    // 消音已撤销（M7-ADJ-tidy-reseed 要求零抑制）。QtConcurrent 任务体的异常由 Qt 捕获进
    // QFuture（result() 处再抛）；修法（lambda 内 try/catch 吞异常）= 行为变化，非零行为
    // 修复不可采 —— 按 M7-ADJ-tidy-reseed 口径入 tidy 基线重播种（本文件 1→2，与同文件
    // :531 既有同形任务同册归因），不再在代码里消音。
    watcher->setFuture(QtConcurrent::run([dir] { return read_log_tail(dir); }));
}

void MainWindow::Impl::handle_file_event(const pp::FileEvent &ev) {
    // M4-T11：ev.index = 提交序号（仅勾选文件）→ 映射回列表行号再落状态
    if (ev.index < 0 || std::size_t(ev.index) >= run_rows.size() ||
        std::size_t(ev.index) >= run_row_paths.size())
        return;
    // R-256 后半（M7R22G2）：「行序不变」隐式契约 → **显式校验 + 稳定键兜底** —— 核对映射
    // 行仍是提交时的同一源文件（run_row_paths 快照）；行集漂移（增删/重排）时按路径重定位，
    // 找不到即丢弃本事件的落行（计数账本仍按 ev.index 走，不受影响）。
    std::size_t row = run_rows[std::size_t(ev.index)];
    const std::filesystem::path &want = run_row_paths[std::size_t(ev.index)];
    if (row >= model->size() || model->row(row).entry.src != want) {
        constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
        row = kNotFound;
        for (std::size_t i = 0; i < model->size(); ++i) {
            if (model->row(i).entry.src == want) {
                row = i;
                break;
            }
        }
        if (row == kNotFound)
            return;
        run_rows[std::size_t(ev.index)] = row; // 重定位结果回写映射（后续事件直用）
    }
    // R-250：单账本 —— "已取件"在此记账（离开 Queued 的第一个事件），inflight/queued 自算。
    if (ev.state != pp::FileState::Queued && std::size_t(ev.index) < run_seen.size() &&
        !run_seen[std::size_t(ev.index)]) {
        run_seen[std::size_t(ev.index)] = true;
        ++run_started;
    }
    // R-076：多输出部分成功不聚合成 Done —— 调度器仍把 Verdict::DoneWithErrors 映射成
    // FileState::Done（scheduler.cpp 非本包文件面，见 handoff「跨界需求」），故这里按
    // FileResult.outputs 的逐输出事实**补判**：有成有败 = 部分成功。
    pp::FileState state = ev.state;
    bool zero_bytes = false;
    if (ev.result != nullptr) {
        for (const pp::OutputResult &out : ev.result->outputs) {
            if (out.ok && out.out_bytes == 0)
                zero_bytes = true; // R-069：0 字节产物事实
        }
        if (state == pp::FileState::Done && ev.result->outputs.size() > 1) {
            std::size_t ok = 0, bad = 0;
            for (const pp::OutputResult &out : ev.result->outputs)
                (out.ok ? ok : bad)++;
            if (ok > 0 && bad > 0)
                state = pp::FileState::DoneWithErrors; // R-076
        }
    }
    switch (state) { // 计数先于 set_state：set_state 同步触发 content_changed → refresh_status
    case pp::FileState::Done:
        ++run_terminal;
        ++run_done;
        break;
    case pp::FileState::DoneWithErrors: // R-076：多输出部分成功 = 独立档，不并入 Done
        ++run_terminal;
        ++run_partial;
        break;
    case pp::FileState::Skipped:
        ++run_terminal;
        ++run_skipped;
        break;
    case pp::FileState::Failed:
        ++run_terminal;
        ++run_failed;
        break;
    case pp::FileState::Cancelled:
        ++run_terminal;  // Scheduler::finish() 对每个文件恰好发一次终态事件
        ++run_cancelled; // R-140：取消不隐身（状态栏有独立桶）
        break;
    default:
        break;
    }
    // R-008：Progress 是逐输出的**过程**事件（一次文件会发多条），落模型会把左列表状态胶囊
    // 全程覆写成过程态 —— 过程态只喂 page_run，列表只落阶段/终态。
    if (state != pp::FileState::Progress) {
        model->set_state(row, state);
        if (zero_bytes) // R-069：终态事件带 0 字节事实回传列表
            model->set_zero_byte_output(row, true);
        // R-144（M7-R2D1）：失败/部分成功的逐文件原因 → 失败清单行（收尾落盘；状态栏下钻用）
        if ((state == pp::FileState::Failed || state == pp::FileState::DoneWithErrors) &&
            ev.result != nullptr) {
            QStringList reasons;
            if (!ev.result->error.empty())
                reasons << QString::fromStdString(ev.result->error);
            for (const pp::OutputResult &out : ev.result->outputs) {
                if (!out.ok && !out.error.empty()) {
                    reasons << QStringLiteral("%1: %2").arg(QString::fromStdString(out.format_id),
                                                            QString::fromStdString(out.error));
                }
            }
            run_failure_lines << QStringLiteral("%1\t%2").arg(
                QString::fromStdString(model->row(row).entry.src.string()),
                reasons.join(QStringLiteral(" / ")));
        }
    }
    page_run->on_event(ev);
    // R-024：逐文件事件洪峰合并刷新 —— 运行期不再每条事件都跑一次全量 refresh_status
    // （含两个 O(n) 计数 + 磁盘查询），改为 150ms 定时合并；终态事件立即刷（定稿要看得见）。
    if (state != pp::FileState::Progress && run_terminal >= run_total)
        w->refresh_status();
    else
        schedule_status_refresh();
}

// R-024：合并刷新（singleShot 起搏：只要还有事件就推后，静默 kStatusFlushMs 后落地一次）
void MainWindow::Impl::schedule_status_refresh() {
    if (status_flush == nullptr) {
        w->refresh_status();
        return;
    }
    if (!status_flush->isActive())
        status_flush->start();
}

} // namespace pp::ui
