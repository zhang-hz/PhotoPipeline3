# M5 开发期裁定记录（主对话裁定，随波次滚动追加）

> 依据：`docs/qt-gui-design-spec.md`（v6 单源）与实现回报的冲突/缺口。裁定人=主对话；实施归属写明任务号。
> 口径：spec §4/§5（规则层）> 页面细节节（§7）> tokens.css/HTML 原型量化值；规范内冲突一律以 §4/§5 文字为准。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | **caption 单轨 46×32**：可见面与命中区同为 46×32（WinUI3，spec §3.3 逐字）；`frameless` 命中语义零弱化（带外按下不触发、HTMAXBUTTON Snap 保留）。`caption-band`/`caption-dual-track` 探针期望值**允许机械换锚**（语义不变），逐条登记 | spec §3.3「Caption 按钮：46×32」为规范正文；M4 双轨是顶栏 52 时代的过渡态 | T4（顶栏 44 换锚时收敛） |
| A2 | **双列行栅格以 §5 R2 为准**：`[80][12][轨][24][80][12][轨]`，字段轨仅 88/160/flex（160 档第二标签 x=276）。tokens.css `.frow2` 的 80/152/80/152（12 间距，第二标签 x=256）**作废**；T11 时对账 tokens.css（注记作废，PNG 不重渲、差异登记） | spec 声明「字段轨宽以本文为准」且 §5 为硬约束；152px 违反「字段宽只有三档」 | T2 已按此实现；T11 文档对账 |
| A3 | 文件行注释字阶取 **§4.2 辅助 10px**（§7.5 内「10.5 注释」表述作废） | 规则层 §4.2 字阶表 > 页面细节；§7.5 为示意行 | T5 |
| A4 | 徽标/导航数字圆取 **9.5px/700**（tokens.css 的 9px 作废） | 同 A3 口径 | T4 |
| A5 | `theme.h` 的 `style_mirrors_system_accent()` 改**显式引 QtWidgets**（`qApp->style()` 需 QApplication），消除隐式包含顺序雷 | 编译期隐患（T2 回报，已在 formkit.h 规避） | T4 顺手修复 |
| A6 | `filelistmodel.h` `kCheckBox=15` **废除自持副本**，统一 `theme::Metrics::checkbox_px=13`；全仓硬编码语义色（filelistmodel state_style、page_meta `0xcc8800`、exif_editor `0xdd3333`、page_output 磁贴 `0xc9c9d2` 等）**逐文件收口进令牌** | C1 令牌单源 | T5/T7/T8/T10 各自文件面 |
| A7 | **按钮高 26 换锚**：凡把"按钮"挂在 `input_h(24)` 的调用点改 `btn_h=26`/formkit 按钮（左栏 ＋/−、运行页取消、输出页浏览/预设、分类新建、shell CTA 除外——CTA 22 按 §5） | R4 表按钮高 26 | T4（壳）/T5（左栏）/T8/T9/T10 |
| A8 | **改值/状态标记零字形**：`.mod` 改值 = 强调边框+强调色字（无 `●` 前缀）；`〔批〕` → 中性 pill「批」；`↑↓` 操作提示进 tooltip；PillBadge 完成态 = 矢量勾 + 「已完成」文字（spec 表中「✓」视为速记，字形禁令优先）；文案排版中的 `→`/`—`（如「原 → 生效」）属正文保留 | C3 字形禁令 + §2.6 文案即界面 | T8（paramform）/T10（exif_editor）；T9 复核 |
| A9 | `RunMarkLabel` 补 `QEvent::ScreenChange` 失效缓存（DPI 切换重建 pixmap）；icons 探针对 `assets/icons/` 的硬断言维持（`--ui-smoke` 只在构建树运行，打包树不跑） | DPI 正确性；打包闭包不含 assets 不影响冒烟口径 | T9 / 不变 |
| A10 | `theme::Metrics::form_label_w(70)/form_row_h(32)` 迁移期双源，页面迁移完成后**退役**（转发 formkit_layout 或删除） | 消除双源 | T12 |
| A11 | spec §4/§5 与 tokens.css/HTML 量化值冲突，一律以 **§4/§5 文字**为准（T1 已裁定，本文复述确认）；已知取值差登记在 wave 报告（按钮 26/24、CTA 22/24、chip 11px/10.5px 等） | spec 自声明单源 | 已实施；T11 台账核对 |
