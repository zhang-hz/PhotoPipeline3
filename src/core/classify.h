// PP-THAWED(0.3.0-M4-D20): 纯规格块（M4-W0-T1 落注）—— 0.3.0 新模块规格，**零声明、零实现、未接入构建**
//   落地任务 = W1-T8（core/classify：注册表/持久化/分组键；出口 `test_classify` 绿）→ 落地后改标 PP-FROZEN(0.3.0)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — classify spec (0.3.0 / M4-W0-T1：仅规格注释，待 W1-T8 落地为声明 + 实现)
//   依据 docs/v0.3.0-design.md §3.6 表行（`core/classify.h`（新））+ §6.2（分类子系统语义）
//   本文件当前不写任何声明/实现，也不在 CMakeLists.txt 中编译（T1 不接入构建）。
#pragma once

// ============================================================================================
// PP-THAWED(0.3.0-M4-D20) §3.6 · core/classify.h（新）
//   裁定原文（逐字抄录，剥去行首 "// " 前缀即设计原文）：
// clang-format off
// | `core/classify.h`（新） | `ClassDef{id,name,rgb,hotkey}` / `ClassRegistry{classes, assignment(norm_path→id)}`；`save/load(data_dir()/classes.json)`；路径规范化=绝对路径+大小写折叠（Win） |
// clang-format on
//
// §6.2 分类子系统（逐字摘录，供 W1-T8 落地）：
// clang-format off
// - `ClassRegistry`：增删改名/颜色/热键（0-9）；"全部"为保留虚拟分类。默认模板：精选/待定/废片。
// - 归属：一文件一分类（可无）；打标即写 registry；持久化 `data_dir()/classes.json`（键=规范化绝对路径；文件不在列表时惰性清理）。
// - 自动分组视图：`GroupKey` role 提供 `月份(YYYY-MM) | 相机型号 | 源格式` 三选一 + 「无分组」；QTreeView 节头=组名+计数。
// - 圈选语义（D2）：复选框 `checked` = 参与运行 = 批量规则作用范围；分类行右键菜单「全选该类/反选该类/清空勾选」；搜索过滤与勾选正交（勾选状态不因过滤丢失）。
// clang-format on
//
// 建议形态（§3.6 未逐字给出字段类型/容器/函数签名 → 由 W1-T8 定稿，非本任务改动）：
// clang-format off
// struct ClassDef {
//     std::string id;        // 稳定标识（持久化键；"all" 保留 = 虚拟"全部"分类，不落盘）
//     std::string name;      // 显示名（可改名；默认模板：精选 / 待定 / 废片）
//     std::uint32_t rgb;     // 颜色（UI 徽标/节头着色）
//     char hotkey;           // 热键 0-9（'0' 保留作"清除"；热键表随注册表动态生成，§6.1）
// };
//
// class ClassRegistry {
// public:
//     std::vector<ClassDef> classes;                      // 有序（UI 面板顺序）
//     std::map<std::string, std::string> assignment;      // norm_path → class id（一文件一分类，可无）
//     // 增删改名/颜色/热键（0-9）；"全部"为保留虚拟分类（不可删/不可改名）
//     bool  add(const ClassDef&);  bool rename(const std::string& id, const std::string& name);
//     bool  erase(const std::string& id);  bool set_color(const std::string& id, std::uint32_t rgb);
//     bool  set_hotkey(const std::string& id, char key);
//     // 归属：打标即写 registry（一文件一分类；清除 = 移除映射）
//     void  assign(const std::filesystem::path& p, const std::string& id);
//     void  clear_assignment(const std::filesystem::path& p);
//     std::optional<std::string> class_of(const std::filesystem::path& p) const;
//     void  prune_missing();                              // 文件不在列表时惰性清理
//     // 持久化：data_dir()/classes.json（路径单源见 core/settings.h 的 class_file，§3.6）
//     bool  load(const std::filesystem::path& file, std::string* err = nullptr);
//     bool  save(const std::filesystem::path& file, std::string* err = nullptr) const;
//     static std::string norm_path(const std::filesystem::path& p);   // 绝对路径 + 大小写折叠（Win）
// };
// clang-format on
//
// 边界与纪律：路径规范化 = 绝对路径 + 大小写折叠（仅 Windows；其他平台保持大小写），
//   是 `assignment` 键与 `classes.json` 往返的唯一口径；注册表/持久化不含任何图像处理（铁律五：
//   无额外图像处理）；分类只影响"是否参与运行"的勾选语义（§6.2 圈选语义），不改输出参数。
// ============================================================================================
