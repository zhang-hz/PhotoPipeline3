// PP-FROZEN(file)
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

// 镜像路径：out_root / (src 相对 base_dir 的路径)，扩展名替换为 new_ext（不含点）
// base_dir 非 src 前缀时 → out_root / src.filename()
// new_ext 为空 → 保留原扩展名
std::filesystem::path mirror_path(const std::filesystem::path& src,
                                  const std::filesystem::path& base_dir,
                                  const std::filesystem::path& out_root,
                                  std::string_view new_ext);

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
