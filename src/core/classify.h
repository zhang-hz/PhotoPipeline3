// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/classify（0.3.0 / M4-W1-T8：分类注册表 / 持久化 / 分组键）
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.6 行 `core/classify.h`（新）**已落地**
//   落地任务 = W1-T8（注册表 CRUD / `classes.json` 持久化 / 路径规范化 / 惰性清理 /
//   GroupKey 提取）；原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记（冻结头 SPDX 延续）。
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/classify.h`（新） | `ClassDef{id,name,rgb,hotkey}` / `ClassRegistry{classes, assignment(norm_path→id)}`；`save/load(data_dir()/classes.json)`；路径规范化=绝对路径+大小写折叠（Win） |
// clang-format on
//
// §6.2 分类子系统（逐字摘录）：
// clang-format off
// - `ClassRegistry`：增删改名/颜色/热键（0-9）；"全部"为保留虚拟分类。默认模板：精选/待定/废片。
// - 归属：一文件一分类（可无）；打标即写 registry；持久化 `data_dir()/classes.json`（键=规范化绝对路径；文件不在列表时惰性清理）。
// - 自动分组视图：`GroupKey` role 提供 `月份(YYYY-MM) | 相机型号 | 源格式` 三选一 + 「无分组」；QTreeView 节头=组名+计数。
// - 圈选语义（D2）：复选框 `checked` = 参与运行 = 批量规则作用范围；分类行右键菜单「全选该类/反选该类/清空勾选」；搜索过滤与勾选正交（勾选状态不因过滤丢失）。
// clang-format on
//
// 定稿说明（§3.6 未逐字给出字段类型/容器/函数签名 → 本任务按 §6.2 语义定稿；
//   0.3.0 冻结形态的字段名/语义与设计一致）：
//   * `ClassDef{id,name,rgb,hotkey}`：`rgb` = 0xRRGGBB；`hotkey` ∈ {'1'..'9'}（'0' 保留作
//     "清除"，0 = 未分配）。热键**唯一**：`add`/`set_hotkey` 在他类已占用该键时返回 false
//     （不抢占、不交换）。
//   * `ClassRegistry{classes, assignment(norm_path→id)}`：`classes` 有序 = UI 面板顺序；
//     `assignment` 键 = `norm_path()` 归一化绝对路径（Win 折叠大小写）。一文件一分类（可无）。
//   * 保留虚拟分类「全部」= id `"all"`（`kAllId`）：不可增/删/改名/设色/设热键，**不入库**
//     （`classes.json` 不落该 id，`load` 遇之即跳过），`assign(p,"all")` = 清除归属。
//   * 默认模板：精选 / 待定 / 废片（热键 '1'/'2'/'3'，与 §6.1 底部热键提示
//     `1 精选 2 待定 3 废片 0 清除` 逐字一致）。
//   * 持久化：UTF-8 JSON（最小自实现读写器；src/core/ 零 Qt —— Qt JSON 适配层仍是
//     `src/ui/preset_io.cpp`）。`save()` 原子写（`<file>.tmp` → rename）；`load()` 文件不存在
//     = 保持现状并成功（首次运行不是错误），损坏 = false + err 且注册表**不动**；
//     文件里的陈旧 id / 重复 id / 重复热键按"自愈"口径丢弃（键=id，热键先到先得）。
//   * 惰性清理：`prune_missing()` 删除"路径已不存在"的归属项（由消费端在文件列表刷新时调用；
//     不在 `load()` 内自动做 —— load 只见注册表，不知当前列表）。
//   * GroupKey 提取为**纯函数** `group_key(GroupMode, GroupSource)`（零 IO / 零 Qt）：
//     probe 摘要由 thumbs 通道收集后填 `GroupSource`；role 序号与节头渲染属 W2-T11（ui 层）。
//   * 边界与纪律：注册表/持久化/分组键**不含任何图像处理**（铁律五：无额外图像处理）；
//     分类只影响"是否参与运行"的勾选语义（§6.2 圈选语义），不改输出参数。
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · ClassDef / ClassRegistry（0.3.0 冻结形态）
// ============================================================================================

struct ClassDef {
    std::string id;        // 稳定标识（持久化键）；"all" 保留 = 虚拟"全部"（不入库/不入类表）
    std::string name;      // 显示名（可改名；默认模板：精选 / 待定 / 废片）
    std::uint32_t rgb = 0; // 颜色 0xRRGGBB（UI 徽标/节头着色）
    char hotkey = 0;       // 热键 '1'..'9'；'0' 保留作"清除"；0 = 未分配
};

class ClassRegistry {
public:
    // 保留虚拟分类「全部」的 id（§6.2："全部"为保留虚拟分类；不入库）
    static constexpr std::string_view kAllId = "all";

    // 有序（UI 面板顺序）；默认模板 = 精选/待定/废片
    std::vector<ClassDef> classes;
    // norm_path → class id（一文件一分类，可无；空缺项 = 无分类）
    std::map<std::string, std::string> assignment;

    ClassRegistry(); // 默认模板（§6.2）

    // —— CRUD（§6.2：增删改名/颜色/热键（0-9）；"全部"为保留虚拟分类）——
    // 全部返回 false = 被拒（id 为空/保留/重复、名称为空、热键非法或已被他类占用、无此类）；
    // 任一被拒操作都不改动注册表（强异常安全）。
    bool add(const ClassDef &c);
    bool rename(const std::string &id, const std::string &name);
    bool erase(const std::string &id); // 连同该类全部归属一并删除
    bool set_color(const std::string &id, std::uint32_t rgb);
    bool set_hotkey(const std::string &id, char key);

    // id 查表；nullptr = 无此类（含保留 id "all"）
    const ClassDef *find(const std::string &id) const;

    // —— 归属（打标即写 registry：一文件一分类，可无）——
    // id 为空 / 保留 id / 未知 id → 等价 clear_assignment()（"全部"与陈旧 id 不落归属）
    void assign(const std::filesystem::path &p, const std::string &id);
    void clear_assignment(const std::filesystem::path &p);
    // 无分类（含未知 id 的陈旧项）→ nullopt
    std::optional<std::string> class_of(const std::filesystem::path &p) const;
    // 惰性清理：删除"文件已不存在"的归属项（§6.2）
    void prune_missing();

    // —— 持久化（`data_dir()/classes.json`；路径单源 = core/settings.h 的 class_file，§3.6）——
    // 返回 true = 成功（文件不存在也视为成功：保持默认模板，不报错）
    bool load(const std::filesystem::path &file, std::string *err = nullptr);
    bool save(const std::filesystem::path &file, std::string *err = nullptr) const;

    // 绝对路径 + 大小写折叠（**仅 Windows**；其他平台保持大小写）+ 分隔符统一 '/' +
    // 折叠 `..`/`.`/重复分隔符/尾分隔符。空路径 → 空串（不解析为 cwd）。
    static std::string norm_path(const std::filesystem::path &p);
};

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · GroupKey 提取（§6.2 自动分组视图；纯函数，零 IO）
// ============================================================================================
// 自动分组视图：`月份(YYYY-MM) | 相机型号 | 源格式` 三选一 + 「无分组」（§6.2）。

enum class GroupMode { None, Month, Camera, SourceFormat }; // None = 「无分组」（单一节）

struct GroupSource {               // probe 摘要（thumbs 通道收集；本函数只读）
    std::string datetime_original; // "YYYY:MM:DD HH:MM:SS"（EXIF 形态；可空）
    std::string camera_model;      // Exif.Image.Model（可空）
    std::string source_format;     // 源格式 id（"jpeg"/"cr2"/…；可空）
};

// 分组键：返回空串 = 「无分组」（缺失/不可解析的字段归入无分组节，不抛错、不猜值）
//   Month        → "YYYY-MM"（接受 "YYYY:MM:DD…" 与 ISO "YYYY-MM-DD…"；月非法 → 空串）
//   Camera       → 相机型号（去首尾空白）
//   SourceFormat → 源格式（去首尾空白 + 去前导 '.' + 小写）
//   None         → 空串（单一节）
std::string group_key(GroupMode mode, const GroupSource &src);

// 「无分组」节头名（§6.2；消费端用 key 为空时呈现）
inline constexpr std::string_view kNoGroupLabel = "无分组";

} // namespace pp
