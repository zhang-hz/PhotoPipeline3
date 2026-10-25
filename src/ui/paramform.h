// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — schema-driven parameter form (M1b frozen)
//
// PP-FROZEN(0.3.0) —— M5-T8（W-G3 §7.2「格式参数卡」）**追加面**（既有方法签名/语义一字不动）：
//     * `set_tokens(tokens)`：主题 tokens 注入（与 PageOutput/PageMeta/PreviewPanel 同一口径）——
//       M5-T8 起本控件的行一律经 formkit 构造（R1 pitch 28 / R2 标签轨 80 / R3 范围入 10px 注释 /
//       R4 控件规格），自绘控件（勾选框/折叠插入符）自带令牌副本，故需要本注入口；
//     * 布局形态（§7.2「一行一义六行」）：技术行 / 无损行 / 核心参数行 / 高级参数折叠行 ——
//       高级参数 = 现代折叠行（chevron + 摘要、无组框、无双层框线），区内含搜索行与子分组；
//       改值标记 = 强调边框 + 强调色字（theme::set_mod；裁定 A8：`●` 前缀已删）。
//   既有方法（set_selection/selection/values/set_values/is_param_visible 与两枚信号）语义不变。
#pragma once
#include "core/params.h"
#include <QWidget>
#include <string>
#include <vector>

namespace pp::ui {

// 前置声明（不拉 ui/theme.h：只为减头重/依赖面 —— theme.h 自 M5-A5 起已自含 <QApplication>，
// 本头又会被 AUTOMOC 的 mocs_compilation.cpp 直接包含；与 ui/page_output.h / page_meta.h 同口径）
namespace theme {
struct Tokens;
}

struct FormSelection {
    std::string backend;   // current backend id ("" -> first)
    std::string tech;      // current tech id ("" -> first)
    bool lossless = false; // lossless switch (only meaningful when tech lossless_capable)
};

class ParamForm : public QWidget {
    Q_OBJECT
public:
    // fmt: static format def (static_formats() entries are process-stable).
    // backends: effective backend list. For runtime-introspected formats (heif/avif)
    // pass pp::introspect_backends(fmt.id); for others pass fmt.backends.
    ParamForm(const pp::FormatDef &fmt, std::vector<pp::BackendDef> backends,
              QWidget *parent = nullptr);
    ~ParamForm(); // M1b-U3: +1 line vs §2.6 frozen text (moc needs a complete Impl; see report)

    void set_selection(const FormSelection &sel); // programmatic (no signal)
    FormSelection selection() const;
    pp::ParamSet values() const;            // current values, "__" keys excluded
    void set_values(const pp::ParamSet &s); // apply by key, unknown keys ignored

    // test/inspection hook: is the row for `key` currently visible?
    bool is_param_visible(const std::string &key) const;

    // ---- PP-FROZEN(0.3.0) 追加面（M5-T8）----
    void set_tokens(const theme::Tokens &tokens); // 主题令牌（自绘控件/图标/折叠区颜色）

    // ---- PP-FROZEN 加性追加（M7-R2D2；裁定登记 build/m7r2-d2/handoff.md）----
    // UX-10.1 档位 → 色度限幅（docs/ux-review-14.md §10.1-2）：把 `key` 枚举行的可选项收窄到
    // `allowed`（按 ParamValue 取值），其余项**置灰 + 禁用原因 tooltip**（不静默改写当前选择，
    // 不一致由调用方按 heif_profile_compatible 报中文错误）；`allowed` 为空 = 清除限幅（全可选）。
    // 值变化仍走 changed()；本接口只动视图（不发信号、不改工作集）。
    void limit_enum_choices(const std::string &key, const std::vector<int64_t> &allowed,
                            const QString &disabled_reason);
    // M7-R2D2 加性扩展：字符串枚举（HEIF/AVIF 的 chroma = "420"/"422"/"444"）的同一限幅面。
    // 整数版按 ParamValue 匹配，字符串版按 choices 的**显示值**匹配；两者语义一致（只动视图）。
    void limit_enum_choices_text(const std::string &key, const std::vector<std::string> &allowed,
                                 const QString &disabled_reason);

signals:
    void selection_changed(const pp::ui::FormSelection &sel); // user action only
    void changed();                                           // any value/selection change

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp::ui
