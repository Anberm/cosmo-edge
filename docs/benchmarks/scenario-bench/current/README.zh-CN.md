# ScenarioBench 当前基准刷新报告

本目录指向 v1.0 发布文档冻结后的最新已审阅 benchmark 刷新。Canonical 数据保存在带版本号的 v1.1 报告包中，原始 `metrics.json` 不进入仓库。

## 汇总

| 平台 | 目标 FPS/路 | 80% 保守展示边界 | 中文 | English |
| --- | ---: | --- | --- | --- |
| BM1688 | 0.1 | 6 路 | <a href="../v1.1/results/bm1688/vlm-observation/report.zh-CN.html">打开</a> | <a href="../v1.1/results/bm1688/vlm-observation/report.html">English</a> |
| CV186X | 0.1 | 6 路 | <a href="../v1.1/results/cv186x/vlm-observation/report.zh-CN.html">打开</a> | <a href="../v1.1/results/cv186x/vlm-observation/report.html">English</a> |
| RK3576 | 0.1 | 4 路 | <a href="../v1.1/results/rk3576/vlm-observation/report.zh-CN.html">打开</a> | <a href="../v1.1/results/rk3576/vlm-observation/report.html">English</a> |

## 说明

- 2026-08-20 刷新统一使用 1080p24 受控本地循环输入、60 秒阶梯和任务本地完成计数。
- VLM 原始运行只记录 FPS、未启用 FPS 门禁；表格采用 v1.1 保守回算，不是精确容量或长稳结论。
- 启动敏感窗口不作为性能失败，也不增加展示路数；完整判定口径见 v1.1 方法说明和 canonical 数据。
- [此前的单设备报告](vlm-77175-npu/report.zh-CN.html)只作为已被取代的历史观测保留旧链接。
