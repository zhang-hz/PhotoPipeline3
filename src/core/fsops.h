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

// 输入扩展名白名单（13 种扩展名）：tif/tiff/png/jpg/jpeg/jxl/heic/heif/avif/webp/bmp/gif/tga
const std::vector<std::string> &input_extensions();

// 替换扩展名（保留目录与 stem）
std::filesystem::path with_extension(const std::filesystem::path &p, std::string_view new_ext);

// ===========================================================================
// M7-WP1 加性追加（R-003 / R-005 / R-007 / R-028 / R-031 / R-083 / I1 / I2）
//   裁定登记：build/m7-wp1/handoff.md「裁定登记」节（加性 + 追认：既有签名逐字保留、
//   既有语义零改动；新能力一律新函数/新重载承载）。
// ===========================================================================

// —— I1 原子写四件套（R-003/R-028/R-194/R-290）——
// 临时文件名：<target 文件名>.<pid>.<随机十六进制>.tmp（同目录；双开/同进程并发写互不
//   干扰，R-028 的固定名 `.tmp` 互踩问题即由此根治）。target 为空 → 返回空 path。
std::filesystem::path atomic_temp_path(const std::filesystem::path &target);

// tmp → target 的原子替换（同目录 rename）。目标已存在 → 覆盖（POSIX rename 与 MSVC
//   std::filesystem::rename 的标准语义；test_fsops 有"覆盖已存在目标"实证用例）。
//   失败 → false 且 *err 非空，tmp **不**删除（调用方决定重试或 discard_temp_file）。
bool commit_temp_file(const std::filesystem::path &tmp, const std::filesystem::path &target,
                      std::string *err);

// 失败清场：尽力删除临时文件（noexcept；删除失败静默 —— 清场不得抛异常）。
void discard_temp_file(const std::filesystem::path &tmp) noexcept;

// 一步式原子写：写 data → **字节数校验**（落盘 size == data.size()）→ commit_temp_file
//   覆盖 target；任一步失败 → discard_temp_file 清场 + false + *err。target 已存在时
//   继承其权限位（preset_io 的既有行为收口到这里）。data 为空 = 合法（写空文件）。
bool write_atomic_file(const std::filesystem::path &target, std::string_view data,
                       std::string *err);

// —— I2 输出名/输出根校验（R-007）——
// 输出路径合法性（**平台无关判定**，与 validate_output_template 同口径的词法规则）：
//   * 每个路径段：非法字符 `< > : " | ? *` 与 ASCII 控制字符（0x00–0x1F）；
//   * 每个路径段不得以点/空格结尾（Windows 静默截断面）；
//   * 保留设备名（大小写不敏感、含带扩展名形态 CON.txt）：CON/PRN/AUX/NUL/COM1–9/LPT1–9；
//   * 长度：**M7-R2B 解除 259/255 硬线**（R-007 收口）—— 写入口统一经 normalize_extended_path()
//     归一 `\\?\` 扩展形式后超长路径（>260 整串 / >255 段）可真实落盘，长度不再按 MAX_PATH
//     预检拦截；保留名/非法字符/尾随点空格仍拒（这些在 `\\?\` 形式下会被**字面保留**而非
//     静默截断，属真实数据破坏面，防线不撤）；
//   * 末段必须是文件名（非空）。
// 判负 → false 且 *err 为非空英文描述；err 可为 nullptr。
bool validate_output_filename(const std::filesystem::path &p, std::string *err);

// 输出根探针（R-007/R-073 的引擎侧预检）：目录不存在 → 试建；随后试写一个唯一探针文件
//   并删除。不可建/不可写 → false 且 *err 非空。dir 为空 → 判负。
bool probe_output_root(const std::filesystem::path &dir, std::string *err);

// —— R-005 加性重载：软保留（幽灵跳过防线）——
// `reserved` = 硬保留（同一文件更早 target 已解析出、**尚未落盘**的路径）；
// `soft_reserved` = 软保留（批内已完成登记、但磁盘上可能不存在的路径：登记时失败的输出
//   不再入册，唯余"成功后被外部删除"一类残留）。
//   * Skip：目标**在磁盘上存在**或命中硬保留 → 跳过；仅命中软保留而磁盘缺失 → 回退
//     正常写入（R-005：不许把"幽灵占位"当已存在输出）；
//   * Rename/Overwrite：软硬保留一视同仁（保持既有避让语义）。
// 既有 4 参签名转发到本重载（软保留空集）→ 既有行为逐字不变。
OutputPlan resolve_conflict(const std::filesystem::path &desired, ConflictPolicy policy,
                            const std::vector<std::filesystem::path> &reserved,
                            const std::vector<std::filesystem::path> &soft_reserved,
                            std::string &err);

// —— R-031 加性重载：收集时排除输出根子树 ——
// `exclude_roots` 内的目录/文件整棵排除（is_inside 判定；含根自身）—— 仅元数据模式把
// out_root 放进输入树时不再把产物收集为下一轮输入（`out/out/…` 镜像堆叠问题）。
// 既有 3 参签名转发（空排除集）→ 既有行为逐字不变。
std::vector<std::filesystem::path> collect_inputs(
    const std::vector<std::filesystem::path> &roots, const std::vector<std::string> &exts,
    const std::vector<std::filesystem::path> &exclude_roots, std::vector<std::string> &errors);

// —— M7-R2B 加性追加（R-007 长路径 `\\?\` 归一收口；裁定登记 build/m7r2-b/handoff.md）——
// Windows 长路径归一：**绝对**路径归一为 `\\?\` 扩展形式（drive 形态 `C:\x` → `\\?\C:\x`；
// UNC 形态 `\\server\share\x` → `\\?\UNC\server\share\x`），绕过 MAX_PATH=260 的 open 上限
// （fopen/Exiv2 写入口统一经本函数，见 metadata.cpp 的 exiv2_apply 族；codecs 侧写盘点
// 已随 M7-R2.1-F4 收口——enc_heif fopen / enc_oiio open / resolve_conflict exists 探测）。
// 幂等：已带 `\\?\`（或 `\\.\` 设备命名空间）
// 前缀原样返回；相对路径/空路径原样返回（无稳定绝对语义，不归一）；非 Windows 恒原样返回
// （POSIX 无 MAX_PATH）。分隔符归一为 `\`（`\\?\` 命名空间不接受 `/`）。
std::filesystem::path normalize_extended_path(const std::filesystem::path &p);

// —— M7-R2.1-F4 / R-239 加性追加（末尾；裁定登记 docs/m7-adjudications.md 十一节）——
// ConflictPolicy ↔ 文本映射**单源**（词表 skip|overwrite|rename，与 SCHEMA/预设 JSON/CLI
// 既有词表逐字相同）。旧三套副本（dev_cli.cpp parse_conflict/conflict_text、preset_io.cpp
// parse_conflict_name/conflict_name）转发到本组；page_output 侧是 itemData 枚举往返（无
// 文本表）不涉。默认值分歧（dev=overwrite / GUI=rename / JSON=rename）不由映射层收口——
// R-145 取「--help 醒目标注」路线。既有签名/行为零改动。
const char *conflict_policy_name(ConflictPolicy p);
bool parse_conflict_policy(std::string_view s, ConflictPolicy &out);

// ===========================================================================
// M8-W1 加性追加（输出目录写入权限 / UAC 流程，需求 1）
//   口径：**加性 + 追认** —— 既有 probe_output_root 签名逐字保留（实现改为转发到新探针，
//   判负时 *err 文案增强为携带 OS 错误文本与错误码；冻结行为 = true/false 与「*err 非空」
//   的契约零改动）；新能力全部由新类型/新函数承载，既有函数零改动。
//   裁定登记：docs/m8-adjudications.md（由 Lead 登记；本卡不改文档面）。
//
//   动机（0.3.5 已知残余，docs/0.3.5-report.md:123-124）：
//   * 旧 UI 用**错误文本**猜「是否权限问题」（`error_looks_like_permission`），probe 的
//     `not writable` 分支又**不带 OS 错误** → 磁盘满/只读介质也会弹 UAC；同时中文系统上
//     Win32 错误文本本地化，文本匹配本质不可靠。→ 改为**结构化错误分类**（OS 错误码）。
//   * 旧提权经 `cmd /c ... mkdir ... & icacls ...`：`%VAR%` 展开风险、引号截断风险，且
//     路径/账户含 `%` 时直接放弃提权（功能空洞）。→ 新参数构造函数**不经 cmd**、用 SID
//     授权，纯函数可单测。
// ===========================================================================

// 输出根探针的结构化结果：把「为什么不可写」从文本猜测变成可判定的错误码。
enum class OutputRootProbeStatus {
    Ok = 0,       // 可建、是目录、可写
    EmptyPath,    // 输出根为空
    NotCreatable, // create_directories 失败（父目录无写权 / 盘符不存在 / 路径非法…）
    NotDirectory, // 路径存在但不是目录（普通文件 / 设备）
    WriteDenied,  // 试写被拒且**属 ACL/权限类**（should_offer_elevation 的唯一可提权状态）
    WriteFailed,  // 试写失败但**非**权限类（磁盘满 / 只读介质 / 配额 / I/O 错误…）
};

struct OutputRootProbe {
    OutputRootProbeStatus status = OutputRootProbeStatus::Ok;
    unsigned long os_error = 0;             // Win32 GetLastError / POSIX errno；0 = 无 OS 错误
    std::string detail;                     // 可读描述（含路径 + OS 文本 + 码）；status==Ok 时为空
    std::filesystem::path nearest_existing; // 根不存在/不可建时最近的已存在目录（诊断用）
};

// 结构化输出根探针（语义 = probe_output_root 的超集）：目录不存在 → 试建；随后试写一个
// 唯一探针文件并删除（失败也清场）。错误分类按 **OS 错误码**（不按文本）：
//   磁盘满(112/28) / 只读介质(19/30) / 共享冲突(32) / 路径不存在(3) → WriteFailed/NotCreatable，
//   只有 ERROR_ACCESS_DENIED(5)、ERROR_PRIVILEGE_NOT_HELD(1314)（POSIX: EACCES/EPERM）
//   → WriteDenied。detail 保留既有前缀 "output root is ..."（error_text.h 前缀族继续命中）。
OutputRootProbe probe_output_root_ex(const std::filesystem::path &dir);

// 状态名（日志字段用；也是测试断言的稳定文本）。
const char *output_root_probe_status_name(OutputRootProbeStatus status);

// 是否值得「申请管理员授权」（UAC）。纯函数（不落盘/不访问 OS）+ 可单测：
//   仅 WriteDenied（ACL 类拒绝）为 true；磁盘满/只读介质/路径不存在等一律 false。
//   非 Windows 恒 false（POSIX 无 UAC；权限不足由 UI 直接给可操作提示）。
bool should_offer_elevation(const OutputRootProbe &probe);

// 当前进程用户 SID（形如 "S-1-5-21-…"）；非 Windows / 取不到 → 空串。
//   用 SID 而非账户名：域账户/本地账户/重命名账户/超长账户名下，账户名授权会授错主体，
//   SID 是唯一稳定身份（icacls 的 `*S-1-…` 形式）。
std::string current_user_sid();

// SID 字符串合法性（"S-1-<authority>-<sub1>-…"：revision 必须 1；authority ≤ 2^48-1；
//   子权威 1..15 个、各 ≤ 2^32-1；无前导零/多余字符）。
bool is_valid_sid(std::string_view sid);

// icacls.exe 的参数串（**不经 cmd**；SID 授权；Windows 命令行引号规则）：
//   "<绝对根路径>" /grant *<sid>:(OI)(CI)M
//   返回空 = 不可安全构造（root 为空/非绝对/含 '"' 或控制字符 / SID 非法）。
//   路径中的 `%VAR%` 原样保留 —— ShellExecuteExW→CreateProcess 不做 cmd 环境变量展开，
//   这正是旧 cmd /c 实现的功能空洞（含 % 的路径以前直接放弃提权）。
std::string icacls_grant_parameters(const std::filesystem::path &root, std::string_view sid);

} // namespace pp
