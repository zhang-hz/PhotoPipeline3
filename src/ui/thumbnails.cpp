// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — async probe/thumbnail provider (M1b-U4)
//
// 规格落地（docs/m1b-tasks.md §2.7 实现约束）：
//   - 构造函数里 qRegisterMetaType<pp::ImageInfo>()（队列信号需要）
//   - worker = std::jthread ×2；FIFO 队列 + mutex + condition_variable_any
//   - 结果经 QMetaObject::invokeMethod(this, [...], Qt::QueuedConnection) 回 GUI 线程发 ready
//   - 同一 row 未决时重复 request → 丢弃旧任务（generation 失效，在途任务结果不再投递）
//   - 析构 request_stop + 清队列 → join；在途任务跳过投递后立即退出
//   - queue_empty：pending 归零（至少一次 request 之后）时发；与 ready 同为 GUI 线程投递
#include "ui/thumbnails.h"

#include "core/thumbs.h"   // 冻结头只给接口（不含 make_thumbnail/ThumbOutcome 声明）

#include <QMetaObject>
#include <QMetaType>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace pp::ui {
namespace {

// ThumbImage → QImage：Format_RGBA8888（与 core 的 rgba 字节序一致）；copy() 深拷贝解耦源 vector。
// 尺寸/字节数不符 → null（probe_ok 但缩略图不可用的正常情况）。
QImage image_from_thumb(const pp::ThumbImage& t) {
    if (t.width <= 0 || t.height <= 0) return {};
    const std::size_t need = std::size_t(t.width) * std::size_t(t.height) * 4u;
    if (t.rgba.size() < need) return {};
    const QImage view(t.rgba.data(), t.width, t.height, qsizetype(t.width) * 4,
                      QImage::Format_RGBA8888);
    return view.copy();
}

}  // namespace

struct Thumbnailer::Impl {
    struct Job {
        int row = 0;
        QString path;
        std::uint64_t gen = 0;
    };

    explicit Impl(int target) : target_edge(target) {}

    int target_edge = 96;
    Thumbnailer* owner = nullptr;

    mutable std::mutex mu;
    std::condition_variable_any cv;
    std::deque<Job> queue;                  // FIFO（worker 从队首取）
    std::map<int, std::uint64_t> current;   // row → 最新 generation（缺失/不等 = stale）
    std::uint64_t next_gen = 1;
    int outstanding = 0;                    // 已入队未结束（含在途；含已被替换但仍在跑的任务）
    int in_flight = 0;
    bool any_request = false;               // queue_empty 只在 ≥1 次 request 之后发
    bool stopping = false;
    std::vector<std::jthread> workers;

    bool stale_locked(int row, std::uint64_t gen) const {
        const auto it = current.find(row);
        return it == current.end() || it->second != gen;
    }

    void worker_loop(std::stop_token st);
};

void Thumbnailer::Impl::worker_loop(std::stop_token st) {
    while (!st.stop_requested()) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, st, [this] { return !queue.empty(); });
            if (st.stop_requested()) return;
            job = queue.front();
            queue.pop_front();
            ++in_flight;
        }

        // 已被同 row 的新请求替换 → 不必做昂贵的解码
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lk(mu);
            fresh = !stale_locked(job.row, job.gen);
        }
        pp::ThumbOutcome out;
        if (fresh) {
            out = pp::make_thumbnail(std::filesystem::path(job.path.toStdString()), target_edge);
        }

        // 解码期间可能又被替换/被 clear_pending/正在析构 → 再判一次，丢弃结果
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lk(mu);
            deliver = fresh && !stopping && !stale_locked(job.row, job.gen);
        }
        if (deliver) {
            Thumbnailer* const owner = this->owner;
            const int row = job.row;
            const QString path = job.path;
            const pp::ImageInfo info = out.info;
            const bool probe_ok = out.probe_ok;
            const QString error = QString::fromStdString(out.error);
            const QImage thumb = image_from_thumb(out.thumb);
            QMetaObject::invokeMethod(
                owner,
                [owner, row, path, info, probe_ok, error, thumb] {
                    emit owner->ready(row, path, info, probe_ok, error, thumb);
                },
                Qt::QueuedConnection);
        }

        bool empty_now = false;
        {
            std::lock_guard<std::mutex> lk(mu);
            --in_flight;
            --outstanding;
            empty_now = (outstanding == 0) && any_request && !stopping;
        }
        if (empty_now) {
            Thumbnailer* const owner = this->owner;
            QMetaObject::invokeMethod(
                owner, [owner] { emit owner->queue_empty(); }, Qt::QueuedConnection);
        }
    }
}

Thumbnailer::Thumbnailer(int target_long_edge, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(target_long_edge)) {
    qRegisterMetaType<pp::ImageInfo>();
    impl_->owner = this;
    impl_->workers.reserve(2);
    for (int i = 0; i < 2; ++i) {
        Impl* const im = impl_.get();
        impl_->workers.emplace_back([im](std::stop_token st) { im->worker_loop(st); });
    }
}

Thumbnailer::~Thumbnailer() {
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->stopping = true;
        impl_->queue.clear();     // 排队任务立即丢弃
        impl_->current.clear();   // 在途任务 → stale，结果不再投递
    }
    for (auto& w : impl_->workers) w.request_stop();
    impl_->cv.notify_all();
    impl_->workers.clear();       // join：在途任务跑完当前 make_thumbnail 后立即退出
}

void Thumbnailer::request(int row, const QString& path) {
    Impl* const im = impl_.get();
    {
        std::lock_guard<std::mutex> lk(im->mu);
        if (im->stopping) return;
        const std::uint64_t gen = im->next_gen++;
        im->current[row] = gen;
        // 同一 row 未决的旧请求：丢弃（队列里的删掉；在途的靠 generation 失效）
        for (auto it = im->queue.begin(); it != im->queue.end();) {
            if (it->row == row) {
                it = im->queue.erase(it);
                --im->outstanding;
            } else {
                ++it;
            }
        }
        im->queue.push_back(Impl::Job{row, path, gen});
        ++im->outstanding;
        im->any_request = true;
    }
    im->cv.notify_one();
}

void Thumbnailer::clear_pending() {
    bool empty_now = false;
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->outstanding -= int(impl_->queue.size());
        impl_->queue.clear();
        impl_->current.clear();   // 在途任务 → stale
        empty_now = (impl_->outstanding == 0) && impl_->any_request;
    }
    if (empty_now) emit queue_empty();   // GUI 线程调用
}

int Thumbnailer::pending() const {
    std::lock_guard<std::mutex> lk(impl_->mu);
    return impl_->outstanding;
}

}  // namespace pp::ui
