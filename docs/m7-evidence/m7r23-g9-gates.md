# m7r23-g9-gates.md — R2.3-G9「文档收口」自检读数

> 归属：M7-R2.3-G9（2026-10-13）。纯文档包：零 src/tests/tools 改动（`git diff --name-only`
> 自证，见 ③-1）。自检脚本 = `build/m7r23-g9/{ledger_check,link_check}.py`（gitignored 临时
> 探针）；三段式规矩见 `docs/m7-evidence/README.md`。

## ① 命令行与读数（G9 会话实跑，2026-10-13）

| 判据 | 命令 | 结果 |
|---|---|---|
| 台账计数自检 | `python build/m7r23-g9/ledger_check.py docs/m7-report.md` | **345 = 326/15/3/1**：A rows=331（已修 312 + 登记 15 + 误识别关闭 3 + 受约束口径闭环 1）+ B rows=14（已修 14）；`A missing=[]`（R-001…R-331 无缺号）、`A dup=[]`——与附录 A 表头注「R2.3 终态四档」计数逐字一致 |
| 相对链接自检 | `python build/m7r23-g9/link_check.py`（8 个改动/新建文档全文相对链接） | **LINK_CHECK checked=119 missing=0**（本文件入库后复跑；首跑 missing=1 即本文件尚未创建，如实留痕） |
| 文件归属面 | `git diff --name-only` + `git status --short` | 改动仅 `CHANGELOG.md`/`README.md`/`docs/m7-report.md`/`docs/m7-adjudications.md`/`docs/m7r2-plan.md`/`docs/m7-evidence/README.md` + 新建 `docs/m7-evidence/m7r23-g{7,8,9}-gates.md`；**tests/、src/、tools/ 零触碰**（金样/夹具/冻结行/回归基线零改动面自证） |
| 冻结行引用 | grep 本文档集 | `SMOKE total=20 pass=20 fail=0`、`UI-SMOKE OK shots=12 pages=3`、`PhotoPipeline 0.3.3`、`VERIFY` 行式仅以引文出现，无改写 |
| R2.3 提交面 | `git log --oneline 6894200..fe3b43f` | 7 提交 = G7×3（`83a9cb5`/`fa871c8`/`fe3b43f`）+ G8×4（`4c9d7db`/`af7687f`/`9b37f75`/`0b9a4bb`），与索引表/§9 一致；`origin/main` = `fe3b43f`（G7 明文授权推送），G9 报告类提交未推 |

## ② 台账终态再核对（上轮校读点名项，逐条）

| 点名项 | 复核结果（grep/读行实证） |
|---|---|
| R-266 行补余量注记 | **已改**：附录 A R-266 行主修并列 `27c9f40`+`4c9d7db`，M7R22G1-6 分歧注记改注「**已由 R2.3-G8 消**（`4c9d7db`，M7R23G8-1；format-roundtrip checked 15→16，读数归位 off-active=jpeg front=jpeg）」；§4.2 交互面行、§8.4 残余② 同步追记 |
| R-153 双哈希并列 | **核过无缺**（无需改）：`docs/m7-report.md:1335`「**已修** `d99df1b`+`d737cb4`」双哈希在案（体例同 R-183） |
| E5 余量行与「清零」对齐 | **核过一致**（无需改）：四处同口径「93001a9 余量清零」——§1.1（report :105）、§4.1 E 表 E5 行（:535）、附录 A R-007 行（:1315）、adjudications 五节 E5 行；无残留「余量登记」旧口径 |
| B.5 行与「六步全落」对齐 | **核过一致**（无需改）+ 一处消歧：§4.2（report :563）与 CHANGELOG「变更」节均作「六步全落（步① + 步②–⑥）」；adjudications M7R22G2-6 的「B.5 全五步」改「**B.5 步②–⑥ 全五步**」防与「六步」误读（G2 五步 = ②–⑥，步① 归 R2.1-F2） |
| 计数/引用/命名空间一致性 | 逐条修正入 §9.3：提交索引补「R2.3 轮 7 提交」；命名空间注补 `M7R23G*` 与漏列的 `M7R22G5-1`；包名对照补 G7/G8/G9；§1.3 补「R2.3 续表 = 十六节」；§8.4 残余①–⑤⑧ 补 R2.3 追记指针（消除「linux 读数未取得」「R-266 未修」等过期断言与新章的表面冲突）；尾注补 R2.3 增章；词表计数补「13 键 + 2 辅助常量 = 新增 15 条（111→126）」的算术口径（G8 handoff「13」与文件实测 15 行的差额 = kInfoRow0/kPpMetaPrefix 两辅助常量，git diff 实测归因） |

## ③ 事实取证（引用落点，逐条 grep/读码）

1. **R-266 修点**：`src/ui/page_output.cpp:157-161`（`closing_active && !sel.empty()` →
   `sel.set_active(sel.items().front())`）；模型真值表 `tests/unit/test_smoke.cpp` 零改动
   （`git show --stat 4c9d7db` 不含该文件）。
2. **ColorTemperature 受约束登记（M7R23G9-1）**：`src/ui/meta_format.cpp:172-175` 防御分支
   在案（`exif_text(exif, "Exif.Photo.ColorTemperature")` 空串即不拼接）；
   `tests/unit/test_meta_format.cpp:214-216` 注释逐字载「本 Exiv2（vcpkg 钉版）的键表不接受
   `Exif.Photo.ColorTemperature`（ExifKey 构造即抛 Invalid tag name）→ kelvin 拼接分支经
   ExifData 不可达（实测取证）」。
3. **词表 15 条新增常量**：`git diff 6894200..HEAD -- src/ui/meta_probe_context.h` 逐行列出
   kGpsEffectiveLabel/kDelta×6/kInfoRow0/kMapClose/kMapDialog/kMtimeSync×2/kPrivacyStrip×2/
   kPpMetaPrefix；文件计数 `grep -c "inline const QString k"` = 126（F2 `0ca8c62` 时点 = 111）。
4. **CI run 号**：main-full **#58**（id 38350019547，`fa871c8`）与 **#59**（id 38352494135，
   `fe3b43f`）均 success 10/10（公开 API 复取，命令见 m7r23-g7-gates.md ②）。
5. **代码面零改动声明**：R2.3 全部代码提交（G8 四条）落在 `src/ui/**`；`src/core`/
   `src/codecs`/`src/decode` 在 `35c4410..fe3b43f` 区间 **零 diff**（`git diff --stat` 自证）。

## ④ 结论

- 台账四档计数 **345 = 326/15/3/1** 终态不变（R2.3 消的是「已修」行内余量注记，主标签零改判）；
- 上轮校读点名的四条（R-266/R-153/E5/B.5）逐条复核完毕：1 条改实（R-266 注记）、3 条核过
  一致（B.5 顺带消歧 1 处）；
- 冻结行/金样/夹具/回归基线零改动（文件归属面自证）；本包零判据变更（纯文档）。
- **未跑（如实）**：ctest / ui_smoke / 金样 / regression / ci-format / ci-tidy——纯文档包不涉
  代码判据；代码面读数引 G8（m7r23-g8-gates.md）与 G5 终门禁（m7r22-gate.md）在案记录。
