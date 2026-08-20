# ScenarioBench 当前基准刷新报告

本目录指向 v1.0 发布文档冻结后的最新已审阅 benchmark 刷新。Canonical 数据保存在带版本号的 v1.1 报告包中，原始 `metrics.json` 不进入仓库。

## 汇总

| 平台 | 目标 FPS/路 | 本轮门禁结果 | 中文 | English |
| --- | ---: | --- | --- | --- |
| BM1688 | 0.1 | 8 路通过 | <a href="../v1.1/results/bm1688/vlm-observation/report.zh-CN.html">打开</a> | <a href="../v1.1/results/bm1688/vlm-observation/report.html">English</a> |
| CV186X | 0.1 | 6 路通过 | <a href="../v1.1/results/cv186x/vlm-observation/report.zh-CN.html">打开</a> | <a href="../v1.1/results/cv186x/vlm-observation/report.html">English</a> |
| RK3576 | 0.1 | 6 路通过 | <a href="../v1.1/results/rk3576/vlm-observation/report.zh-CN.html">打开</a> | <a href="../v1.1/results/rk3576/vlm-observation/report.html">English</a> |

## 说明

- 2026-08-20 刷新统一使用 1080p24 受控本地循环输入、60 秒阶梯和任务本地完成计数。
- VLM FPS 只记录、不启用门禁；这些是短时运行观测，不是正式支持路数或长稳资格结论。
- 结果按本轮已启用门禁记录；完整判定口径见 v1.1 方法说明和 canonical 数据。
- [此前的单设备报告](vlm-77175-npu/report.zh-CN.html)继续保留旧链接，但已由上述带版本号的三平台刷新取代。
