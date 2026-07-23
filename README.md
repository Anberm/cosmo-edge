<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**C++ edge AI engine for production video analytics, visual pipeline orchestration, and on-device VLM workflows**

[![Nightly Sophon Build and Test](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml)

[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#c-native-runtime)
[![Platform](https://img.shields.io/badge/platform-Sophon%20BM1688%20%2F%20x86%20Linux%20%2F%20Windows-purple?style=flat-square)](#supported-platforms)
[![Release](https://img.shields.io/badge/release-v1.0.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Stress Test](https://img.shields.io/badge/stress%20test-200%20video%20samples-brightgreen?style=flat-square)](#validation)
[![Pipelines](https://img.shields.io/badge/pipelines-26%20validated-brightgreen?style=flat-square)](#validation)
[![Website](https://img.shields.io/badge/website-cosmowander.ai-3B82F6?style=flat-square)](https://www.cosmowander.ai/)
[![Docs](https://img.shields.io/badge/docs-online-2563EB?style=flat-square)](https://www.cosmowander.ai/docs/)
[![GitHub](https://img.shields.io/badge/GitHub-cosmo--edge-181717?style=flat-square&logo=github)](https://github.com/cosmo-wander-ai/cosmo-edge)
[![Gitee](https://img.shields.io/badge/Gitee-cosmo--edge-C71D23?style=flat-square&logo=gitee)](https://gitee.com/cosmo-wander-ai/cosmo-edge)

[Official Website](https://www.cosmowander.ai/) | [Online Docs](https://www.cosmowander.ai/docs/) | [Quick Start](#quick-start) | [Features](#key-features) | [Validation](#validation) | [Benchmarks](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/) | [Hardware](#cosmoedge-ready-devices)

[English](README.md) | [简体中文](README.zh-CN.md)

</div>

---

<div align="center">

<https://github.com/user-attachments/assets/23a014a5-d753-432f-8de5-c750bc82d8e2>

</div>

*Run multiple AI pipelines with real-time OSD overlays and live event output on a single edge device.*

CosmoEdge is a C++ edge AI engine for production video analytics. It takes teams from model files to running applications: import models, compose pipelines, connect video sources, view AI overlays in the browser, and send structured events over MQTT or HTTP.

The C++17 runtime handles multi-channel video processing, hardware decoding, OSD rendering, and low-overhead edge deployment — the runtime, web console, and integration path needed to deploy, monitor, debug, and maintain edge AI applications in the field.

## What You Can Build

- Multi-camera safety monitoring with real-time OSD and event snapshots.
- Counting, line-crossing, and zone-intrusion analytics for people, vehicles, and objects.
- On-device visual inspection driven by VLM prompts for quality and compliance checks.
- Long-tail detection of rare or unlisted objects from a text prompt, with no task-specific training (GroundingDINO).

Each runs as a managed edge deployment with model management, scenario tasks, alarm rules, and MQTT/HTTP data export.

## Key Features

### C++ Native Runtime

CosmoEdge uses a C++17 runtime at its core. For edge video systems, decoding, inference scheduling, OSD rendering, event generation, and stream output must run continuously on resource-constrained devices.

- Lower overhead for long-running multi-channel video workloads.
- Direct integration with hardware decoding, NPU runtimes, memory pools, and streaming components.
- Predictable CPU, memory, and thread behavior for appliance-style edge deployments.
- One engine core across x86 developer mode and Sophon production deployments.

### Visual Pipeline Orchestration

Build video AI workflows in a browser. Connect video sources, AI models, post-processing nodes, OSD rendering, alarm rules, and output channels with a visual pipeline editor.

<div align="center">

<https://github.com/user-attachments/assets/94b9418b-36c8-47b6-a730-ad8f508a6709>

</div>

### Application Workflow

CosmoEdge connects the runtime, web console, and integration layer into one deployment workflow:

```text
Model Repository -> Scenario Task -> Real-time Analysis -> Alarm Management -> Data Push
       |                  |                 |                   |              |
  Upload/manage       Configure        Multi-channel        Rule engine   MQTT / HTTP
  ONNX/bmodel         pipelines        AI + OSD overlay     snapshots     webhook
  model versions      per scene        WebRTC streaming     event log     integration
```

<details>
<summary><b>Full capability list</b></summary>

| Module             | Capabilities                                                             |
| ------------------ | ------------------------------------------------------------------------ |
| Model Repository   | Model upload, metadata management, version management, hot-swap workflow |
| Scenario Tasks     | Pipeline binding, camera binding, scheduling, scene-level configuration  |
| Real-time Analysis | RTSP, video files, USB cameras, WebRTC live view, HTTP-FLV fallback      |
| Image Analysis     | Batch image upload, VLM analysis, structured results                     |
| Alarm Management   | Rule-based alarms, severity levels, snapshots, filtering, event history  |
| Data Integration   | MQTT push, HTTP webhook, structured JSON event format                    |
| System Management  | Dashboard, device status, user auth, i18n, configuration management      |

</details>

### Real-time Visual Debugging

CosmoEdge includes an OSD system for both operators and developers:

- Business labels instead of raw model class names.
- Semantic colors for normal, warning, violation, and uncertain states.
- Zone overlays, line-crossing indicators, counters, and event panels.
- Debug view for raw detections, confidence scores, track IDs, and model output.

### Prompt-driven AI: GroundingDINO + VLM

Run prompt-driven vision models on edge devices as asynchronous pipeline nodes alongside traditional CV pipelines:

| Capability         | How it works                                    | Typical use                                           |
| ------------------ | ----------------------------------------------- | ----------------------------------------------------- |
| GroundingDINO      | Text prompt -> open-vocabulary object detection | Find long-tail objects without task-specific training |
| Edge VLM           | Closed question -> YES/NO state judgment        | "Is the cabinet door open?" -> alarm on YES           |
| VLM Image Analysis | Image upload -> structured visual check         | Quality inspection, compliance review                 |

<div align="center">

<https://github.com/user-attachments/assets/212a33a8-e662-4678-9945-02c78d808e4d>

</div>

Edge VLM nodes support compatible Qwen3 VLM series and Qwen3.5 multimodal models. Certified device packages can provide `CosmoEdge-VL-Judge-0.8B`, optimized for YES/NO visual state judgment.

Newly added: the VLM node runs in two interchangeable backends — an embedded on-device runtime (data stays on the device) or any OpenAI-compatible endpoint (self-hosted or SaaS) for larger models. The async, event-driven VLM path absorbs the extra network latency; the on-device runtime remains the validated default.

### Model Sources

**Available in CosmoEdge:**

| Category                  | Supported Models / Architectures                                    | Pipeline Support    |
| :------------------------ | :------------------------------------------------------------------ | :------------------ |
| Object Detection          | YOLOv5, YOLOv8, YOLOv10, YOLOv11, YOLOv12, YOLO26                   | Full pipeline       |
| Object Tracking           | ByteTrack                                                           | Full pipeline       |
| Attribute Classification  | Safety helmet, vest, uniform classifiers                            | Full pipeline       |
| Counting & Statistics     | Line crossing, zone counting, directional flow                      | Full pipeline       |
| Open-vocabulary Detection | GroundingDINO                                                       | Async pipeline node |
| Visual State Judgment     | Qwen3 VLM models, Qwen3.5 multimodal models (text prompt -> YES/NO) | Async pipeline node (on-device or OpenAI-compatible API) |
| Image Analysis            | VLM batch analysis                                                  | Standalone task     |

**Model ecosystem compatibility:**

CosmoEdge uses ONNX as the model interchange format. Models from major CV training frameworks can be imported through a documented conversion path:

- **Ultralytics (YOLO)**: Export with `yolo export format=onnx`, then import via Model Repository or convert to bmodel for NPU deployment.
- **Roboflow**: Train on Roboflow, export ONNX, import into CosmoEdge.
- **Custom models**: Any ONNX-compatible detection or classification model can be integrated through the model porting guide.

**Broader Sophon model ecosystem:**

CosmoEdge runs on the Sophon BM1688 inference stack. Models from SOPHGO's official model zoo can be integrated through the model porting guide, which covers post-processing adaptation and pipeline node registration.

→ [SOPHGO Model Zoo (sophon-demo)](https://github.com/sophgo/sophon-demo)
→ [CosmoEdge Model Porting Guide](docs/en/tutorials/05-model-porting/model-porting.md)

## Quick Start

### Option A: x86 Developer Mode

You can try CosmoEdge without edge hardware. The x86 developer mode uses the same UI and workflow as edge deployment, with lower throughput than Sophon NPU mode.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
# Or use Gitee mirror (China):
# git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
```

> **💡 Docker Compose Version Note**
> This documentation uses the latest Docker Compose V2 command format (`docker compose`). If you are using an older Docker environment, please replace `docker compose` with the hyphenated `docker-compose` in all commands.

```bash
# 2. Start in x86 mode
# Linux:
sudo docker compose -f docker-compose.x86.yml up -d --build

# Windows (PowerShell/CMD):
docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. Open the web console
# http://localhost:8080
```

> **USB cameras**: If you have USB cameras attached, uncomment the `devices` block in `docker-compose.x86.yml` before starting.

After startup, follow the [Scenario Configuration tutorial](docs/en/tutorials/02-scenario-config/scenario-config.md) to set up your first AI detection scenario.

### Option B: Sophon Edge Device

Use this path for NPU-accelerated deployment.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
# Or use Gitee mirror (China):
# git clone https://gitee.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. Build the Sophon/aarch64 package
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package

# 3. View exported release packages
ls -lh build_output/
# The output package will be named like: cosmo-V<version>-<hash>.tar.gz

# 4. Copy the package to the Sophon edge device (replace <device_ip> with actual IP, default is 192.168.100.1)
scp build_output/cosmo-V*.tar.gz root@<device_ip>:/tmp/

# 5. SSH to the device, extract the package, and run the installation script
ssh root@<device_ip>
cd /tmp
tar -zxvf cosmo-V*.tar.gz
sudo bash scripts/install.sh

# 6. Reboot the device to start the services
sudo reboot
```

On Windows PowerShell to build the package:

```powershell
.\scripts\build_sophon_package.ps1
```

After installing the package and rebooting the device:

- **Default IP**: `192.168.100.1` (ensure your computer is configured with a static IP in the `192.168.100.x` subnet to connect directly)
- **Web Console URL**: `http://192.168.100.1`
- **Default Username**: `admin`
- **Default Password**: `admin` (it is highly recommended to change this password after your first login)

This path builds a release package and installs it on a Sophon device. For teams that need production hardware, certified CosmoEdge devices include preconfigured Sophon acceleration, production model packages, and deployment support. See [CosmoEdge-ready devices](#cosmoedge-ready-devices).

Initial Onboarding Guide
<div align="center">

<https://github.com/user-attachments/assets/b4ffe661-cd06-4c79-ac01-310b1028e0be>

Test video path: cosmo-edge\data\test-video

</div>

## Validation

CosmoEdge comes from a commercial codebase and has completed internal system validation before open-source release.

A scenario task (pipeline) bundles model, scheduling, and rule logic; at deployment it binds to specific inputs, zones, and rules. The 26 figure counts validated pipelines — the same set covers far more real deployments as inputs and rules change, with no new code.

| Area                   | Current validation status                                                                                    |
| ---------------------- | ------------------------------------------------------------------------------------------------------------ |
| Video stress test      | Continuous playback test with 200 video samples; no memory leaks or crashes observed                        |
| Pipeline validation    | 26 pipelines validated against internal scenario baselines (CV, VLM, and GroundingDINO)                      |
| Concurrent CV workload | ScenarioBench v1.0 verifies up to 16 NPU video channels for CV workloads; detailed reports are linked below |
| Regression testing     | Multi-round system regression completed with dedicated QA, including the final v1.0 release regression pass |
| Pilot deployments      | Authorized customer pilots covering several hundred video-analysis channels, 2+ months continuous, across a range of industry scenarios |

### Performance Benchmarks

The results below are reproducible ScenarioBench benchmarks. CV and x86 reports are kept under the [v1.0 benchmark set](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/); the VLM row points to the current refreshed VLM methodology report. Full raw `metrics.json` traces are distributed separately from the repository.

A video channel means one decoded input stream. A scenario task is one algorithm pipeline bound to a channel, so mixed scenarios can run more tasks than video channels. The table reports the maximum verified stable channel count within the published benchmark range.

| ScenarioBench workload | Hardware profile | Max verified video channels | Concurrent scenario tasks | Target FPS | Result | Evidence |
| --- | --- | ---: | ---: | ---: | --- | --- |
| No Safety Helmet | YY-16T01-Preview / NPU | 16 | 16 | 3/channel | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.html) |
| Pedestrian Detection | YY-16T01-Preview / NPU | 16 | 16 | 5/channel | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.html) |
| Pedestrian + No Safety Helmet | YY-16T01-Preview / NPU | 16 | 32 | 3/channel/task | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.html) |
| VLM Review | YY-16T01-Preview / NPU | 8 | 8 | 0.1/channel | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/current/vlm-77175-npu/report.html) |
| No Safety Helmet x86 baseline | X86 CPU baseline | 7 | 7 | 3/channel | LIMITED; 8 channels exceeded latency thresholds | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.html) |

See the bilingual [v1.0 benchmark manifest](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/manifest.json), [environment notes](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/environment), and [current benchmark refresh notes](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/current/) for hardware profiles, model inputs, and publication policy. The x86 row is a CPU-only comparison baseline, not the v1.0 NPU device capacity target.

## Architecture

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

### Tech Stack

| Layer       | Technology                                  |
| ----------- | ------------------------------------------- |
| Engine      | C++17, CMake, FFmpeg, SQLiteCpp             |
| Inference   | Sophon BMRT, ONNX Runtime for x86 mode      |
| Frontend    | Vue.js, Vue Flow, Element Plus              |
| Streaming   | SRS 6.0, WebRTC, HTTP-FLV                   |
| Integration | REST API, WebSocket, MQTT, HTTP webhook     |

## Supported Platforms

| Platform       |  Status  | Intended use                                 |
| -------------- | :-------: | -------------------------------------------- |
| Sophon BM1688  |  Primary  | NPU-accelerated production deployment        |
| x86 Linux      | Supported | Development, evaluation, integration testing |
| x86 Windows    | Supported | Development and evaluation                   |
| Sophon BM1684X |  Planned  | NPU-accelerated deployment                   |

## CosmoEdge-ready Devices

CosmoEdge is open source. The repository provides the same engine, web UI, and workflow used by certified device packages. You can bring your own models, run on x86 for development, and deploy on compatible edge hardware.

Certified devices help teams avoid hardware bring-up and model packaging work. They include preconfigured NPU acceleration, production model packages, and dedicated support.

| Capability                   |               Open-source repository               |    Certified device package    |
| ---------------------------- | :------------------------------------------------: | :-----------------------------: |
| C++ engine                   |                      Included                      |            Included            |
| Visual pipeline orchestrator |                      Included                      |            Included            |
| Web management console       |                      Included                      |            Included            |
| x86 developer mode           |                      Included                      |            Included            |
| Sophon NPU runtime support   |         Source support, hardware required         |          Preconfigured          |
| CV model package             |               Bring your own models               | Pre-installed (~25 production CV models) |
| `CosmoEdge-VL-Judge-0.8B`  | Bring your own/custom package; validation required | Pre-installed validated package |
| GroundingDINO package        |          Bring your own or custom package          |          Pre-installed          |
| Deployment support           |                     Community                     |            Dedicated            |

Certified devices improve deployment readiness; they do not lock software features behind a hardware SKU.

The open-source engine and all software features are available worldwide today, no purchase required. CosmoEdge-ready certified devices are now available in mainland China through the [Taobao store](https://item.taobao.com/item.htm?id=1066672051450). For availability outside mainland China or project deployment support, contact <hello@cosmowander.ai>.

## Documentation

| Start here                                                                                   | For            | Description                               |
| -------------------------------------------------------------------------------------------- | -------------- | ----------------------------------------- |
| [Documentation Home](docs/en/index.md)                                                          | Everyone       | Full documentation index and reading path |
| [Quick Start Guide](docs/en/tutorials/01-quickstart/quickstart.md)                              | Everyone       | First setup and scenario run              |
| [Scenario Configuration](docs/en/tutorials/02-scenario-config/scenario-config.md)               | Integrators    | Build scene-level AI workflows            |
| [VLM Guide](docs/en/tutorials/03-vlm-guide/vlm-guide.md)                                        | Developers     | Use visual state judgment with prompts    |
| [Pipeline Orchestration](docs/en/tutorials/04-pipeline-orchestration/pipeline-orchestration.md) | Advanced users | Compose custom pipelines visually         |
| [Model Porting Guide](docs/en/tutorials/05-model-porting/model-porting.md)                      | ML engineers   | Import ONNX or target-runtime models      |
| [Build Guide](docs/en/guide/build.md)                                                           | Developers     | Build x86 Docker and Sophon packages      |
| [API Overview](docs/en/reference/api.md)                                                        | Developers     | REST, WebSocket, and MQTT API categories  |

## Release Status and Roadmap

CosmoEdge `v1.0.0` is the first stable public release. The engine is production-ready for evaluation, integration, and community model expansion.

### Shipped in v1.0.0

- [X] C++17 edge inference engine
- [X] Visual pipeline orchestrator
- [X] Web management console
- [X] x86 developer mode for Linux and Windows
- [X] Sophon BM1688 release packaging
- [X] VLM and GroundingDINO integration
- [X] 26 pipeline scenarios internally validated
- [X] Final v1.0 regression pass and hardening (shell-injection guards, null-safety and lifecycle fixes, default-password enforcement)

### Roadmap

- [ ] Expand the validated pipeline scenario library
- [ ] Community model and scenario examples
- [ ] Additional model adapters and post-processing templates

## Contributing

CosmoEdge is at v1.0.0. We welcome focused contributions in these areas:

- Bug reports with logs and reproduction steps.
- Documentation fixes and tutorial improvements.
- Scenario examples and integration notes.
- Small, scoped pull requests after an issue discussion.

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. First-time contributors can also start with the [Contributor Guide](docs/en/development/contributing.md).

## FAQ

<details>
<summary><b>Do I need a Sophon device to try CosmoEdge?</b></summary>

No. Use x86 developer mode on Linux or Windows to try the UI, pipeline workflow, model management, and integration path. Sophon hardware is needed for production-level NPU throughput.

</details>

<details>
<summary><b>How many scenarios or algorithms does CosmoEdge support?</b></summary>

CosmoEdge has two core concepts: models (AI weights) and scenario tasks — also called pipelines — each an orchestrated graph of model, scheduling, and rule logic that binds to specific inputs, zones, and rules at deployment. The 26 validated pipelines each cover many real deployments as their inputs and rules change. Capability scales with composition, not with a fixed algorithm catalog.

</details>

<details>
<summary><b>Does the open-source repository include model weights?</b></summary>

The open-source repository does not include production model weights by default. You can bring your own models, including compatible Qwen3 VLM series models and Qwen3.5 multimodal models. Certified device packages can provide a library of ~25 pre-installed production CV models, plus `CosmoEdge-VL-Judge-0.8B` and GroundingDINO. Community or custom models should be validated for the target scenario.

</details>

<details>
<summary><b>Can I use my own trained models?</b></summary>

Yes. CosmoEdge is designed around model import and model lifecycle management. The model porting guide documents the recommended path from ONNX or target runtime formats into the model repository.

</details>

<details>
<summary><b>How is CosmoEdge different from inference servers or NVR projects?</b></summary>

CosmoEdge is an application runtime for complete edge AI workflows, not just a model-serving layer or video recorder.

</details>

<details>
<summary><b>Is CosmoEdge production-ready?</b></summary>

The codebase comes from commercial development for production deployments and has passed internal stress, pipeline, and regression validation, including the final v1.0 release regression pass. CosmoEdge `v1.0.0` is the first stable public release: the engine, core workflows, and release packaging are built for field deployment, and public APIs and contributor workflows are stable.

</details>

## Contact

- Community: [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions)
- 国内社区: [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues)
- Partnership & Enterprise: <hello@cosmowander.ai>
- Security: see [SECURITY.md](SECURITY.md) for private vulnerability reporting

## License

CosmoEdge is licensed under the [Apache License 2.0](LICENSE).

```text
Copyright 2026 CosmoEdge Contributors

Licensed under the Apache License, Version 2.0
```

---

<div align="center">

An open-source project by Cosmo Wander AI and the CosmoEdge contributors.

Turn video AI models into deployable edge applications.

📦 This repo is mirrored to [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) for mainland China access. See [MIRRORING.md](MIRRORING.md).

</div>
