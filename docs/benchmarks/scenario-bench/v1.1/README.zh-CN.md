# CosmoEdge 1.1 多平台多路视频分析性能报告

> BM1688、CV186X 与 RK3576 的人员检测、未佩戴安全帽分析和双任务并发结果；RV1126B 作为附加实验平台列出。

入口：[中文主报告](report.zh-CN.html) · [English report](report.html) · [测试方法](methodology.md) · [机器可读索引](results/index.json)

## 双任务结果

每路同时运行人员检测与未佩戴安全帽分析，两个任务均为 5 FPS。

| 平台 | 通过路数 | 任务绑定 | 单级时长 |
| --- | ---: | ---: | ---: |
| BM1688 | ≥16 | 32/32 | 30 秒 |
| CV186X | ≥8 | 16/16 | 30 秒 |
| RK3576 | ≥8 | 16/16 | 30 秒 |
| RV1126B（附加实验平台） | ≥4 | 8/8 | 30 秒 |

## 单任务容量矩阵

数字表示最后通过路数；`≥` 表示测试设定的最高路数通过。`*` 表示增加下一路时任务绑定被阻断，已通过路数仍保留。`†` 表示扩容轮在开始测量前被存储条件阻断。

| 平台 | 任务 | 24 FPS | 10 FPS | 7 FPS | 5 FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | 人员检测 | ≥8 | ≥16 | ≥16 | ≥16 |
| BM1688 | 未佩戴安全帽分析 | ≥7* | ≥14* | ≥16 | ≥16 |
| CV186X | 人员检测 | ≥8* | ≥15* | ≥16 | ≥16 |
| CV186X | 未佩戴安全帽分析 | 6 | ≥13* | ≥16 | ≥16 |
| RK3576 | 人员检测 | 6 | 12 | ≥16 | ≥8† |
| RK3576 | 未佩戴安全帽分析 | 6 | 10 | 12 | ≥16 |

### RV1126B 附加结果

| 任务 | 24 FPS | 10 FPS | 7 FPS | 5 FPS |
| --- | ---: | ---: | ---: | ---: |
| 人员检测 | 2 | ≥4 | ≥4 | ≥4 |
| 未佩戴安全帽分析 | 2 | ≥4 | ≥4 | ≥4 |

## 统一测试条件

- CosmoEdge 源码：`89c73a7464a81ef378686447d7c1eeb88b988455`，tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`。
- 视频：固定 1920×1080、H.264、24 FPS 本地循环样本，SHA-256 `3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92`。
- 路数逐路增加；每级保持 30 秒；约每 3 秒采样；不加载预览客户端。
- FPS 达标率门禁 80%，遥测缺失率 0，平均丢弃率上限 5%。
- 未佩戴安全帽任务是“检测 + 分类”两级管线，两级节点使用相同目标 FPS。

BM1688 与 CV186X 使用字节一致的检测和分类模型；RK3576、RV1126B 使用相同公开输入输出合同的平台专用 RKNN 产物。模型完整哈希见 [models](models/model-card.md)。

## 独立报告

本轮共生成 49 个小模型用例附件。每个用例均包含中英文 HTML、`summary.json`、`metrics.json`、`command.txt` 和脱敏 `test.log`。

| 平台 | 平台汇总 | 全部独立用例 | 单任务汇总 | 双任务汇总 | 既有 VLM |
| --- | --- | --- | --- | --- | --- |
| BM1688 | <a href="./results/bm1688/report.zh-CN.html">打开</a> | <a href="./results/bm1688/cases/report.zh-CN.html">打开</a> | <a href="./results/bm1688/single-detector/report.zh-CN.html">打开</a> | <a href="./results/bm1688/dual-detector/report.zh-CN.html">打开</a> | <a href="./results/bm1688/vlm-observation/report.zh-CN.html">打开</a> |
| CV186X | <a href="./results/cv186x/report.zh-CN.html">打开</a> | <a href="./results/cv186x/cases/report.zh-CN.html">打开</a> | <a href="./results/cv186x/single-detector/report.zh-CN.html">打开</a> | <a href="./results/cv186x/dual-detector/report.zh-CN.html">打开</a> | <a href="./results/cv186x/vlm-observation/report.zh-CN.html">打开</a> |
| RK3576 | <a href="./results/rk3576/report.zh-CN.html">打开</a> | <a href="./results/rk3576/cases/report.zh-CN.html">打开</a> | <a href="./results/rk3576/single-detector/report.zh-CN.html">打开</a> | <a href="./results/rk3576/dual-detector/report.zh-CN.html">打开</a> | <a href="./results/rk3576/vlm-observation/report.zh-CN.html">打开</a> |
| RV1126B | <a href="./results/rv1126b/report.zh-CN.html">打开</a> | <a href="./results/rv1126b/cases/report.zh-CN.html">打开</a> | <a href="./results/rv1126b/single-detector/report.zh-CN.html">打开</a> | <a href="./results/rv1126b/dual-detector/report.zh-CN.html">打开</a> | — |

本次刷新只更新小模型结果；三款发布平台原有 VLM 附件保持不变。

## 复现文件

- [release-manifest.json](release-manifest.json)：源码、工具、视频和平台身份。
- [methodology.md](methodology.md)：测试步骤与结果判定。
- [scenarios](scenarios/README.md)：脱敏后的公开场景描述。
- <a href="./SHA256SUMS">SHA256SUMS</a>：公开包文件哈希。
