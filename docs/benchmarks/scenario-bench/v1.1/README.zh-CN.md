# CosmoEdge 1.1 多平台多路视频分析性能报告

> BM1688、CV186X、RK3576 与 RV1126B 的人员检测、未佩戴安全帽分析和并发混合任务结果。

入口：[中文主报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/report.zh-CN.html) · [English report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/report.html) · [72 小时报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/dual-cv-72h/report.zh-CN.html) · [测试方法](methodology.md) · [canonical 用例 Schema](results/cases.schema.json)

本页链接的 HTML 报告和聚合索引均在文档构建时生成。仓库只保留 canonical 测量数据，不重复提交报告载荷。

## 72 小时双 CV 固定配置长稳观测

四个平台使用同一受控本地循环输入，完成一个连续的 72 小时观测。每路同时运行人员检测与未佩戴安全帽分析，两个业务任务均设为 5 FPS。72 小时终点已经包含完整观测，因此不再把 24、48 小时中间过程单列为结果。这里验证的是表中固定路数，不代表最大容量、RTSP 韧性或生产推荐配置。

| 平台 | 固定路数 | 任务绑定 | 样本 | 最低 / 平均 / 最高 FPS | 最大丢弃率 | CPU / 内存 / 磁盘峰值 | 结果 |
| --- | ---: | ---: | ---: | --- | ---: | --- | --- |
| BM1688 | 8 | 16 | 4316 / 4320 | 4.68 / 5.086 / 5.49 | 0 | 30% / 44% / 96% | PASS |
| CV186X | 8 | 16 | 4316 / 4320 | 4.54 / 5.085 / 5.29 | 0 | 43% / 44% / 96% | PASS |
| RK3576 | 8 | 16 | 4316 / 4320 | 5.00 / 5.098 / 5.17 | 0 | 46% / 30% / 15% | PASS |
| RV1126B | 4 | 8 | 4316 / 4320 | 4.85 / 5.230 / 5.37 | 0 | 41% / 41% / 47% | PASS |

每个平台保留 4320 个理论分钟样本中的 4316 个，覆盖率 99.91%。最大采样间隔为 60.067 秒，低于 180 秒完整性上限；采集错误、不完整或缺失绑定样本、未关闭严重事件和观测到的丢弃均为 0。因此，在既定完整性检查范围内，没有发现运行中途断联证据。

磁盘占用在本次执行中只做观测，没有作为完整性门禁：BM1688 与 CV186X 的 4316 个样本始终为 96%，RK3576 从 14% 变为 15%，RV1126B 从 46% 变为 47%。确定性公开投影使用 99% 阈值；后来增加的 90% 防护只约束未来运行，不追溯改判本次已完成观测。

四个平台在观测开始前定时重启均已关闭，观测期间各完成 80 次状态检查，无失败、无纠正写入，结束时仍为关闭；无需恢复设置，也不形成重启韧性结论。ScenarioBench 源码快照已冻结，但私有控制器文件在长稳进程启动后发生过更新，且启动时未产出控制器摘要，因此不声称已冻结启动时控制器字节。

私有 run manifest、suite state、suite summary，以及逐平台 metrics、summary、report、定时重启守护记录和清理记录，均已在远端只读环境中按 SHA-256 精确核验。公开 canonical 结果是基于完整私有 metrics 与最终状态生成的确定性后处理投影，原始证据仍保持私有。监控记录显示清理完成：自有通道剩余 0、布局已恢复且清理错误为 0；但没有另行产出独立 `final-state` 文件，报告保留这一限制。详见构建生成的[中文 72 小时报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/dual-cv-72h/report.zh-CN.html)与[脱敏 canonical 数据](results/dual-cv-72h.json)。

## 并发混合任务矩阵

每路同时运行两个业务任务、三个模型阶段：人员检测包含一个检测阶段；未佩戴安全帽分析包含检测与分类两个阶段。两个业务任务均设为 5 FPS。

| 平台 | 每路任务组成 | 模型阶段/路 | 目标 FPS/任务 | 通过路数 | 业务任务绑定 |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥16 | 32/32 |
| CV186X | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥8 | 16/16 |
| RK3576 | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥8 | 16/16 |
| RV1126B | 人员检测 + 未佩戴安全帽分析 | 3 | 5 | ≥4 | 8/8 |

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
| RV1126B | 人员检测 | 2 | ≥4 | ≥4 | ≥4 |
| RV1126B | 未佩戴安全帽分析 | 2 | ≥4 | ≥4 | ≥4 |

## 小模型统一测试条件

- 小模型 CosmoEdge 源码：`89c73a7464a81ef378686447d7c1eeb88b988455`，tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`。
- 视频：固定 1920×1080、H.264、24 FPS 本地循环样本，SHA-256 `3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92`。
- 路数逐路增加；每级保持 30 秒；约每 3 秒采样；不加载预览客户端。
- FPS 达标率门禁 80%，遥测缺失率 0，平均丢弃率上限 5%。
- 未佩戴安全帽任务是“检测 + 分类”两级管线，两级节点使用相同目标 FPS。

BM1688 与 CV186X 使用字节一致的检测和分类模型；RK3576、RV1126B 使用相同公开输入输出合同的平台专用 RKNN 产物。模型完整哈希见 [models](models/model-card.md)。

## Canonical 数据与构建生成报告

49 个小模型用例只在 4 份平台级 canonical JSON 中保存一次；72 小时观测与 VLM 观测各有一份额外的脱敏 canonical 文件。每份事实源都保留回溯私有冻结证据的哈希。双语用例页、平台/工作负载/长稳汇总、索引与矩阵都从这些文件生成，不再作为额外证据副本提交。

| 平台 | Canonical 用例 | 构建生成概览 | 构建生成用例页 | 构建生成工作负载报告 | 72 小时双 CV | 最新 VLM |
| --- | --- | --- | --- | --- | --- | --- |
| BM1688 | [JSON](results/bm1688/cases.json) | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/cases/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/single-workload/report.zh-CN.html">单任务</a> · <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/bm1688/vlm-observation/report.zh-CN.html">打开</a> |
| CV186X | [JSON](results/cv186x/cases.json) | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/cases/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/single-workload/report.zh-CN.html">单任务</a> · <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/cv186x/vlm-observation/report.zh-CN.html">打开</a> |
| RK3576 | [JSON](results/rk3576/cases.json) | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/cases/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/single-workload/report.zh-CN.html">单任务</a> · <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/dual-cv-72h/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rk3576/vlm-observation/report.zh-CN.html">打开</a> |
| RV1126B | [JSON](results/rv1126b/cases.json) | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/cases/report.zh-CN.html">打开</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/single-workload/report.zh-CN.html">单任务</a> · <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/concurrent-mixed/report.zh-CN.html">混合任务</a> | <a href="https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.1/results/rv1126b/dual-cv-72h/report.zh-CN.html">打开</a> | — |

## VLM 性能展示边界

VLM 观测已于 2026-08-20 使用相同的 1080p24 受控输入刷新，每级保持 60 秒。原始运行未启用 FPS PASS/FAIL；发布材料统一按“全路最低 FPS 达到每路 0.1 FPS 目标的 80%，且非 FPS 窗口完整”的连续阶梯进行保守回算。

| 平台 | 目标 FPS/路 | 发布参考 | 性能展示边界 | 下一阶梯 |
| --- | ---: | ---: | ---: | --- |
| BM1688 | 0.1 | ≥80% | 6 路 | 7 路全路最低 0.07 FPS（70%） |
| CV186X | 0.1 | ≥80% | 6 路 | 7 路为启动敏感窗口，不纳入性能判定 |
| RK3576 | 0.1 | ≥80% | 4 路 | 5 路全路最低 0.07 FPS（70%） |
| RV1126B | — | — | — | 本轮无 VLM 观测 |

逐路就绪探测属于正式采样前置条件，其探测样本不进入 FPS 统计。CV186X 与 RK3576 的历史运行早于最终逐路就绪协议，因此启动敏感停止既不作为性能失败，也不增加展示路数。上述数值是本次发布的保守性能展示边界，不是精确硬件极限、正式容量或长稳资格结论。统一就绪协议的 VLM 复测仍作为独立后续事项。Canonical 原始测量统一收录在[一份文件](results/vlm-observations.json)中。

VLM 执行源码为 `f0a26546c60c57e70166f18d556f712a273a866d`，tree 为 `a9ebe3921771d8aaa0d29244074e7bfe3d098cf3`；每个平台观测都记录了源 summary、源 metrics 和工具补丁哈希。

manifest 中的精简前完整归档仅覆盖 49 个小模型用例，包含逐用例命令、脱敏日志、summary、metrics 与 HTML。其哈希已经冻结，但归档状态是**已准备、未发布**，也不进入 Git 仓库。刷新后的 VLM 原始运行仍是按平台保存的私有证据，通过上述哈希引用，不属于该归档。

## 复现文件

- [release-manifest.json](release-manifest.json)：源码、工具、视频和平台身份。
- [dual-cv-72h.json](results/dual-cv-72h.json)：脱敏后的 72 小时固定配置观测与私有源证据哈希。
- [methodology.md](methodology.md)：测试步骤与结果判定。
- [scenarios](scenarios/README.md)：脱敏后的公开场景描述。
- <a href="./SHA256SUMS">SHA256SUMS</a>：canonical 仓库源文件哈希；构建生成的公开输出会另行生成完整校验清单。
