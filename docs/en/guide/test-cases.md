---
title: Test Scope and Test Cases
description: Current functional, platform, integration, performance, and reliability test scope for CosmoEdge, with reusable acceptance cases.
prev:
  text: Architecture Overview
  link: /en/guide/architecture
next: false
---

# CosmoEdge Test Scope and Test Cases

This document defines the current test baseline for QA, development, and integration teams. Every conclusion
must identify the software version, target platform, model, input, configuration, and duration. A pass on x86,
in a build container, or on another chip is not target-device acceptance.

## 1. Test Scope and Evidence Boundaries

Testing covers these layers:

1. **Static and unit testing**: formatting, static analysis, DTO, route, service, and component tests.
2. **Container integration testing**: the login, upload, configuration, inference, and event loop on x86 Docker.
3. **Target-platform testing**: target-specific models, media paths, and resource behavior on real BM1688, CV186X, or RK3576 devices.
4. **Business acceptance**: false positives, misses, event semantics, and sustained operation on fixed positive, negative, and difficult samples.
5. **Release evidence**: a frozen candidate commit, package digest, target marker, running version, and complete results.

Preserve these boundaries:

- Each build selects exactly one inference backend. ONNX, Sophon `.nn`/`.bmodel`, and RKNN `.rknn` artifacts are not cross-backend artifacts.
- BM1688, CV186X, RK3576, and RV1126B artifacts are not interchangeable based on file names.
- Open packages use plaintext models and do not require a device-binding certificate. Protected model distribution and its device certificate are separate capabilities.
- `COSMO_DEV_MODE` controls development logging, watchdog, and related production behavior. It is not an HTTP-login or device-SN authentication bypass.
- Live boxes and labels are rendered from Pipeline metadata. The current action catalog has no standalone OSD node.
- Performance gates must come from measurements of the current candidate, not fixed channel or latency values from another release, model, or platform.

## 2. Test Environments and Materials

### 2.1 Platform Matrix

| Platform | Backend / artifact | Applicable tests | Evidence boundary |
| --- | --- | --- | --- |
| x86 Linux / Windows | ONNX Runtime / `.onnx` | UI, API, workflow, and CPU integration | Does not establish NPU behavior or capacity |
| Apple Silicon macOS Preview | Emulated `linux/amd64` / `.onnx` | One local-video evaluation path | Not native arm64, NPU, or production performance evidence |
| Sophon BM1688 | BMRT / target-specific model package | Installation, inference, media, capacity, and stability | Requires a BM1688 package and device |
| Sophon CV186X | BMRT / target-specific model package | Installation, inference, media, capacity, and stability | Requires a CV186X package and device |
| Rockchip RK3576 | RKNN Runtime / `.rknn` | RKNN, MPP/RGA, capacity, and stability | Requires an RK3576 package and device |
| Rockchip RV1126B | RKNN Runtime / `.rknn` | Build and conditional device validation | Deployable acceptance also needs the target model overlay |

Record the actual OS, driver, runtime, model, and package identity in the report. This table is not a version lock.

### 2.2 Test Materials

- Fixed videos and images with SHA-256 values; inputs must be legally usable and free of unsanitized customer data.
- Model source, version, digest, input/output contract, preprocessing, postprocessing, and label order.
- Positive, negative, and difficult samples with an expected result for every sample.
- Target chip, software version, package digest, runtime, and driver identity.
- An isolated MQTT broker or HTTP webhook receiver for push testing.
- A versioned ScenarioBench package and explicit gates for performance tests.

## 3. Functional and Integration Test Cases

### 3.1 Authentication, Sessions, and Onboarding (TC-AUTH)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-AUTH-001` | First login | Sign in to a new data directory with `admin` / `admin` | Login returns an `mtk`; the response requires changing the factory-default password |
| `TC-AUTH-002` | Invalid session | Call `/gtw/cwai/System/QueryDeviceInfo` without an `mtk` or with an invalid one | HTTP 401 or an explicit authentication failure; it must not be confused with a missing route |
| `TC-AUTH-003` | Password change and session revocation | Change the password, then reuse every token issued before the change | The password persists; all old tokens for that user are revoked; the new password can log in |
| `TC-AUTH-004` | Onboarding boundary | Call `Status` and `Complete` anonymously; call `Reset` anonymously and after login | `Status`/`Complete` allow anonymous access; `Reset` requires a valid `mtk`; state persists |
| `TC-AUTH-005` | Open model-distribution boundary | Query model-authorization status in an Open package and run a bundled plaintext model | An unavailable provisioning tool does not block Open models; application features are not restricted by device SN |
| `TC-AUTH-006` | Protected certificate status | Test no certificate, an invalid certificate, and a valid certificate in a Protected package containing the provisioner | States are actionable; invalid or foreign-device certificates are not valid; a valid certificate binds to this device |

### 3.2 Video Input and Media Lifecycle (TC-CAM)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-CAM-001` | Local-video upload | Query upload capabilities, upload a fixed MP4 in chunks, and create a channel | Progress, completion, and channel data are correct; no server temporary path is disclosed |
| `TC-CAM-002` | Play count | Set play count to `0`, `1`, and a value greater than 1 | `0` loops indefinitely; `1` plays once; a positive number is total plays; values outside `0–100` cannot persist |
| `TC-CAM-003` | RTSP input | Add a fixed RTSP source and observe first frame, sustained playback, and cleanup | It connects and decodes; stop or delete leaves no task, decoder, or preview state behind |
| `TC-CAM-004` | RTSP reconnection | Interrupt a running RTSP source and then restore it | Disconnection is visible; reconnect is automatic; no crash, duplicate task, or permanent stuck state |
| `TC-CAM-005` | Invalid input | Submit an unsupported protocol, invalid address, corrupt video, and over-budget file | Each input is safely rejected with an actionable error; other channels remain available |

### 3.3 Model Repository and Target Compatibility (TC-MOD)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-MOD-001` | x86 model import | Import a supported fixed ONNX with complete metadata | The model is usable; positive and negative image results match the baseline |
| `TC-MOD-002` | Sophon model import | Import a chip-matched `.bmodel`/model package on the target device | Loading, tensors, and postprocessing pass; upload success alone is not acceptance |
| `TC-MOD-003` | RKNN model import | Load the corresponding `.rknn` on RK3576 or RV1126B | Runtime, input format, and output parsing match the target profile |
| `TC-MOD-004` | Wrong target artifact | Import a target-specific artifact on an incompatible chip or backend | Initialization or preflight fails explicitly; no silent fallback is reported as success |
| `TC-MOD-005` | Export policy | Export a user-managed model and a preset/encrypted/device-bound model | Exportable models return attachments; protected models return an explicit non-exportable error |

### 3.4 Pipeline Orchestration (TC-FLOW)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-FLOW-001` | Minimal detection chain | Create “Video Decode → Object Detection → optional Tracking/Filter → Event Report” | Nodes, edges, and parameters persist; the running chain produces explainable targets and events |
| `TC-FLOW-002` | Invalid connection | Save a missing upstream input, incompatible type, or broken edge | Frontend or backend rejects it and identifies an actionable node or field |
| `TC-FLOW-003` | Import/export | Export a Pipeline and import it into an isolated environment on the same version | Node IDs, model references, parameters, and edges persist; missing models are reported as dependencies |
| `TC-FLOW-004` | Current action catalog | Inspect the action list and configure live overlays | No standalone OSD action exists; Live Display renders Pipeline output metadata |

### 3.5 Scenario Tasks, Regions, and Strategies (TC-TASK)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-TASK-001` | ROI behavior | Place the target inside and outside a fixed region under identical settings | The positive sample inside creates the expected event; the outside negative sample creates no matching region event |
| `TC-TASK-002` | Parameter persistence | Change thresholds, alarm interval, deduplication, and time rules; save and restart | UI, runtime configuration, and post-restart values agree; one-variable changes remain explainable |
| `TC-TASK-003` | Schedule | Configure an active and an inactive time window | Tasks run only in the active window; boundary transitions do not duplicate tasks or leak resources |
| `TC-TASK-004` | Batch switching | Enable, disable, and repeat requests across multiple channels | Operations are idempotent; failures identify their item; inference and media resources release after stop |

### 3.6 Live Display and Rendering (TC-LIVE)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-LIVE-001` | Raw/algorithm preview | Switch one channel between raw and algorithm streams | First-frame, loading, no-signal, and switching states are correct; stale players and connections are cleaned up |
| `TC-LIVE-002` | Metadata overlay | Enable an algorithm that supports visualization and select its overlay | Boxes, labels, regions, and tracks match target metadata; there is no standalone OSD-node dependency |
| `TC-LIVE-003` | Sustained playback | Preview for the task-defined duration while switching channels and overlays | Player, connection, and memory counts do not grow without bound; no persistent black frame or stale overlay |

The task must define latency gates and record network, codec, and playback mode. This page does not set a universal millisecond target.

### 3.7 VLM and DINO (TC-LLM)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-LLM-001` | VLM image baseline | Run one prompt on fixed positive, negative, and difficult samples | Output matches the task definition; false positives, misses, latency, and resources are recorded and repeatable |
| `TC-LLM-002` | VLM video task | Bind a fixed video after the image baseline passes and set analysis frequency | Events, captures, and timing match results; no repeated initialization or sustained timeout |
| `TC-LLM-003` | DINO open vocabulary | Test clear English target words on positive and negative samples | Boxes match the target meaning and location; ambiguous terms and similar negatives are recorded |

VLM/DINO gates must bind the model, prompt, input, target FPS, and device. Do not use a universal inference-time value.

### 3.8 Events and External Push (TC-ALARM)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-ALARM-001` | Event query/export | Query and export fixed events by channel, algorithm, and time | List and export counts, times, and fields agree; encoding is correct; sensitive paths are not disclosed |
| `TC-ALARM-002` | MQTT | Configure an isolated broker, trigger one fixed event, and listen on the device-report topic | Registration, heartbeat, and event structures match the MQTT reference; disconnect and reconnect are observable |
| `TC-ALARM-003` | HTTP webhook | Configure an isolated receiver that returns success, timeout, and failure | Success payload fields are correct; failure does not block the main task; retry/status matches the implementation |

### 3.9 System, Upgrade, and Recovery (TC-SYS)

| Case | Goal | Steps | Expected result |
| --- | --- | --- | --- |
| `TC-SYS-001` | Resource-aware upload | Query capabilities and upload with ample space and at the safety reserve | Available/required space and refusal reason agree; the disk reserve is enforced |
| `TC-SYS-002` | Software upgrade | Record `bootId` and version, upload a valid package, and upgrade | Name, MD5, archive, and space validation precede reboot; a new `bootId` plus version proves recovery |
| `TC-SYS-003` | Invalid upgrade | Use a wrong name, digest, target, or dangerous archive structure | Upgrade fails before replacing the active application; the old version remains usable or follows the recovery plan |
| `TC-SYS-004` | Watchdog | Simulate a process failure only on an authorized test device | A non-development build follows its service recovery policy; a `COSMO_DEV_MODE` build records that watchdog is disabled |

## 4. Performance and Stability Tests

| Case | Goal | Method | Pass condition |
| --- | --- | --- | --- |
| `TC-PERF-001` | Capacity staircase | Increase load channel by channel with a versioned ScenarioBench package | Report the last passing point and first failure; do not turn the highest configured point into a recommendation automatically |
| `TC-PERF-002` | Preview overhead | Test no preview, one preview, every-channel preview, and multiple clients on one stream | Record media, inference, discard, and resources separately for every mode; conclusions are not interchangeable |
| `TC-PERF-003` | Sustained run | Run the fixed full-load point for the required 12/24/72 hours | Full-load coverage reaches the gate; sampling is continuous; no crash, counter rollback, or unbounded resource growth |
| `TC-PERF-004` | Business accuracy | Compare reference and candidate output on a versioned offline dataset | Use task-approved class, IoU, confidence, and matching gates; humans review false positives and misses |

A public benchmark describes only its recorded platform, model, input, and version. A new candidate needs its own result or an explicit unverified status.

## 5. Minimum Validation by Change Type

| Change | Minimum validation |
| --- | --- |
| Documentation | Complete documentation checks and site build |
| Frontend | i18n, resource i18n, production build, and affected-page acceptance |
| C++ | Formatting, CPU test build, `cosmo-tests`, and focused unit tests |
| API / network | Authentication, invalid input, error response, and lifecycle tests |
| Model / inference / media | Target package, target device, fixed real input, and initialization logs |
| Build / package / resources | Target marker, package content, SHA-256, and deployment compatibility |

Use [CI and Quality Checks](/en/development/ci) and the [Build Guide](/en/guide/build) for commands; this page does not maintain a second command list.

## 6. Test Report Requirements

Every report must include:

- candidate commit and source tree;
- platform, OS, runtime, driver, and media path;
- package name, target marker, and SHA-256;
- model, input, configuration, and ScenarioBench package identities;
- exact commands, start/end time, and full-load duration;
- pass, fail, blocked, and waived results;
- first failure, logs, and recovery status;
- unverified boundaries, especially missing target-device, performance, stability, or production acceptance;
- confirmation that customer data, credentials, device identities, and private addresses were sanitized.

A successful build, x86 result, model upload, or returning web page proves only that layer. None alone is target-device or production acceptance.
