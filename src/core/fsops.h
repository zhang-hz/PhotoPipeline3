// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.4（依据 docs/v0.3.0-design.md §3.4，"加性为主"）
//   本文件内 PathCtx / render_output_path（替代 mirror_path）/ validate_output_template 为
//   0.3.0 一次性解冻（D20）授权变更面；落地任务 = W1-T5（输出路径真值表 §4.2）。
//   对应任务落地后：把本文件内的 PP-THAWED 标记改标为 PP-FROZEN(0.3.0)（冻结头 SPDX 延续）。
//   本文件其余声明（resolve_conflict / collect_inputs / is_inside / input_extensions /
//   with_extension / OutputPlan / ConflictPolicy）签名不变 → 维持 PP-FROZEN 只读。
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

enum class ConflictPolicy { Skip, Overwrite, Rename };

struct OutputPlan {
    std::filesystem::path out_path;  // 最终路径
    bool skip = false;               // Skip 策略且目标已存在
    int rename_index = 0;            // Rename 序号（0=原名；1→"name (1).ext"）
};

// PP-THAWED(0.3.0-M4-D20) §3.4 · PathCtx + render_output_path + validate_output_template
//   [重排] mirror_path() → render_output_path()（路径模板解析，§4.2）；PathCtx 与
//   validate_output_template 为 0.3.0 新增。落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.4 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// // [重排] mirror_path() → render_output_path()（路径模板解析，§4.2）：
// struct PathCtx { std::string format_dir;  // "jpeg"/"webp"/…
//                  std::filesystem::path rel_dir;  // 源相对目录
//                  std::string stem, ext; };       // 主名 / 新扩展名
// std::filesystem::path render_output_path(std::string_view tmpl, const PathCtx&,
//                                          const std::filesystem::path& out_root);
// bool validate_output_template(std::string_view tmpl, std::string* err); // 未知符号/.. 校验
// // resolve_conflict() / collect_inputs() 不变；reserved 集合改为按"每个 out_path"登记（§4.4）
// clang-format on
//   注（落地提示，T5 处置）：§3.1 的 OutputTarget 注释写作 `fsops::output_path()`，本节定名为
//   `render_output_path()` —— 以 §3.4 为准（M4-T1 已上报，W5 收口入 m4-report）。
//   §4.2 补充（模板语法，供 T5 实现）：符号 $format/$dir/$file/$name/$ext；段间 `/`；字面段可混排；
//   未知 `$` 符号=校验错误；禁止 `..` 与绝对路径；`$dir` 空段坍缩斜杠。
// PP-THAWED(0.3.0-M4-D20) §3.4：本声明在 0.3.0 被 render_output_path() 替代（形态见上）
// 镜像路径：out_root / (src 相对 base_dir 的路径)，扩展名替换为 new_ext（不含点）
// base_dir 非 src 前缀时 → out_root / src.filename()
// new_ext 为空 → 保留原扩展名
std::filesystem::path mirror_path(const std::filesystem::path& src,
                                  const std::filesystem::path& base_dir,
                                  const std::filesystem::path& out_root,
                                  std::string_view new_ext);

// PP-THAWED(0.3.0-M4-D20) §3.4：本函数签名**不变**；仅 `reserved` 集合的登记粒度改为
// 按"每个 out_path"（§4.4；先逐输出独立应用，再按 out_path 快照登记）——由 T5 在调用侧落地。
// 冲突解析：desired 已存在或落在 reserved 中时按 policy 处理
// Rename：依次尝试 "stem (1).ext"、"stem (2).ext"…（上限 10000，超出返回 err）
// reserved 为本批次已分配但可能尚未落盘的路径（批内冲突，G4）
OutputPlan resolve_conflict(const std::filesystem::path& desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path>& reserved,
                            std::string& err);

// child 是否位于 parent 之内（词法比较，均先 weakly_canonical）
bool is_inside(const std::filesystem::path& child, const std::filesystem::path& parent);

// 递归收集：扩展名白名单（大小写不敏感，不含点）；目录不存在/无权限 → 跳过并记入 errors
std::vector<std::filesystem::path> collect_inputs(const std::vector<std::filesystem::path>& roots,
                                                  const std::vector<std::string>& exts,
                                                  std::vector<std::string>& errors);

// 输入扩展名白名单（10 种输入）：tif/tiff/png/jpg/jpeg/jxl/heic/heif/avif/webp/bmp/gif/tga
const std::vector<std::string>& input_extensions();

// 替换扩展名（保留目录与 stem）
std::filesystem::path with_extension(const std::filesystem::path& p, std::string_view new_ext);

}  // namespace pp
