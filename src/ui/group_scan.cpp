// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 异步 EXIF 分组摘要（group_scan）（R-200：filelistmodel 一 TU 6
// 类按所有权拆分产物；M7-R2.1-F3）
//
// 拆分口径 = **行为零变化**：类/函数体自 filelistmodel.cpp 逐字搬入（共享纯件入
// ui/filelistmodel_util.h 单源）；objectName / 信号 / 公开面逐位不变；
// 冻结头 filelistmodel.h 逐字未动。拆分步序见 build/m7r2-e2/handoff.md ⑥-2。
#include "ui/filelistmodel.h"

#include "ui/filelistmodel_util.h"
#include "ui/icons.h"
#include "ui/thumbnails.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QRunnable>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include "core/fsops.h"
#include "ui/theme.h"

namespace pp::ui {

// ============================================================================================
// M4-T11 · 自动分组（键 = core/classify 的 group_key() 纯函数；EXIF 摘要异步补扫）
// ============================================================================================

namespace {

// 分组摘要补扫（独立单线程池 = 串行，不与缩略图/转码抢核；只读 EXIF，不碰像素）
class GroupScanRunnable : public QRunnable {
public:
    GroupScanRunnable(FileListModel *model, QString path) : model_(model), path_(std::move(path)) {
        setAutoDelete(true);
    }
    void run() override {
        std::string datetime;
        std::string camera;
        try {
            const pp::SourceMeta meta =
                pp::read_metadata(std::filesystem::path(path_.toStdString()));
            datetime = pp::effective_datetime(meta.exif, meta.xmp);
            const auto it = meta.exif.findKey(Exiv2::ExifKey("Exif.Image.Model"));
            if (it != meta.exif.end()) {
                camera = it->toString();
                const std::size_t b = camera.find_first_not_of(" \t\r\n");
                const std::size_t e = camera.find_last_not_of(" \t\r\n");
                camera = (b == std::string::npos) ? std::string() : camera.substr(b, e - b + 1);
            }
        } catch (...) {
            // 摘要失败 = 无字段（归入「无分组」；分组绝不因读失败而抛错/阻断）
            datetime.clear();
            camera.clear();
        }
        FileListModel *const model = model_;
        const QString path = path_;
        // 回填在 GUI 线程（context = 模型自身）执行：Qt 队列投递，模型析构则事件自动作废
        QMetaObject::invokeMethod(
            model,
            [model, path, datetime, camera] {
                model->apply_group_scan(-1, path, datetime, camera);
            },
            Qt::QueuedConnection);
    }

private:
    FileListModel *model_ = nullptr;
    QString path_;
};

} // namespace

// R-200：跨 TU 衔接工厂（util 头声明；实现即本文件 Runnable）
QRunnable *make_group_scan_task(FileListModel *model, const QString &path) {
    return new GroupScanRunnable(model, path);
}
} // namespace pp::ui
