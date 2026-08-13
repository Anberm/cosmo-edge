# CosmoEdge 1.1 多平台多路视频分析性能报告

> BM1688、CV186X 与 RK3576 上的人员检测、安全帽检测和多任务并发测试

入口：[中文主报告](report.zh-CN.html) · [English report](report.html) · [测试方法](methodology.md) · [机器可读结果索引](results/index.json)

## 摘要

> **状态：最终公开性能材料。当前仅完成仓库与 PR 收口，尚未对外发布。**

我们在三类边缘 AI 平台上验证了 CosmoEdge 1.1 的多路视频分析能力。在本报告指定的模型、视频、设备和运行时条件下，BM1688 短时阶梯测试完成了 16 路双算法 × 5 FPS，CV186X 与 RK3576 完成了 8 路双算法 × 5 FPS。结果代表指定配置下的实测工作负载，不代表芯片理论峰值。

## 推荐配置与实测边界

| 平台 | 公开推荐配置 | 短时实测边界 | 单级稳态时长 | 状态 |
| --- | --- | --- | ---: | --- |
| BM1688 reference device | 待重复测试和长稳确认 | 16 路双算法 × 5 FPS | 15 秒 | Preliminary |
| CV186X reference device | 待重复测试和长稳确认 | 8 路双算法 × 5 FPS | 30 秒 | Preliminary |
| RK3576 EVB | 待重复测试和长稳确认 | 8 路双算法 × 5 FPS | 15 秒 | Preliminary |

这里的“实测边界”是当前测试覆盖到且通过门禁的最高路数，不等同于官方推荐配置，也不表示已经探测到芯片的绝对上限。

## 公开工作负载

- 每路同时运行人员检测和安全帽检测两个任务。
- 每个任务的目标分析帧率均为 5 FPS。
- 输入为固定的 1080p、24 FPS 本地视频样本。
- 路数从 1 路开始逐路增加。
- 通过门禁：每个任务的最低 FPS 达标率不低于 80%，采样缺失率为 0，平均丢弃率不高于 5%。
- 本轮未加载预览客户端，结果代表后台视频分析负载。

## 最后通过点

| 平台 | 路数 | 人员检测最低 FPS | 安全帽检测最低 FPS | 平均丢弃率 | 加速器峰值 | CPU 峰值 | 内存峰值 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| BM1688 reference device | 16 | 4.92 | 4.90 | 0% | 60% | 61% | 46% |
| CV186X reference device | 8 | 5.00 | 5.00 | 0% | 60% | 19% | 42% |
| RK3576 EVB | 8 | 5.15 | 5.11 | 0% | 41% | 47% | 30% |

## 单算法容量矩阵

以下数字表示各短时阶梯的最后通过路数。“≥”表示最高配置点仍通过；“*”表示下一路被任务绑定错误阻断，不是实测性能上限。

| 平台 | 单算法任务 | 24 FPS | 10 FPS | 7 FPS | 5 FPS | 状态 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| BM1688 | 人员检测 | 7 | 15 | ≥16 | ≥16 | Preliminary |
| BM1688 | 安全帽检测 | 7 | ≥12* | ≥16 | ≥16 | Preliminary |
| CV186X | 人员检测 | ≥3* | ≥8* | ≥11* | ≥15* | Preliminary |
| CV186X | 安全帽检测 | ≥3* | ≥8* | ≥11* | ≥15* | Preliminary |
| RK3576 | 人员检测 | 6 | 12 | 15 | ≥16 | Preliminary |
| RK3576 | 安全帽检测 | 5 | 10 | 12 | 15 | Preliminary |

## 可比性声明

三平台使用了各自转换和验证的目标模型，当前内部算法编号不构成公开模型身份，也不能据此制作芯片性能排行榜。除非模型来源、模型版本、输入尺寸、量化精度、前后处理、视频、编解码配置、预热、稳态时长和重复次数全部统一，本报告只描述“各平台典型工作负载能力”，不作跨芯片性能排名。

## VLM 实验结果

VLM 结果不进入本报告主容量结论。三平台均使用每级 120 秒的 1→8 路阶梯，目标为每路 0.1 FPS；分析 FPS 仅记录、不参与 PASS/FAIL。表中通过仅指丢弃率、遥测完整性及系统保护等非 FPS 门禁。

| 平台 | 非 FPS 门禁最后通过路数 | 该点等效单路 FPS | 首个停止点 | 状态 |
| --- | ---: | ---: | --- | --- |
| BM1688 | ≥8 | 0.040 | 最高配置点完成观测 | Experimental |
| CV186X | ≥8 | 0.080 | 最高配置点完成观测 | Experimental |
| RK3576 | 7 | 0.063 | 第 8 路平均丢弃率 22.75% | Experimental |

BM1688、CV186X 使用设备提供的单路观测；RK3576 原始遥测采用共享任务计数器，公开附件固定使用复核后的 1→8 路等效单路序列 `0.100 / 0.120 / 0.116 / 0.115 / 0.091 / 0.076 / 0.063 / 0.057`。在重新启用目标 FPS、完成次数、缺失率和延时门禁之前，VLM 不进入正式容量表。

## 测试环境

CosmoEdge 1.1 源码基线冻结为 `feat/model-guard-v2.3` 的 commit `209bc2b52849864a15bdad91beb61f5bc982c17f`、tree `f64a98bce05b9ee8dc64dda8e56ad50f9d15687f`。它包含本轮采用的 RK3576 VLM 推理路径与性能优化；冻结点新增变化只涉及 Web 联动、升级缓存行为与纯格式化，未改动推理、媒体或内存生命周期语义。

BM1688 与 CV186X 的实测设备使用同一 Open 安装包，SHA-256 为 `8aee0bdb146d80647b4f517114c2920781ed6760e90e5bdf951fefd982dbecb2`。包内 `cosmo-engine` SHA-256 与两台设备运行引擎均为 `bc7274327896384bcf68abf7fc42ce9e133f15131f3be21cb265b8e4deb55d11`。该安装包不嵌入 source commit，且早于最终源码冻结点；因此这是明确的设备/包绑定，不宣称为最终源码的可复现构建。RK3576 原始安装包未回收，相关结果按设备版本、模型身份、环境与测试证据绑定，不能视为 package-qualified 结果。

| 平台 | 公开设备 | OS | Runtime / Media | 内存与存储 |
| --- | --- | --- | --- | --- |
| BM1688 | BM1688 reference device | Ubuntu 22.04.5 LTS | libsophon/BMRT 0.4.12；Sophon FFmpeg/GStreamer 2.0.0 | 系统 2,160,271,360 B；加速器 heap 1,536 + 4,096 MiB；system/data 文件系统 9,260,003,328 / 49,366,970,368 B |
| CV186X | CV186X reference device | Ubuntu 22.04.5 LTS | libsophon/BMRT 0.4.12；Sophon FFmpeg/GStreamer 2.0.0 | 系统 2,160,451,584 B；加速器 heap 1,536 + 4,096 MiB；system/data 文件系统 9,260,003,328 / 49,375,051,776 B |
| RK3576 | Rockchip RK3576 EVB1 V10 | Linux 发行版未由只读接口暴露 | RKNN/Driver/RGA/MPP 精确版本未由只读接口暴露；已确认 Rockchip MPP/RGA 媒体路径 | 7,917 MiB 共享系统内存；设备 API 报告存储已用 11.56 GB、可用 2.13 GB |

模型身份、输入输出合同、仓库路径与可用 SHA-256 见 `models/`。与压测字节一致的 BM1688、CV186X 检测和分类模型已放入 `data/resource/aiboxresource/models/`，默认 Sophon Open 构建会随包安装。视频 SHA-256 为 `ec77182a264f3059a091b68c4973942dba3b80e93f20feaf4d7e146885caf9d2`；ScenarioBench 版本及关键文件哈希见 `release-manifest.json`。没有证据的 RKNN/Driver/RGA/MPP 版本和 RK VLM 文件 SHA-256 均保持为空，不使用推测值。

## 限制说明

- 数据仅适用于报告绑定的模型、视频、设备、运行时和安装包。
- 当前多任务容量阶梯属于短时测试；在重复测试和长稳通过前，不作为官方推荐路数。
- 结果不是芯片理论算力。
- 不同模型或转换产物之间不能直接横向比较。
- 未启用门禁的指标不计为性能通过。
- 任务绑定失败表示测试被阻断，不表示性能上限。
- 环境、模型、媒体链路或安装包变化后必须重新验证。

## 复现

公开复现包提供方法说明、脱敏场景、机器可读结果、环境模板和文件哈希。原始设备序列号、内部通道 ID、内部算法 ID、本地绝对路径、客户素材和完整调试日志仅保存在内部证据包中。

本发布材料已经生成单算法、双算法与 VLM 的独立 `summary.json`、`metrics.json`、`command.txt`、脱敏日志和 HTML。执行前需按 `methodology.md` 将公开模型引用解析为设备本地编号；公开包不会携带设备地址、凭据或内部编号。

| 平台 | 单算法逐路报告 | 双算法逐路报告 | VLM 观测报告 | 机器可读汇总 |
| --- | --- | --- | --- | --- |
| BM1688 | [打开](results/bm1688/single-detector/report.html) | [打开](results/bm1688/dual-detector/report.html) | [打开](results/bm1688/vlm-observation/report.html) | [summary.json](results/bm1688/summary.json) |
| CV186X | [打开](results/cv186x/single-detector/report.html) | [打开](results/cv186x/dual-detector/report.html) | [打开](results/cv186x/vlm-observation/report.html) | [summary.json](results/cv186x/summary.json) |
| RK3576 | [打开](results/rk3576/single-detector/report.html) | [打开](results/rk3576/dual-detector/report.html) | [打开](results/rk3576/vlm-observation/report.html) | [summary.json](results/rk3576/summary.json) |

## 产品发版证据边界

- Protected 最终安装包 SHA-256 与受控构建来源；
- RK3576 最终安装包 SHA-256 与源码来源；
- RK3576 VLM 最终模型文件 SHA-256；
- 正式推荐配置所需的重复测试、长稳、客户旅程与精度资格材料。

这些项目不阻止公开本性能报告，但阻止把短时边界包装成官方推荐配置，也意味着本报告不能代替完整产品资格报告。BM1688、CV186X 的开源检测和分类模型只在仓库资源树中分发一份；样例视频和其他模型二进制不随本 benchmark 分发。已记录的 SHA-256 用于确认精确产物身份。
