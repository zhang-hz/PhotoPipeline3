// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 输出页「已选格式」唯一真相（deep-review-0.3.2.md R-189 四表示收敛）
//
// 契约（M7-R2.1-F3）：page_output 原以四份并行表示维持「已选格式」—— ① selected 顺序表
// ② 磁贴勾选 ③ 页签序 ④ active_id（+ 表单 objectName 现役判据）—— 靠 4 个手写 sync 配平
// （deep-review R-189/E3-10）。收敛为本类**唯一真相**：变更只走 toggle()/set_active()，
// 四面一致由调用方的单次 reproject（投影）保证；本类不含任何控件（纯语义，可单测）。
//   * 规范顺序 = 磁贴顺序（构造时传入全量 id 序；插入后重排，与原 tile_index 排序逐值相同）；
//   * R-266 语义逐字保留：移除现役项 → 集合非空则现役前移首位；集合为空则 active 保留
//     **被移除的 id** 作「游离槽」（仅元数据模式回退用；显示面按 contains() 收口，不把
//     游离槽当现役页签）—— 重勾选任一项即复位为现役。
// header-only（R-109 路线，同 format_policy.h / byte_text.h）；tests/unit/test_smoke.cpp 钉真值表。
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace pp::ui::fmtsel {

class FormatSelection {
public:
    FormatSelection() = default;
    // order = 全量格式 id 的规范顺序（= 磁贴序）；空序 = 保持插入序（测试用）
    explicit FormatSelection(std::vector<std::string> order) : order_(std::move(order)) {}

    bool contains(const std::string &id) const {
        return std::find(items_.begin(), items_.end(), id) != items_.end();
    }
    const std::vector<std::string> &items() const { return items_; }
    const std::string &active() const { return active_; }
    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }

    // 现役判据（R-266）：游离槽（空集保留的 active）**不是**现役页签。
    bool active_in_set() const { return contains(active_); }

    // 集合变更（toggle_format 的集合侧语义逐字：no-op 早退 / 插入后规范序 / R-266 游离槽）。
    // 返回是否发生变更。id 合法性（find_format）由调用方负责（原实现亦然）。
    bool toggle(const std::string &id, bool on) {
        const bool was = contains(id);
        if (on == was)
            return false;
        if (on) {
            items_.push_back(id);
            std::sort(items_.begin(), items_.end(),
                      [this](const std::string &a, const std::string &b) {
                          return rank(a) < rank(b); // 规范顺序 = 磁贴顺序
                      });
            active_ = id;
        } else {
            items_.erase(std::remove(items_.begin(), items_.end(), id), items_.end());
            if (active_ == id) {
                // R-266：非空 → 现役前移首位；空 → 保留被移除 id 作游离槽（见头注）
                active_ = items_.empty() ? id : items_.front();
            }
        }
        return true;
    }

    // 现役切换（页签 currentChanged 的集合侧语义：越界忽略 / 同值早退）
    bool set_active(const std::string &id) {
        if (!contains(id) || id == active_)
            return false;
        active_ = id;
        return true;
    }

    // 批量重建（apply_preset 路径的集合侧语义）：清空后按规范序重建，**不改现役**
    // （原 selected.clear() + push + sort 逐字等价；现役前移由调用方按 R-133 约定决定）。
    void reset_items(std::vector<std::string> ids) {
        items_ = std::move(ids);
        std::sort(items_.begin(), items_.end(),
                  [this](const std::string &a, const std::string &b) { return rank(a) < rank(b); });
    }

    // 页签序 → id（原 tabs index 的取值语义：越界/空返回空串）
    std::string at(int index) const {
        if (index < 0 || index >= static_cast<int>(items_.size()))
            return std::string();
        return items_[static_cast<std::size_t>(index)];
    }

private:
    int rank(const std::string &id) const {
        for (std::size_t i = 0; i < order_.size(); ++i) {
            if (order_[i] == id)
                return int(i);
        }
        return int(order_.size()); // 未知 id 排尾（原 tile_index 同口径）
    }

    std::vector<std::string> order_; // 规范顺序（= 磁贴序）
    std::vector<std::string> items_; // 已选（保持规范序）
    std::string active_ = "jxl";     // 现役（空集时可为游离槽，见 R-266）
};

} // namespace pp::ui::fmtsel
