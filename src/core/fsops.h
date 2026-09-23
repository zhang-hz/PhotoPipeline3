// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.4 **已落地**（依据 docs/v0.3.0-design.md §3.4，"加性为主"）
//   落地任务 = W1-T5（输出路径真值表 §4.2）；原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为
//   本标记（冻结头 SPDX 延续）。
//   本文件其余声明（resolve_conflict / collect_inputs / is_inside / input_extensions /
//   with_extension / OutputPlan / ConflictPolicy）签名不变 → 维持 PP-FROZEN 只读。
//   0.2 的 mirror_path() 已按 §3.4 的 [重排] 行被 render_output_path() 取代（函数退场；
//   test_fsops 的 mirror_path 用例同步改造为 render_output_path 用例）。
//   §4.2 补充（模板语法，T5 实现口径）：符号 $format/$dir/$file/$name/$ext；段间 `/`；
//   字面段可混排（photos-$format）；未知 `$` 符号 = 校验错误；禁止 `..` 与绝对路径；
//   `$dir` 空段坍缩斜杠。
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

enum class ConflictPolicy { Skip, Overwrite, Rename };

struct OutputPlan {
    std::filesystem::path out_path; // 最终路径
    bool skip = false;              // Skip 策略且目标已存在
    int rename_index = 0;           // Rename 序号（0=原名；1→"name (1).ext"）
};

// [重排] mirror_path() → render_output_path()（路径模板解析，§4.2）：
struct PathCtx {
    std::string format_dir;        // "jpeg"/"webp"/…
    std::filesystem::path rel_dir; // 源相对目录
    std::string stem, ext;         // 主名 / 新扩展名
};

// 模板解析（纯函数，可单测）：
//   * 段间以 '/' 分隔；空段（"a//b"、"a/"）坍缩；符号与字面段可混排（photos-$format）；
//   * $format=ctx.format_dir；$dir=ctx.rel_dir；$file=stem.ext；$name=stem；$ext=ext；
//   * ctx.ext 为空 → $file 退化为不含点的主名（metadata-only 的"保持原扩展名"口径由调用方
//     把 ctx.ext 设为源扩展名来表达）；
//   * 展开后为空的段整体坍缩（$dir 为空 → "jpeg/x.jpg"，而非 "jpeg//x.jpg"）；
//   * 非法模板（validate_output_template 判负）或展开结果为空 → 返回空 path
//     （调用方必须先 validate_output_template 并在 ready_to_start 阻断）。
std::filesystem::path render_output_path(std::string_view tmpl, const PathCtx &,
                                         const std::filesystem::path &out_root);

// 未知符号 / `..` / 绝对路径校验（§3.4 冻结签名）。判负 → false 且 *err 为非空英文描述。
// 平台无关判定（自写词法规则，不用 std::filesystem 的平台相关解析：'\\' 与 "C:" 一并拒绝，
// 保 M3 铁律六"零平台分叉"）。
bool validate_output_template(std::string_view tmpl, std::string *err);

// 0.3.0（T5 加性小助手，§3.4「加性为主」范围内）：src 相对 base_dir 的目录。
// 这是 PathCtx.rel_dir 的唯一来源，pipeline（真实输出路径）与 scheduler（§4.4 inflight 键）
// 共用同一语义 —— 两处各自镜像推导会漂移，故提为单实现。base_dir 非 src 前缀/为空 → 返回空。
std::filesystem::path relative_dir(const std::filesystem::path &src,
                                   const std::filesystem::path &base_dir);

// 冲突解析：desired 已存在或落在 reserved 中时按 policy 处理
// Rename：依次尝试 "stem (1).ext"、"stem (2).ext"…（上限 10000，超出返回 err）
// reserved 为本批次已分配但可能尚未落盘的路径（批内冲突，G4）
// 0.3.0 口径（§4.4）：reserved 按"每个 out_path"登记；逐输出独立应用本函数
// （一源的 N 个输出各自解析，互不共用序号）。
OutputPlan resolve_conflict(const std::filesystem::path &desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path> &reserved, std::string &err);

// child 是否位于 parent 之内（词法比较，均先 weakly_canonical）
bool is_inside(const std::filesystem::path &child, const std::filesystem::path &parent);

// 递归收集：扩展名白名单（大小写不敏感，不含点）；目录不存在/无权限 → 跳过并记入 errors
std::vector<std::filesystem::path> collect_inputs(const std::vector<std::filesystem::path> &roots,
                                                  const std::vector<std::string> &exts,
                                                  std::vector<std::string> &errors);

// 输入扩展名白名单（10 种输入）：tif/tiff/png/jpg/jpeg/jxl/heic/heif/avif/webp/bmp/gif/tga
const std::vector<std::string> &input_extensions();

// 替换扩展名（保留目录与 stem）
std::filesystem::path with_extension(const std::filesystem::path &p, std::string_view new_ext);

} // namespace pp
