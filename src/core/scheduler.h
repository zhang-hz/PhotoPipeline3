// PP-FROZEN(file)
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include "core/pipeline.h"

namespace pp {

struct RunSummary {
    std::size_t total = 0, ok = 0, failed = 0, skipped = 0, cancelled = 0;
    uint64_t out_bytes = 0;
    double total_ms = 0, throughput_mb_s = 0, avg_file_ms = 0;
};

class Scheduler {
public:
    using EventCb = std::function<void(const FileEvent&)>;   // 任意线程调用；UI 负责 queued 转发

    Scheduler(RunConfig cfg, std::vector<FileEntry> files);
    ~Scheduler();

    void set_event_callback(EventCb cb);
    void start();          // 非阻塞；创建 worker 池（N=cfg.workers 或物理核数）
    void cancel();         // 置取消标志；已在编码中的文件跑完
    void wait();           // 阻塞至全部结束
    bool running() const;

    const std::vector<FileResult>& results() const;   // wait() 后有效（按输入顺序）
    RunSummary summary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp
