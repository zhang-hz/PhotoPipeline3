# smoke-adaptive（dev-only 自适应端到端夹具；**不进金样 20 对**）

M10 / W3-T4 的端到端验收面（docs/v0.4.0-design.md §9.9-3 / §12.5）：证明"格式预设携带策略 →
pipeline 轮循环 → 同步评分 → 选定候选 → 侧车 `adaptive` 对象"这条链**在真实编码器上**成立。
本目录只有策略/预设文本，**没有**图片语料（输入复用 `tests/golden/base/` 的冻结小图）。

## 文件

| 文件 | 作用 |
|---|---|
| `smoke-adaptive.json` | 格式预设 v1（`kind="format-preset"`）：webp/libwebp/lossy、`quality=80`、`adaptive.enabled=true`、策略 = 目标 90 / 最多 3 轮 / `search quality 80→100 step 5` |
| `policy-notmet.dsl` | 独立策略 DSL（`--adaptive-policy` 面）：jxl/libjxl/vardct、目标 100（不可达）/最多 3 轮 / `search distance 1.0→0.5 step 0.1` |
| `README.md` | 本文件（复现命令 + 断言口径） |

## 复现（在 `-DPP_BUILD_DEV=ON` 的构建树上；示例树 = `build/m10/w3-exec`）

```powershell
# A) --preset 携带自适应（目标 90 可达）→ reason=met / score>=90
python tools/env.py run -- build/m10/w3-exec/photopipeline.exe --dev `
    tests/golden/base/rgb8.png --out <OUT_A> `
    --preset tests/golden/smoke-adaptive/smoke-adaptive.json --workers 1

# B) --adaptive-policy（目标 100 不可达）→ reason=max_rounds / 3 轮 / 输出最后一轮成功编码
python tools/env.py run -- build/m10/w3-exec/photopipeline.exe --dev `
    tests/golden/base/photo.jpg --out <OUT_B> --format jxl --tech vardct `
    --adaptive-policy tests/golden/smoke-adaptive/policy-notmet.dsl --workers 1
```

## 断言（实测值见 build/m10/w3-exec/report.md）

A（`<OUT_A>/rgb8.webp.pp.json`）：
- `adaptive.reason == "met"`、`adaptive.selected_round == 1`、`adaptive.rounds[0].decision == "met"`；
- `adaptive.target == 90`、`adaptive.max_rounds == 3`、`adaptive.output == "last"`；
- `metric.state == "ok"` 且 `metric.score >= 90`；
- 产物可解码（二次转码 exit 0）；**同命令二次运行产物 SHA256 相同**；
- info 日志含 `adaptive round {…}` 与 `adaptive summary {… policy_sha=<前 16> …}`。

B（`<OUT_B>/photo.jxl.pp.json`）：
- `adaptive.reason == "max_rounds"`、`adaptive.rounds` 长度 = 3、`adaptive.selected_round == 3`；
- `metric.state == "ok"` 且 `metric.score == adaptive.final_score < adaptive.target`（未达标仍产出）；
- 产物 = 第 3 轮候选（`out_bytes` 与 `rounds[2].bytes` 一致）。

## 不属于本目录的门禁

- `tests/golden/smoke/*.json`（20 对断言级金样）与 `tests/golden/base|edge|meta` 语料**零改动**；
- 回归（`tools/regression.py`）不带预设/不带自适应 ⇒ zero diff（默认关闭路径逐字节不变）。
