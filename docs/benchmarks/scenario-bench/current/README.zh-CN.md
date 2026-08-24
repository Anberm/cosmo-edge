# ScenarioBench 当前基准刷新报告

本目录指向 v1.0 发布文档冻结后的最新已审阅 benchmark 刷新。Canonical 数据保存在带版本号的 v1.1 报告包中，原始 `metrics.json` 不进入仓库。

## 72 小时双 CV 固定配置

| 平台 | 固定路数 | 最低 / 平均 FPS | 中文 | English |
| --- | ---: | --- | --- | --- |
| BM1688 | 8 | 4.68 / 5.086 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/dual-cv-72h/report.html">English</a> |
| CV186X | 8 | 4.54 / 5.085 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/dual-cv-72h/report.html">English</a> |
| RK3576 | 8 | 5.00 / 5.098 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/dual-cv-72h/report.html">English</a> |
| RV1126B | 4 | 4.85 / 5.230 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/dual-cv-72h/report.html">English</a> |

四个平台的固定配置均通过 24、48 与 72 小时完整性检查。这是受控本地循环输入下的长稳观测，不是容量极限，也不是 RTSP 或生产配置结论。

## VLM 性能展示汇总

| 平台 | 目标 FPS/路 | 80% 保守展示边界 | 中文 | English |
| --- | ---: | --- | --- | --- |
| BM1688 | 0.1 | 6 路 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/vlm-observation/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/vlm-observation/report.html">English</a> |
| CV186X | 0.1 | 6 路 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/vlm-observation/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/vlm-observation/report.html">English</a> |
| RK3576 | 0.1 | 4 路 | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/vlm-observation/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/vlm-observation/report.html">English</a> |

## 说明

- 72 小时刷新复用同一受控输入，每分钟采样，并以一份脱敏 canonical 数据和私有源哈希单独追溯。
- 2026-08-20 刷新统一使用 1080p24 受控本地循环输入、60 秒阶梯和任务本地完成计数。
- VLM 原始运行只记录 FPS、未启用 FPS 门禁；表格采用 v1.1 保守回算，不是精确容量或长稳结论。
- 启动敏感窗口不作为性能失败，也不增加展示路数；完整判定口径见 v1.1 方法说明和 canonical 数据。
- [此前的单设备报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/current/vlm-77175-npu/report.zh-CN.html)只作为已被取代的历史观测保留旧链接。
