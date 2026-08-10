<div align="center">

<img src="docs/assets/cosmoedge-logo.png" width="320" alt="CosmoEdge">

**Turn video AI models into deployable edge applications — a C++ edge AI engine for Sophon, Rockchip, and x86.**

Build and operate video analytics, VLM, and event workflows through a consistent orchestration experience. Each platform uses its own runtime, build, and model artifacts.

[![Nightly Sophon Build and Test](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml/badge.svg?branch=main)](https://github.com/cosmo-wander-ai/cosmo-edge/actions/workflows/nightly-build-test-sophon.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](LICENSE)
[![Runtime](https://img.shields.io/badge/runtime-C%2B%2B17-orange?style=flat-square)](#core-capabilities)
[![Release](https://img.shields.io/badge/release-v1.0.0-green?style=flat-square)](https://github.com/cosmo-wander-ai/cosmo-edge/releases)
[![Website](https://img.shields.io/badge/website-cosmowander.ai-3B82F6?style=flat-square)](https://www.cosmowander.ai/)
[![Docs](https://img.shields.io/badge/docs-online-2563EB?style=flat-square)](https://www.cosmowander.ai/docs/)
[![Gitee](https://img.shields.io/badge/Gitee-cosmo--edge-C71D23?style=flat-square&logo=gitee)](https://gitee.com/cosmo-wander-ai/cosmo-edge)

[Quick Start](#quick-start) · [Platforms](#choose-a-platform) · [Validation](#validation) · [Documentation](#documentation-devices-and-community) · [简体中文](README.zh-CN.md)

</div>

---

<div align="center">

<https://github.com/user-attachments/assets/23a014a5-d753-432f-8de5-c750bc82d8e2>

</div>

CosmoEdge goes beyond model serving with a complete application layer for model import, visual orchestration, alarms, and event delivery. The core engine and console in this repository are released under Apache-2.0; certified hardware, commercial preset models, and Model Guard distribution protection have separate boundaries.

## Next Release Preview

> The items below target the next release. Availability in the current source does not mean inclusion in the v1.0.0 packages. See the platform matrix and its linked evidence for platform status.

- **Rockchip RK3576:** the frozen engineering candidate passed ScenarioBench at 1 / 2 / 4 / 8 channels at 5 FPS and completed a 12-hour, 4-channel run at 5 FPS; final combined-branch system validation is pending.
- **Sophon model handling:** chip-agnostic model directories and `chip_type` validation prepare the model metadata and import path for BM1688 and CV186X; CV186X package and device evidence is still in progress.
- **RKNN data path:** targeted DMA-BUF-to-RGA input, persistent bound-input, native quantized output, and direct YOLOv8 candidate decoding paths with explicit fallbacks; on the same frozen candidate, single-channel Detect average decreased from 142.3 ms to 58.2 ms (-59.1%).
- **Agent-assisted development:** a repository-guided path for handing model porting, integration, and UI tasks to the coding agent you already use and receiving verifiable deliverables.
- **Model Guard 2.3:** protects commercial preset-model distribution in Sophon Protected packages. Open and Protected expose the same application features, with no SKU-gated software functionality; they differ in model encryption and device-provisioning tooling.
- **macOS Docker Preview:** an isolated `linux/amd64` candidate for one local-video workflow on Apple Silicon. Two consecutive end-to-end acceptance runs are still required before publication; it does not enable Model Guard or represent native/NPU performance.

## Choose a Platform

CosmoEdge provides one engine architecture and orchestration experience, but each build selects one inference backend and uses models generated for that target platform.

| Platform | Status | Runtime / model artifact | Current scope and evidence |
| --- | --- | --- | --- |
| Sophon BM1688 | Primary | BMRT / `.nn` | Production deployment path with published [v1.0 baselines](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/) |
| Rockchip RK3576 | Release candidate | RKNN / `.rknn` | Engineering baseline: 8 channels at 5 FPS passed; final combined-branch validation is pending. Start with the [integration guide](docs/guide/rk3576-rknn-development.md) |
| Sophon CV186X | Candidate | BMRT / target-specific `.nn` | Model-directory and import compatibility; release package and device evidence in progress |
| x86 Linux / Windows; Apple Silicon macOS | Linux / Windows supported; macOS Preview | ONNX Runtime / `.onnx` | Mac uses amd64 emulation for one local-video developer workflow, not native performance evidence |
| Sophon BM1684X | Planned | — | Not part of the current release scope |

## Quick Start

### Try locally on x86

No edge hardware is required. The x86 mode uses the same UI and workflow with lower throughput than an NPU deployment.

```bash
# 1. Clone
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge

# 2. Start on Linux
sudo docker compose -f docker-compose.x86.yml up -d --build
# Windows: docker compose -f docker-compose.x86.windows.yml up -d --build

# 3. Open http://localhost:8080
```

Apple Silicon macOS uses a separate amd64-emulation Preview path:

```bash
./scripts/macos-docker-preview.sh doctor
./scripts/macos-docker-preview.sh up
./scripts/macos-docker-preview.sh status
# Open http://127.0.0.1:8080
```

The Mac Preview is for local, single-video evaluation. It is not a native macOS
binary, an NPU deployment, or production performance evidence. Read the full
[macOS Docker Preview](docs/en/guide/macos-docker-preview.md) boundaries first.

After startup, use the [Scenario Configuration tutorial](https://www.cosmowander.ai/docs/tutorials/02-scenario-config/scenario-config) to create your first AI detection task. Docker Compose V1 users can replace `docker compose` with `docker-compose`.

### Build for Sophon BM1688

```bash
git clone https://github.com/cosmo-wander-ai/cosmo-edge.git
cd cosmo-edge
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
ls -lh build_output/public-runtime/
```

The default Open package contains plaintext models and requires no device
authorization. To install it on an existing CosmoEdge device, sign in to the web
console, open **System Management → System Maintenance → Software Upgrade**,
select the generated `cosmo-V<version>-<md5>.tar.gz`, and confirm the upgrade.
Keep power connected while the device uploads, validates, installs, and reboots.
After signing in again, verify that **Software Version** matches the package
version.

This public workflow covers upgrades of an already provisioned device. The
repository does not currently publish a blank-device factory-install procedure.
Protected packages use the same web upgrade flow but contain encrypted preset
models and provisioning tooling. See the [Build Guide](https://www.cosmowander.ai/docs/guide/build)
and [Deployment Guide](https://www.cosmowander.ai/docs/guide/deployment) for the
full validation and recovery boundaries.

### Evaluate the RK3576 candidate

> RK3576 is currently a release candidate and requires pinned RKNN runtime and Rockchip media dependencies. Start with the [RK3576 integration guide](docs/guide/rk3576-rknn-development.md).

CV186X does not yet have a public Quick Start. Use the [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) for the current target-specific model contract.

## What You Can Build

- **Real-time video analytics:** detection, classification, tracking, zones, counters, OSD, and alarm snapshots.
- **Prompt-driven vision:** VLM state judgment and GroundingDINO open-vocabulary detection alongside conventional CV pipelines.
- **Visual application workflows:** connect models, rules, events, and output actions in the browser.
- **Edge integrations:** operate managed tasks and deliver structured events through REST, WebSocket, MQTT, or HTTP webhooks.

## Core Capabilities

| Capability | What it covers | Go deeper |
| --- | --- | --- |
| Native runtime | C++17 engine for multi-channel media, inference scheduling, OSD, tasks, and events | [Architecture](https://www.cosmowander.ai/docs/guide/architecture) |
| Visual orchestration | Browser-based pipeline composition, task binding, parameter validation, and live feedback | [Pipeline tutorial](https://www.cosmowander.ai/docs/tutorials/04-pipeline-orchestration/pipeline-orchestration) |
| Inference and media | Platform backends for Sophon, RKNN, and x86; platform-specific builds and model artifacts | [Build Guide](https://www.cosmowander.ai/docs/guide/build) |
| VLM and DINO | Prompt-based judgment, open-vocabulary detection, and optional VLM review before a detection alarm is reported | [VLM Guide](https://www.cosmowander.ai/docs/tutorials/03-vlm-guide/vlm-guide) |
| Operations and integration | Model management, alarms, event history, REST, WebSocket, MQTT, and webhooks | [API Overview](https://www.cosmowander.ai/docs/reference/api) |
| Model onboarding and protection | Model conversion, import, validation, and the Open/Protected distribution boundary | [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) |

<details>
<summary>▶ Watch: compose a complete visual pipeline</summary>

<https://github.com/user-attachments/assets/94b9418b-36c8-47b6-a730-ad8f508a6709>

</details>

<details>
<summary>▶ Watch: GroundingDINO and VLM visual workflows</summary>

<https://github.com/user-attachments/assets/212a33a8-e662-4678-9945-02c78d808e4d>

</details>

## Agent-Assisted Development

Already have a model-porting, integration, or UI task? Give your usual coding agent the business goal, available materials, target device or test environment, and acceptance criteria. The repository provides task entry points, examples, checks, and evidence boundaries so the result can include importable artifacts, scoped code changes, and a verifiable conclusion.

Start with [Agent-Assisted Development](docs/en/development/agent-assisted-development.md), then use the [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) or [Contributor Guide](docs/en/development/contributing.md) for the task at hand.

## Validation

The table below is a **published v1.0 baseline**, not evidence for every next-release platform. New results will be bound to the source commit and tree, final package SHA-256, device and runtime versions, model and dataset hashes, and acceptance thresholds.

| ScenarioBench workload | Hardware | Max verified channels | Target FPS | Result | Evidence |
| --- | --- | ---: | ---: | --- | --- |
| No Safety Helmet | YY-16T01-Preview / NPU | 16 | 3/channel | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-npu/report.html) |
| Pedestrian Detection | YY-16T01-Preview / NPU | 16 | 5/channel | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-45626-npu/report.html) |
| Pedestrian + No Safety Helmet | YY-16T01-Preview / NPU | 16 | 3/channel/task | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/pedestrian-helmet-mixed-npu/report.html) |
| VLM Review | YY-16T01-Preview / NPU | 8 | 0.1/channel | PASS | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/vlm-55009-npu/report.html) |
| No Safety Helmet x86 baseline | x86 CPU | 7 | 3/channel | LIMITED; 8 channels exceeded latency limits | [report](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/helmet-7463-x86/report.html) |

See the [benchmark manifest](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/manifest.json), [environment notes](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.0/environment), and [current refresh notes](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/current/) for methodology and publication boundaries. The published table above does not represent RK3576 or CV186X release evidence; the separate RK3576 engineering baseline follows.

### RK3576 engineering baseline

This next-release result is bound to frozen source candidate [`8f8b4b8e`](https://github.com/cosmo-wander-ai/cosmo-edge/commit/8f8b4b8e793172963ef92da7fc9942a1c860534b) (tree `fd1b646f`), engine SHA-256 `bc829d9513334c4520fad1b58439bb3e6e31338c664e93eb15babdaaa564d886`, RKNN Runtime `2.3.2-429f97ae6b`, driver `0.9.8`, RGA `1.10.1_[4]`, and MPP `1.5.0-1`.

| ScenarioBench workload | Hardware | Max verified channels | Target FPS | Result |
| --- | --- | ---: | ---: | --- |
| No Safety Helmet | Rockchip RK3576 EVB1 V10 / RKNN | 8 | 5/channel | PASS |

The 1 / 2 / 4 / 8-channel steps all passed with zero discarded frames or inference, RGA, or MPP failures. Single-channel Detect average decreased from 142.3 ms to 58.2 ms (-59.1%). A separate 12-hour run at 4 channels and 5 FPS, with four algorithm previews, recorded 720 continuous hold samples, CPU avg/P95/max of 33.92% / 39% / 43%, and zero discarded frames or runtime/preview failures.

This is an engineering baseline, not release evidence for the current combined branch. The sanitized report and manifest must be published, and the final combined candidate rerun, before this result is promoted to the release matrix.

## Architecture

```text
+------------------------------------------------------------------+
| Web Console | Visual Orchestration | REST / WebSocket / MQTT      |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| C++ Engine Core                                                   |
| Media | Inference | Tasks | Rules | Alarms | Events | Models      |
+--------------------------------+---------------------------------+
                                 |
+--------------------------------v---------------------------------+
| Inference and Media Backend Interfaces                            |
+--------------------+----------------------+----------------------+
| Sophon BMRT/VPU    | RKNN + MPP/RGA       | ONNX Runtime/FFmpeg  |
| BM1688; CV186X candidate | RK3576          | x86 Linux / Windows  |
+--------------------+----------------------+----------------------+
```

One build selects one inference backend. Model artifacts are generated for the target platform, and feature/model coverage and capacity remain platform-specific. Model Guard Protected distribution currently belongs to the Sophon packaging path.

## Documentation, Devices, and Community

| Start here | Best for |
| --- | --- |
| [Documentation Home](https://www.cosmowander.ai/docs/) | Full documentation index and learning path |
| [Quick Start Guide](https://www.cosmowander.ai/docs/tutorials/01-quickstart/quickstart) | First setup and scenario run |
| [Scenario Configuration](https://www.cosmowander.ai/docs/tutorials/02-scenario-config/scenario-config) | Building scene-level workflows |
| [VLM Guide](https://www.cosmowander.ai/docs/tutorials/03-vlm-guide/vlm-guide) | Prompt-based visual judgment and events |
| [Model Porting Guide](https://www.cosmowander.ai/docs/tutorials/05-model-porting/model-porting) | Importing your own model |
| [Agent-Assisted Development](docs/en/development/agent-assisted-development.md) | Delegating an extension task with verifiable results |
| [Build Guide](https://www.cosmowander.ai/docs/guide/build) | x86 and Sophon build/package paths |
| [API Overview](https://www.cosmowander.ai/docs/reference/api) | REST, WebSocket, MQTT, and webhook integration |

Certified devices add preconfigured acceleration, validated commercial model packages, and dedicated deployment support; they do not unlock separate software features. Devices are available in mainland China from the [Taobao store](https://item.taobao.com/item.htm?id=1066672051450); contact <hello@cosmowander.ai> for other regions or project support.

Contributions are welcome through scoped bug reports, documentation improvements, scenarios, and integration notes. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Community support is available through [GitHub Discussions](https://github.com/cosmo-wander-ai/cosmo-edge/discussions) and [Gitee Issues](https://gitee.com/cosmo-wander-ai/cosmo-edge/issues); report vulnerabilities through [SECURITY.md](SECURITY.md).

## FAQ

<details>
<summary><b>Can I try CosmoEdge without Sophon or Rockchip hardware?</b></summary>

Yes. Use x86 developer mode on Linux or Windows. Apple Silicon Macs can use the Docker Preview for the console, pipeline, model-management, and integration workflow with one local video. The Mac path uses amd64 emulation and does not enable Model Guard. Edge NPU hardware is still required for target-platform acceleration and capacity validation.

</details>

<details>
<summary><b>What is the boundary between the Open and Protected packages?</b></summary>

They expose the same application features and use the same MD5 upgrade lifecycle. Open uses plaintext models without device authorization; Sophon Protected packages can carry encrypted commercial preset models and provisioning tooling that require a device-bound certificate. Application archives themselves are not signed.

</details>

<details>
<summary><b>Can I use my own trained models?</b></summary>

Yes. Use the model-porting path to validate the tensor, preprocessing, post-processing, target runtime, and business accuracy contract. A model artifact must be generated for the platform where it will run.

</details>

<details>
<summary><b>How production-ready is CosmoEdge?</b></summary>

`v1.0.0` is the current stable public release, with published BM1688 and x86 baseline reports above. RK3576 now has the candidate-bound engineering baseline shown above, but it remains non-release evidence until the final combined candidate and sanitized report are published. Validate your own models and deployment conditions before production use.

</details>

### License

CosmoEdge is licensed under the [Apache License 2.0](LICENSE). Copyright 2026 CosmoEdge Contributors.

---

<div align="center">

An open-source project by Cosmo Wander AI and the CosmoEdge contributors.

Turn video AI models into deployable edge applications.

📦 This repository is mirrored read-only to [Gitee](https://gitee.com/cosmo-wander-ai/cosmo-edge) for mainland China access. See [MIRRORING.md](MIRRORING.md).

</div>
