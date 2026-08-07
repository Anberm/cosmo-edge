<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**面向视频智能分析的生产级 C++ 边缘 AI 引擎，支持可视化流水线编排与端侧 VLM 工作流**

[![Nightly Sophon Build and Test](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml)

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#c-原生运行时)
[![Platform](https://img.shields.io/badge/platform-Sophon%20BM1688%20%2F%20x86%20Linux%20%2F%20Windows-purple?style=flat-square)](#支持平台)
[![Release](https://img.shields.io/badge/release-v1.0.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Stress Test](https://img.shields.io/badge/stress%20test-200%20video%20samples-brightgreen?style=flat-square)](#验证与性能)
[![Pipelines](https://img.shields.io/badge/pipelines-26%20validated-brightgreen?style=flat-square)](#验证与性能)
[![Website](https://img.shields.io/badge/website-cosmowander.ai-3B82F6?style=flat-square)](https://www.cosmowander.ai/zh/)
[![Docs](https://img.shields.io/badge/docs-online-2563EB?style=flat-square)](https://www.cosmowander.ai/docs/)
[![GitHub](https://img.shields.io/badge/GitHub-cosmo--edge-181717?style=flat-square&logo=github)](https://github.com/cosmo-wander-ai/cosmo-edge)
[![Gitee](https://img.shields.io/badge/Gitee-cosmo--edge-C71D23?style=flat-square&logo=gitee)](https://gitee.com/cosmo-wander-ai/cosmo-edge)

[官网](https://www.cosmowander.ai/zh/) | [在线文档](https://www.cosmowander.ai/docs/) | [快速开始](#快速开始) | [核心特性](#核心特性) | [验证与性能](#验证与性能) | [Benchmark](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/) | [硬件](#cosmoedge-ready-设备)

[English](README.md) | 简体中文

</div>

---

> 本文档与英文 README 保持同步更新。如内容存在差异，请以英文版和最新发布说明为准。

<div align="center">

https://github.com/user-attachments/assets/96eeba7e-5b00-4c54-97b3-3ee4571cd5a0

</div>

*在单台边缘设备上运行多路 AI 流水线、实时 OSD 叠加和事件输出。*

CosmoEdge 是一款面向生产级视频智能分析的 C++ 边缘 AI 引擎。它帮助团队从模型文件走向可运行应用：导入模型、编排流水线、接入视频源、在浏览器中查看 AI 叠加画面，并通过 MQTT 或 HTTP 推送结构化事件。

其 C++17 运行时负责多路视频处理、硬件解码、OSD 渲染和低开销边缘部署，提供在现场部署、监控、调试和维护边缘 AI 应用所需的运行时、Web 控制台和集成路径。

## 你可以构建什么

- 多摄像头安全监测，支持实时 OSD 和告警截图。
- 面向人、车、物的计数、越线和区域入侵分析。
- 端侧 VLM 提示词驱动的视觉巡检，用于质量与合规检查。
- 从文本提示词检测罕见或未列举目标，无需针对性训练（GroundingDINO）。

以上每个应用都以受管边缘部署的形式交付：模型管理、场景任务、告警规则，以及 MQTT/HTTP 数据推送。

## 核心特性

### C++ 原生运行时

CosmoEdge 以 C++17 运行时为核心。对边缘视频系统来说，解码、推理调度、OSD 渲染、事件生成和流输出都需要在资源受限的设备上持续运行。

- 长期运行的多路视频负载开销更低。
- 直接集成硬件解码、NPU 运行时、内存池和流媒体组件。
- 面向设备化边缘部署，CPU、内存和线程行为更可预测。
- x86 开发模式和 Sophon 生产部署使用同一套引擎核心。

### 可视化流水线编排

在浏览器中构建视频 AI 工作流。通过可视化流水线编辑器连接视频源、AI 模型、后处理节点、OSD 渲染、告警规则和输出通道。

<div align="center">

https://github.com/user-attachments/assets/c9673081-ad73-4455-9486-1a3021358cdd

</div>

### 应用工作流

CosmoEdge 将运行时、Web 控制台和集成层连接成一条部署工作流：

```text
模型仓库 -> 场景任务 -> 实时分析 -> 告警管理 -> 数据推送
   |          |          |          |          |
上传/管理   配置流水线  多路 AI +   规则引擎   MQTT / HTTP
ONNX/bmodel 按场景绑定  OSD 叠加    告警截图   webhook
模型版本    摄像头      WebRTC 流   事件日志   集成
```

<details>
<summary><b>完整能力列表</b></summary>

| 模块 | 能力 |
| --- | --- |
| 模型仓库 | 模型上传、元数据管理、版本管理、热替换流程 |
| 场景任务 | 流水线绑定、摄像头绑定、任务调度、场景级配置 |
| 实时分析 | RTSP、视频文件、USB 摄像头、WebRTC 实时预览、HTTP-FLV 兜底 |
| 图片分析 | 批量图片上传、VLM 分析、结构化结果 |
| 告警管理 | 基于规则的告警、严重等级、告警截图、筛选、事件历史 |
| 数据集成 | MQTT 推送、HTTP webhook、结构化 JSON 事件格式 |
| 系统管理 | 仪表盘、设备状态、用户认证、国际化、配置管理 |

</details>

### 实时可视化调试

CosmoEdge 内置 OSD 系统，面向操作人员和开发者：

- 使用业务标签，避免直接展示原始模型类别名。
- 使用语义化颜色区分正常、预警、违规和不确定状态。
- 支持区域叠加、越线指示、计数器和事件面板。
- 调试视图可展示原始检测框、置信度、跟踪 ID 和模型输出。

### 提示词驱动 AI：GroundingDINO + VLM

在边缘设备上运行提示词驱动的视觉模型，作为异步流水线节点与传统 CV 流水线一起使用：

| 能力 | 工作方式 | 典型用途 |
| --- | --- | --- |
| GroundingDINO | 文本提示词 -> 开放词表目标检测 | 无需针对特定类别训练即可查找长尾目标 |
| 端侧 VLM | 封闭式问题 -> YES/NO 状态判断 | “柜门是否打开？” -> YES 时触发告警 |
| VLM 图片分析 | 图片上传 -> 结构化视觉检查 | 质量检测、合规检查 |

<div align="center">

https://github.com/user-attachments/assets/f47b541e-0d01-437d-86e1-4183f6e610fd

</div>

端侧 VLM 节点支持兼容的 Qwen3 VLM 系列和 Qwen3.5 多模态模型。认证设备包可提供 `CosmoEdge-VL-Judge-0.8B`，面向 YES/NO 视觉状态判断优化。

新增：VLM 节点支持两种可互换后端——嵌入式本地运行时（数据不出设备），或任意 OpenAI 兼容端点（自建或 SaaS）以调用更大的模型。VLM 走的是异步、事件驱动路径，可吸收额外的网络往返延迟；本地运行时仍是已验证的默认方案。

### 模型来源

**CosmoEdge 已支持：**

| 类别 | 支持模型/架构 | 流水线支持 |
| :--- | :--- | :--- |
| 目标检测 | YOLOv5、YOLOv8、YOLOv10、YOLOv11、YOLOv12、YOLO26 | 完整流水线 |
| 目标跟踪 | ByteTrack | 完整流水线 |
| 属性分类 | 安全帽、反光背心、工服等分类器 | 完整流水线 |
| 计数与统计 | 越线统计、区域计数、方向流量 | 完整流水线 |
| 开放词表检测 | GroundingDINO | 异步流水线节点 |
| 视觉状态判断 | Qwen3 VLM 系列、Qwen3.5 多模态模型（文本提示词 -> YES/NO） | 异步流水线节点（本地或 OpenAI 兼容 API） |
| 图片分析 | VLM 批量分析 | 独立任务 |

**模型生态兼容性：**

CosmoEdge 使用 ONNX 作为模型交换格式。来自主流 CV 训练框架的模型可以按文档中的转换路径导入：

- **Ultralytics (YOLO)**：使用 `yolo export format=onnx` 导出，然后通过模型仓库导入，或转换为 bmodel 用于 NPU 部署。
- **Roboflow**：在 Roboflow 上训练，导出 ONNX，再导入 CosmoEdge。
- **自定义模型**：任何兼容 ONNX 的检测或分类模型，都可以通过模型适配指南接入。

**Sophon 模型生态：**

CosmoEdge 运行在 Sophon BM1688 推理栈上。SOPHGO 官方模型仓库中的模型可以通过模型适配指南接入，指南覆盖后处理适配和流水线节点注册。

→ [SOPHGO Model Zoo (sophon-demo)](https://github.com/sophgo/sophon-demo)
→ [CosmoEdge Model Porting Guide](docs/tutorials/05-model-porting/model-porting.md)

## 快速开始

### 方案 A：x86 开发模式

无需边缘硬件即可试用 CosmoEdge。x86 开发模式使用与边缘部署相同的 UI 和工作流，但吞吐低于 Sophon NPU 模式。

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
# 或使用 Gitee 镜像（国内推荐）：
# git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
```

> **💡 Docker Compose 版本提示**
> 本文档统一使用最新的 Docker Compose V2 命令格式 (`docker compose`)。如果你使用的是旧版 Docker 环境，请将文中的 `docker compose` 替换为带横杠的 `docker-compose`。

```bash
# 2. 启动 x86 模式
# Linux：
sudo docker compose -f docker-compose.x86.yml up -d --build

# Windows (PowerShell/CMD)：
docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. 打开 Web 控制台
# http://localhost:8080
```

> **USB 摄像头**：如有 USB 摄像头设备，请在启动前取消 `docker-compose.x86.yml` 中 `devices` 段的注释。

启动后，参照 [场景配置教程](docs/tutorials/02-scenario-config/scenario-config.md) 设置你的第一个 AI 检测场景。

### 方案 B：Sophon 目标构建

该路径用于交叉编译和验证 Sophon NPU 加速的 aarch64 运行时。

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
# 或使用 Gitee 镜像（国内推荐）：
# git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. 使用默认 public-runtime 配置构建 Open MD5 升级包
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package

# 3. 查看导出的构建产物
ls -lh build_output/public-runtime/
# 文件名：cosmo-V<版本号>-<32位md5>.tar.gz
```

> 默认 Open 包包含明文模型，不需要授权。Protected 包使用相同的 MD5 升级格式，
> 但包含加密模型和授权工具；加密模型需要设备绑定证书。应用升级包本身不签名。
> 两种包都可以从已安装 main 分支程序的管理页面直接升级。

在 Windows PowerShell 下构建发布包：

```powershell
.\scripts\build_sophon_package.ps1
```

PowerShell 入口使用相同的默认配置，输出到
`build_output/public-runtime/`。配置边界详见[构建指南](docs/guide/build.md)。
Protected 包通过受控 `production-release` 配置构建，但不再需要离线应用签名步骤。

安装 Open 或 Protected 包并重启设备后：
- **默认 IP**：`192.168.100.1`（请确保你的电脑与设备处于同一网段，例如配置静态 IP 为 `192.168.100.x`）
- **登录地址**：`http://192.168.100.1`
- **默认用户名**：`admin`
- **默认密码**：`admin`（首次登录后建议修改）

需要生产硬件时，认证 CosmoEdge 设备可提供预配置 Sophon 加速、生产模型包和部署支持。参见 [CosmoEdge-ready 设备](#cosmoedge-ready-设备)。

初始引导指南
<div align="center">

https://github.com/user-attachments/assets/395e5b89-c6af-4276-a89b-577b3400efc1

测试视频路径：cosmo-edge\data\test-video

</div>

## 验证与性能

CosmoEdge 源自商业化代码库，开源发布前已完成内部系统验证。

一条场景任务（流水线）封装了模型、调度和规则逻辑；部署时再绑定具体输入、区域和规则。这里的 26 计的是已验证流水线——同一套任务随输入和规则变化即可覆盖大量实际部署，无需写代码。

| 范围 | 当前验证状态 |
| --- | --- |
| 视频压力测试 | 使用 200 个视频样本进行连续播放测试，未观察到内存泄漏或崩溃 |
| 流水线验证 | 26 条流水线已按内部场景基线完成验证（涵盖 CV、VLM 与 GroundingDINO） |
| 并发 CV 负载 | ScenarioBench v1.0 已验证 NPU 设备最高 16 路 CV 视频负载，详细报告见下方 |
| 回归测试 | 专职 QA 已完成多轮系统回归，含 v1.0 最终发布回归 |
| 试点部署 | 已授权客户试点覆盖数百路视频分析，连续运行两个月以上，覆盖多个行业场景 |

### 性能基准

下列结果来自可复现的 ScenarioBench 基准测试。CV 与 x86 报告保留在 [ScenarioBench v1.0 基准测试](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/)；VLM 行指向当前刷新后的 VLM 方法报告。完整原始 `metrics.json` 采样数据不随仓库提交。

一个视频通道表示一路已解码输入流。一个场景任务表示绑定到通道上的一条算法流水线，因此混合场景的任务数可以高于视频通道数。表中的路数是本次公开基准测试范围内验证通过的最大稳定视频通道数。

| ScenarioBench 负载 | 硬件档位 | 最大验证视频通道数 | 并发场景任务数 | 目标 FPS | 结果 | 证据 |
| --- | --- | ---: | ---: | ---: | --- | --- |
| 安全帽检测 | YY-16T01-Preview / NPU | 16 | 16 | 3/channel | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.zh-CN.html) |
| 行人检测 | YY-16T01-Preview / NPU | 16 | 16 | 5/channel | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.zh-CN.html) |
| 行人 + 安全帽双算法 | YY-16T01-Preview / NPU | 16 | 32 | 3/channel/task | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.zh-CN.html) |
| VLM 审核 | YY-16T01-Preview / NPU | 8 | 8 | 0.1/channel | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/current/vlm-77175-npu/report.zh-CN.html) |
| 安全帽检测 x86 基线 | X86 CPU baseline | 7 | 7 | 3/channel | 受限；8 路开始超过延迟阈值 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.zh-CN.html) |

硬件档位、模型输入和发布策略见双语 [benchmark manifest](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/manifest.json)、[测试环境说明](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/environment) 与 [当前 benchmark 刷新说明](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/current/)。x86 行是 CPU-only 对照基线，不代表 v1.0 NPU 设备容量目标。

## 架构

```text
+---------------------------------------------------------------+
| Web Frontend                                                  |
| Pipeline Editor | Management Console | Real-time View          |
+-------------------------------+-------------------------------+
                                | REST / WebSocket / MQTT
                                v
+---------------------------------------------------------------+
| C++ Engine Core                                               |
| Flow Engine | Media Pipeline | Inference | Services            |
| Task/Action | Decode/Encode | CV/VLM/DINO | Alarm/Event/Model  |
+-------------------------------+-------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Hardware Abstraction                                          |
| Sophon BM1688 NPU/VPU/VPP | x86 CPU                           |
+---------------------------------------------------------------+
```

### 技术栈

| 层级 | 技术 |
| --- | --- |
| 引擎 | C++17、CMake、FFmpeg、SQLiteCpp |
| 推理 | Sophon BMRT、x86 模式使用 ONNX Runtime |
| 前端 | Vue.js、Vue Flow、Element Plus |
| 流媒体 | SRS 6.0、WebRTC、HTTP-FLV |
| 集成 | REST API、WebSocket、MQTT、HTTP webhook |

## 支持平台

| 平台 | 状态 | 目标用途 |
| --- | :---: | --- |
| Sophon BM1688 | 主力平台 | NPU 加速生产部署 |
| x86 Linux | 已支持 | 开发、评估、集成测试 |
| x86 Windows | 已支持 | 开发和评估 |
| Sophon BM1684X | 规划中 | NPU 加速部署 |

## CosmoEdge-ready 设备

CosmoEdge 是开源项目。仓库提供认证设备包所使用的同一套引擎、Web UI 和工作流。你可以使用自带模型，在 x86 上开发，并部署到兼容的边缘硬件。

认证设备帮助团队减少硬件适配和模型打包工作，提供预配置 NPU 加速、生产模型包和专属部署支持。

| 能力 | 开源仓库 | 认证设备包 |
| --- | :---: | :---: |
| C++ 引擎 | 包含 | 包含 |
| 可视化流水线编排器 | 包含 | 包含 |
| Web 管理控制台 | 包含 | 包含 |
| x86 开发模式 | 包含 | 包含 |
| Sophon NPU 运行时支持 | 源码支持，需自备硬件 | 预配置 |
| CV 模型包 | 自带模型 | 预安装（约 25 个生产 CV 模型） |
| `CosmoEdge-VL-Judge-0.8B` | 自带或定制模型包，需自行验证 | 预安装认证模型包 |
| GroundingDINO 包 | 自带或定制模型包 | 预安装 |
| 部署支持 | 社区支持 | 专属支持 |

认证设备提升部署就绪度，不把软件能力锁定在特定硬件上。

开源引擎和全部软件功能现已面向全球开放，无需购买。CosmoEdge-ready 认证设备已在中国大陆上架，可通过[淘宝购买认证设备](https://item.taobao.com/item.htm?id=1066672051450)。如需咨询中国大陆以外地区的可用性或项目部署支持，请联系 hello@cosmowander.ai。

## 文档

| 入口 | 读者 | 说明 |
| --- | --- | --- |
| [文档首页](docs/index.md) | 所有用户 | 完整文档索引和阅读路径 |
| [快速开始](docs/tutorials/01-quickstart/quickstart.md) | 所有用户 | 完成首次启动和场景体验 |
| [场景配置](docs/tutorials/02-scenario-config/scenario-config.md) | 集成商 | 构建场景级 AI 工作流 |
| [VLM 指南](docs/tutorials/03-vlm-guide/vlm-guide.md) | 开发者 | 使用提示词完成视觉状态判断 |
| [流水线编排](docs/tutorials/04-pipeline-orchestration/pipeline-orchestration.md) | 高级用户 | 可视化编排自定义流水线 |
| [模型适配指南](docs/tutorials/05-model-porting/model-porting.md) | 算法工程师 | 导入 ONNX 或目标运行时模型 |
| [使用智能体完成二次开发](docs/development/agent-assisted-development.md) | 二次开发者 | 用你熟悉的编码智能体完成模型转换等二开任务，交付可验证 |
| [构建指南](docs/guide/build.md) | 开发者 | 构建 x86 Docker 和 Sophon 发布包 |
| [API 概览](docs/reference/api.md) | 开发者 | REST、WebSocket 和 MQTT API 类别 |

## 发布状态与路线图

CosmoEdge `v1.0.0` 是首个稳定公开发布版本。引擎已可用于评估、集成和社区模型拓展。

### v1.0.0 已交付

- [X] C++17 边缘推理引擎
- [X] 可视化流水线编排器
- [X] Web 管理控制台
- [X] x86 Linux 和 Windows 开发模式
- [X] Sophon BM1688 发布打包
- [X] VLM 与 GroundingDINO 集成
- [X] 26 条流水线场景完成内部验证
- [X] v1.0 最终回归测试与加固（shell 注入防护、空指针与生命周期修复、默认密码强制修改）

### 路线图

- [ ] 扩充已验证流水线场景库
- [ ] 社区模型和场景示例
- [ ] 更多模型适配器与后处理模板

## 贡献

CosmoEdge 已发布 v1.0.0，欢迎围绕以下方向进行聚焦贡献：

- 带日志和复现步骤的 bug 报告。
- 文档修正和教程改进。
- 场景示例和集成说明。
- 先通过 issue 讨论，再提交范围清晰的小型 PR。

提交 pull request 前，请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。首次贡献者也可以从[贡献者上手路径](docs/development/contributing.md)开始。

## FAQ

<details>
<summary><b>我需要 Sophon 设备才能体验 CosmoEdge 吗？</b></summary>

不需要。你可以在 Linux 或 Windows 上使用 x86 开发模式体验 UI、流水线工作流、模型管理和集成路径。Sophon 硬件主要用于生产级 NPU 吞吐。

</details>

<details>
<summary><b>CosmoEdge 支持多少种场景或算法？</b></summary>

CosmoEdge 有两个核心概念：模型（AI 权重）和场景任务（又称流水线，是模型、调度与规则逻辑编排成的图，部署时绑定具体输入、区域和规则）。26 条已验证流水线，每条都能随输入和规则变化覆盖大量实际部署。能力随编排组合扩展，而非取决于固定的算法目录。

</details>

<details>
<summary><b>开源仓库包含模型权重吗？</b></summary>

开源仓库不默认包含生产模型权重。你可以自带模型，包括兼容的 Qwen3 VLM 系列和 Qwen3.5 多模态模型；认证设备包可提供约 25 个预安装的生产 CV 模型，外加 `CosmoEdge-VL-Judge-0.8B` 和 GroundingDINO。社区或自定义模型建议按目标场景验证效果。

</details>

<details>
<summary><b>可以使用自己训练的模型吗？</b></summary>

可以。CosmoEdge 围绕模型导入和模型生命周期管理设计。模型适配指南会说明从 ONNX 或目标运行时格式进入模型仓库的推荐路径。

</details>

<details>
<summary><b>CosmoEdge 和推理服务器或 NVR 项目有什么区别？</b></summary>

CosmoEdge 是面向完整边缘 AI 工作流的应用运行时，不是单纯的模型服务层或视频录像系统。

</details>

<details>
<summary><b>CosmoEdge 是否已经生产可用？</b></summary>

代码库源自面向生产部署的商业化开发，并通过了内部压力、流水线和回归验证，含 v1.0 最终发布回归。CosmoEdge `v1.0.0` 是首个稳定公开发布版本：引擎、核心工作流和发布打包按现场交付需求设计，公共 API 和贡献者流程已稳定。

</details>

## 联系方式

- Community: [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions)
- 国内社区: [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues)
- Partnership & Enterprise: hello@cosmowander.ai
- 安全报告：请参见 [SECURITY.md](SECURITY.md)，按私密披露流程上报安全漏洞

## License

CosmoEdge 使用 [Apache License 2.0](LICENSE) 开源许可。

```text
Copyright 2026 CosmoEdge Contributors

Licensed under the Apache License, Version 2.0
```

---

<div align="center">

An open-source project by Cosmo Wander AI and the CosmoEdge contributors.

Turn video AI models into deployable edge applications.

📦 本项目在 [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) 维护只读镜像，代码自动从 GitHub 同步。详见 [MIRRORING.md](MIRRORING.md)。

</div>
