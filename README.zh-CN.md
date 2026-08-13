<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**把视频 AI 模型变成可部署的边缘应用——面向 Sophon、Rockchip 与 x86 的 C++ 边缘 AI 引擎。**

使用一致的可视化编排和设备管理体验构建视频分析、VLM 与事件工作流；不同平台使用各自的运行时、构建产物和模型包。

[![Nightly Sophon Build and Test](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml)
[![Nightly RK3576 Cross Build](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/ci-build-rk3576.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/ci-build-rk3576.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#核心能力)
[![Release](https://img.shields.io/badge/release-v1.1.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Website](https://img.shields.io/badge/website-cosmowander.ai-3B82F6?style=flat-square)](https://www.cosmowander.ai/)
[![Docs](https://img.shields.io/badge/docs-online-2563EB?style=flat-square)](https://www.cosmowander.ai/zh/docs/)
[![Gitee](https://img.shields.io/badge/Gitee-cosmo--edge-C71D23?style=flat-square&logo=gitee)](https://gitee.com/cosmo-wander-ai/cosmo-edge)

[快速开始](#快速开始) · [平台选择](#选择平台) · [验证](#验证与性能) · [文档](#文档设备与社区) · [English](README.md)

</div>

---

<div align="center">

<https://github.com/user-attachments/assets/96eeba7e-5b00-4c54-97b3-3ee4571cd5a0>

</div>

CosmoEdge 不只是模型推理服务：它提供从模型导入、可视化编排到告警与事件推送的完整应用层。仓库中的核心引擎与控制台以 Apache-2.0 开源；认证硬件、商业预置模型与 Model Guard 分发保护具有独立边界。

## CosmoEdge 1.1

> 当前分支是 CosmoEdge 1.1 源码线。不同模型和负载的容量不同，请以关联证据为准，不把短时最高点直接当作部署推荐值。

- **多平台发布：**BM1688、CV186X 与 RK3576 共享同一套视频接入、任务编排、事件和可观测流程，并分别使用目标平台模型产物。
- **公开 benchmark 包：**[CosmoEdge 1.1 多平台报告](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)覆盖单算法、双算法与 Experimental VLM，提供脱敏后的可复现附件。
- **Rockchip RK3576：**已集成交叉构建、板端运行、RKNN 推理和 MPP/RGA 媒体路径；4 路单算法已有 12 小时证据，更高短时结果只作为实测边界，不直接作为推荐配置。
- **Sophon 模型处理：**芯片感知校验支持 BM1688 与 CV186X 的目标 `.nn` 产物；benchmark 已记录两台参考设备的 Open 安装包和运行引擎精确绑定。
- **RKNN 数据路径：**包含 DMA-BUF 到 RGA 输入、持久绑定输入、原生量化输出和 YOLOv8 张量直接解码路径，并保留明确 fallback。
- **智能体辅助二开：**提供仓库级入口，把模型适配、系统集成和界面改造任务交给常用编码智能体，并获得可核验交付物。
- **Model Guard 2.3：**为 Sophon Protected 包中的商业预置模型提供分发保护；Open 与 Protected 的应用软件能力一致，不以 SKU 解锁软件功能，区别在于模型是否加密以及是否包含设备授权工具。
- **macOS Docker Preview：**为 Apple Silicon 提供隔离的 `linux/amd64` 单路本地视频体验候选；发布前仍需完成连续两轮端到端验收。它不启用 Model Guard，也不代表 NPU 或原生性能。

## 选择平台

CosmoEdge 提供统一的引擎架构与编排体验，但每次构建只选择一个推理后端，并使用面向目标平台生成的模型产物。

| 平台 | 状态 | 运行时 / 模型产物 | 当前范围与证据 |
| --- | --- | --- | --- |
| Sophon BM1688 | v1.1 已支持 / 主力平台 | BMRT / `.nn` | 生产部署路径，包含 [v1.1 工作负载证据](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)和已发布 [v1.0 基线](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/) |
| Rockchip RK3576 | v1.1 已支持 | RKNN / `.rknn` | 交叉构建和板端路径已验证；参见 [RK3576 集成指南](docs/guide/rk3576-rknn-development.md)与 [v1.1 工作负载证据](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md) |
| Sophon CV186X | v1.1 已支持 | BMRT / 目标芯片专用 `.nn` | 模型导入与设备工作负载证据见 [v1.1 benchmark](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md) |
| x86 Linux / Windows；Apple Silicon macOS | Linux / Windows 已支持；macOS Preview | ONNX Runtime / `.onnx` | Mac 通过 amd64 仿真覆盖单路本地视频开发体验，不代表原生性能 |
| Sophon BM1684X | 规划中 | — | 不属于当前发布范围 |

## 快速开始

### 在 x86 本地试用

无需边缘硬件即可体验。x86 模式使用与边缘部署一致的 UI 和工作流，但吞吐低于 NPU 部署。

```bash
# 1. 克隆
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. 在 Linux 启动
sudo docker compose -f docker-compose.x86.yml up -d --build
# Windows：docker compose -f docker-compose.x86.windows.yml up -d --build
# Apple Silicon macOS（Preview）：./scripts/macos-docker-preview.sh up

# 3. 打开 http://localhost:8080
```

启动后，按照[场景配置教程](https://www.cosmowander.ai/zh/docs/tutorials/02-scenario-config/scenario-config)创建第一个 AI 检测任务。Mac 用户先阅读 [macOS Docker Preview](docs/guide/macos-docker-preview.md) 的许可、环境和能力边界。使用 Docker Compose V1 时，可将 `docker compose` 替换为 `docker-compose`。

### 为 Sophon 构建

```bash
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
# BM1688（省略芯片型号时的默认值）
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip bm1688

# CV186X
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
ls -lh build_output/public-runtime/
```

构建脚本会根据芯片型号选择对应模型资源，无需填写模型路径。芯片型号只影响资源选择，
输出目录和安装包命名规则保持不变。

默认 Open 包包含明文模型，不需要设备授权。Protected 包使用同一种 MD5 升级格式，但包含加密预置模型和授权工具；应用升级包本身不签名。安装到设备前请阅读[构建指南](https://www.cosmowander.ai/zh/docs/guide/build)和[部署指南](https://www.cosmowander.ai/zh/docs/guide/deployment)。

### 为 RK3576 构建

```bash
docker compose -f docker-compose.rk3576.yml run --rm cosmo-rk3576-package
ls -lh build_output/rk3576/
```

公开构建镜像已固定 digest，并包含 RKNN、MPP 和 RGA 构建依赖。目标运行时与
设备验证边界请参阅 [RK3576 集成指南](docs/guide/rk3576-rknn-development.md)。

CV186X 请按照 [CV186X 快速开始](docs/guide/cv186x-quick-start.md)完成安装、模型导入、首个事件验证，以及升级和恢复检查。

## 你可以构建什么

- **实时视频分析：**检测、分类、跟踪、区域规则、计数、OSD 和告警截图。
- **提示词驱动视觉：**让 VLM 状态判断和 GroundingDINO 开放词汇检测与传统 CV 流水线协同工作。
- **可视化应用工作流：**在浏览器中连接模型、规则、事件和输出动作。
- **边缘系统集成：**管理场景任务，并通过 REST、WebSocket、MQTT 或 HTTP webhook 输出结构化事件。

## 核心能力

| 能力 | 能力范围 | 深入了解 |
| --- | --- | --- |
| 原生运行时 | 面向多路媒体、推理调度、OSD、任务和事件的 C++17 引擎 | [架构](https://www.cosmowander.ai/zh/docs/guide/architecture) |
| 可视化编排 | 浏览器端流水线组合、任务绑定、参数校验和实时反馈 | [流水线教程](https://www.cosmowander.ai/zh/docs/tutorials/04-pipeline-orchestration/pipeline-orchestration) |
| 推理与媒体 | Sophon、RKNN 和 x86 平台后端；平台专用构建与模型产物 | [构建指南](https://www.cosmowander.ai/zh/docs/guide/build) |
| VLM 与 DINO | 提示词视觉判断、开放词汇检测，以及检测告警上报前可选的 VLM 复核 | [VLM 指南](https://www.cosmowander.ai/zh/docs/tutorials/03-vlm-guide/vlm-guide) |
| 运维与集成 | 模型管理、告警、事件历史、REST、WebSocket、MQTT 和 webhook | [API 概览](https://www.cosmowander.ai/zh/docs/reference/api) |
| 模型接入与保护 | 模型转换、导入、验证，以及 Open/Protected 分发边界 | [模型适配指南](https://www.cosmowander.ai/zh/docs/tutorials/05-model-porting/model-porting) |

<details>
<summary>▶ 观看演示：可视化编排一条完整 pipeline</summary>

<https://github.com/user-attachments/assets/c9673081-ad73-4455-9486-1a3021358cdd>

</details>

<details>
<summary>▶ 观看演示：GroundingDINO 与 VLM 视觉工作流</summary>

<https://github.com/user-attachments/assets/f47b541e-0d01-437d-86e1-4183f6e610fd>

</details>

## 智能体辅助二次开发

已经有模型适配、系统集成或界面改造任务？把业务目标、已有物料、目标设备或测试环境和验收要求交给常用编码智能体。仓库提供任务入口、示例、检查和证据边界，使结果可以包含可导入产物、范围明确的代码改动和可核验结论。

从[智能体辅助二次开发](docs/development/agent-assisted-development.md)开始，再根据具体任务进入[模型适配指南](https://www.cosmowander.ai/zh/docs/tutorials/05-model-porting/model-porting)或[贡献者指南](docs/development/contributing.md)。

## 验证与性能

### CosmoEdge 1.1 多平台性能报告

已收口的 v1.1 公开材料覆盖 BM1688、CV186X 与 RK3576，包括精简主报告及关联的单算法、双算法、Experimental VLM 附件。除非明确标注更长时长，当前结果属于短时工作负载证据，不代表芯片理论上限，也不会自动成为部署推荐配置。

- [中文 benchmark 索引](docs/benchmarks/scenario-bench/v1.1/README.zh-CN.md)
- [English benchmark index](docs/benchmarks/scenario-bench/v1.1/README.md)
- [中文主报告（官网渲染版）](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/report.zh-CN.html)
- [方法与复现](docs/benchmarks/scenario-bench/v1.1/methodology.md)

发布分支准备基线为 `feat/model-guard-v2.3` commit `209bc2b52849864a15bdad91beb61f5bc982c17f`、tree `f64a98bce05b9ee8dc64dda8e56ad50f9d15687f`。BM1688 与 CV186X 已绑定到记录的 Open 安装包和运行引擎；该安装包不嵌入 source commit，因此不宣称它可从这一准备基线复现构建。RK3576 包哈希与 Protected 包构建来源不在公开 benchmark 中，因此报告不对它们作 package-qualified 声明。精确证据边界见 [release manifest](docs/benchmarks/scenario-bench/v1.1/release-manifest.json)。

下表保留此前已经公开的 **v1.0 历史基线**：

| ScenarioBench 负载 | 硬件 | 最大验证路数 | 目标 FPS | 结果 | 证据 |
| --- | --- | ---: | ---: | --- | --- |
| 安全帽检测 | YY-16T01-Preview / NPU | 16 | 3/channel | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.zh-CN.html) |
| 行人检测 | YY-16T01-Preview / NPU | 16 | 5/channel | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.zh-CN.html) |
| 行人 + 安全帽双算法 | YY-16T01-Preview / NPU | 16 | 3/channel/task | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.zh-CN.html) |
| VLM 复核 | YY-16T01-Preview / NPU | 8 | 0.1/channel | 通过 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/vlm-55009-npu/report.zh-CN.html) |
| 安全帽检测 x86 基线 | x86 CPU | 7 | 3/channel | 受限；8 路超过延迟阈值 | [报告](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.zh-CN.html) |

v1.0 的测试方法和发布边界见 [benchmark manifest](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/manifest.json)、[环境说明](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/v1.0/environment)和[当前刷新说明](https://www.cosmowander.ai/zh/docs/benchmarks/scenario-bench/current/)。

### RK3576 长稳验证背景

这组较早的长稳证据仍绑定源码 [`8f8b4b8e`](https://github.com/cosmo-wander-ai/cosmo-edge/commit/8f8b4b8e793172963ef92da7fc9942a1c860534b)（tree `fd1b646f`）、engine SHA-256 `bc829d9513334c4520fad1b58439bb3e6e31338c664e93eb15babdaaa564d886`、RKNN Runtime `2.3.2-429f97ae6b`、driver `0.9.8`、RGA `1.10.1_[4]` 和 MPP `1.5.0-1`。

| ScenarioBench 负载 | 硬件 | 最大验证路数 | 目标 FPS | 结果 |
| --- | --- | ---: | ---: | --- |
| 安全帽检测 | Rockchip RK3576 EVB1 V10 / RKNN | 8 | 5/路 | 通过 |

1/2/4/8 路阶梯全部通过，丢帧及推理、RGA、MPP 失败均为 0；单路 Detect 平均耗时由 142.3 ms 降至 58.2 ms（下降 59.1%）。另一次 4 路 × 5 FPS、4 路算法预览的 12 小时长稳获得 720 个连续 hold 采样，CPU 平均/P95/最大值为 33.92% / 39% / 43%，丢帧及运行期/预览失败均为 0。

4 路 × 5 FPS 配置已完成 12 小时运行；8 路阶梯只记录工程余量，不作为发布容量承诺。这份历史证据作为长稳背景保留，不静默重绑定到更新的 v1.1 源码基线。

## 架构

```text
+------------------------------------------------------------------+
| Web 控制台 | 可视化编排 | REST / WebSocket / MQTT                 |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| C++ 引擎核心                                                     |
| 媒体 | 推理 | 任务 | 规则 | 告警 | 事件 | 模型                    |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| 推理与媒体后端接口                                               |
+--------------------+----------------------+----------------------+
| Sophon BMRT/VPU    | RKNN + MPP/RGA       | ONNX Runtime/FFmpeg  |
| BM1688；CV186X      | RK3576                | x86 Linux / Windows  |
+--------------------+----------------------+----------------------+
```

每次构建只选择一个推理后端，模型产物面向目标平台生成；功能、模型覆盖和容量仍具有平台差异。Model Guard Protected 分发目前属于 Sophon 打包路径。

## 文档、设备与社区

| 入口 | 适合场景 |
| --- | --- |
| [文档首页](https://www.cosmowander.ai/zh/docs/) | 完整文档索引和学习路径 |
| [快速开始](https://www.cosmowander.ai/zh/docs/tutorials/01-quickstart/quickstart) | 首次启动和场景体验 |
| [场景配置](https://www.cosmowander.ai/zh/docs/tutorials/02-scenario-config/scenario-config) | 构建场景级工作流 |
| [VLM 指南](https://www.cosmowander.ai/zh/docs/tutorials/03-vlm-guide/vlm-guide) | 提示词视觉判断与事件 |
| [模型适配指南](https://www.cosmowander.ai/zh/docs/tutorials/05-model-porting/model-porting) | 导入自有模型 |
| [智能体辅助二次开发](docs/development/agent-assisted-development.md) | 委托二开任务并获得可核验结果 |
| [构建指南](https://www.cosmowander.ai/zh/docs/guide/build) | x86、Sophon 与 RK3576 构建、打包路径 |
| [API 概览](https://www.cosmowander.ai/zh/docs/reference/api) | REST、WebSocket、MQTT 与 webhook 集成 |

认证设备提供预配置加速、经过验证的商业模型包和专属部署支持，但不解锁另一套软件功能。中国大陆可通过[淘宝购买认证设备](https://item.taobao.com/item.htm?id=1066672051450)；其他地区或项目部署支持请联系 <hello@cosmowander.ai>。

欢迎提交范围明确的 bug 报告、文档改进、场景示例和集成说明。提交 pull request 前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。社区支持入口为 [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions)和 [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues)；安全问题请按 [SECURITY.md](SECURITY.md) 私密报告。

## FAQ

<details>
<summary><b>没有 Sophon 或 Rockchip 设备可以试用吗？</b></summary>

可以。在 Linux 或 Windows 上使用 x86 开发模式；Apple Silicon Mac 可以使用 Docker Preview 体验单路本地视频下的控制台、流水线、模型管理和集成路径。Mac 路径运行 amd64 仿真且不启用 Model Guard。目标平台的 NPU 加速和容量验证仍需要对应边缘硬件。

</details>

<details>
<summary><b>Open 与 Protected 包的边界是什么？</b></summary>

两者提供相同的应用软件能力，并使用同一种 MD5 升级生命周期。Open 使用明文模型且不需要设备授权；Sophon Protected 包可携带加密商业预置模型和授权工具，需要设备绑定证书。应用升级包本身不签名。

</details>

<details>
<summary><b>可以使用自己训练的模型吗？</b></summary>

可以。模型适配流程会验证张量、预处理、后处理、目标运行时和业务精度约束；模型产物必须面向实际运行的平台生成。

</details>

<details>
<summary><b>CosmoEdge 的生产就绪程度如何？</b></summary>

`v1.1.0` 是 BM1688、CV186X、RK3576 与 x86 的多平台发布线。关联报告记录了实测工作负载边界和明确的证据缺口；生产容量仍需结合自有模型、视频流、精度要求和部署条件完成验证。

</details>

### License

CosmoEdge 使用 [Apache License 2.0](LICENSE) 开源许可。Copyright 2026 CosmoEdge Contributors。

---

<div align="center">

An open-source project by Cosmo Wander AI and the CosmoEdge contributors.

Turn video AI models into deployable edge applications.

📦 本仓库在 [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) 维护只读镜像，代码自动从 GitHub 同步。详见 [MIRRORING.md](MIRRORING.md)。

</div>
