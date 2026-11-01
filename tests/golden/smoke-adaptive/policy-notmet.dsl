# M10 / W3-T4 自适应端到端策略（--adaptive-policy 面）：**不可达标** → max_rounds 终止。
# 语义（docs/v0.4.0-design.md §9.1-4/§9.1-5）：max_rounds 触顶 = 失败终止，但**输出最后一轮成功
# 编码**；sidecar 的 adaptive.reason = "max_rounds"、metric.state = "ok" 且 score < target。
# 复现：photo.jpg（64x64）+ jxl vardct（默认 distance=1.0）→ 轮值 0.9/0.8/0.7，分数 ~97 < 100。
version 1
target 100
max_rounds 3
output last
on_metric_unavailable stop

stage distance-up
  search distance from current to 0.5 step 0.1
