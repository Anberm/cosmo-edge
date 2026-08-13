# CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark

> Person detection, safety-helmet detection, and concurrent multi-task workloads on BM1688, CV186X, and RK3576

Entry points: [English report](report.html) · [中文报告](report.zh-CN.html) · [methodology](methodology.md) · [machine-readable results index](results/index.json)

## Summary

> **Status: release-candidate public performance material. Repository and PR preparation are complete; external publication has not started.**

We validated CosmoEdge 1.1 multi-stream video analytics on three edge-AI platforms. Under the model, video, device, and runtime conditions defined by this report, the short staircase runs completed 16 channels with two detectors at 5 FPS on BM1688, and 8 channels with two detectors at 5 FPS on CV186X and RK3576. These are measured workload results for the stated configurations, not theoretical chip limits.

## Recommended profiles and observed boundaries

| Platform | Recommended profile | Short-run observed boundary | Hold per step | Status |
| --- | --- | --- | ---: | --- |
| BM1688 reference device | Pending repeat and soak validation | 16 channels, two detectors, 5 FPS each | 15 s | Preliminary |
| CV186X reference device | Pending repeat and soak validation | 8 channels, two detectors, 5 FPS each | 30 s | Preliminary |
| RK3576 EVB | Pending repeat and soak validation | 8 channels, two detectors, 5 FPS each | 15 s | Preliminary |

An observed boundary is the highest tested channel count that passed the configured gates. It is neither an official recommended profile nor proof of the absolute platform limit.

## Public workload

- Each channel runs person detection and safety-helmet detection concurrently.
- Each task targets 5 analysis FPS per channel.
- Input is a fixed local 1080p, 24 FPS video sample.
- Channels are added one at a time from one channel upward.
- Pass gates: minimum FPS ratio at least 80% for each task, zero missing telemetry, and average discard rate no greater than 5%.
- No preview client load was enabled; results represent background video analysis.

## Last passing points

| Platform | Channels | Person minimum FPS | Helmet minimum FPS | Average discard | Accelerator peak | CPU peak | Memory peak |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| BM1688 reference device | 16 | 4.92 | 4.90 | 0% | 60% | 61% | 46% |
| CV186X reference device | 8 | 5.00 | 5.00 | 0% | 60% | 19% | 42% |
| RK3576 EVB | 8 | 5.15 | 5.11 | 0% | 41% | 47% | 30% |

## Single-detector capacity matrix

Values are the last passing channel counts in short-run staircases. “≥” means the highest configured point passed. “*” means the next step was blocked by task binding and is not a measured performance limit.

| Platform | Single-detector workload | 24 FPS | 10 FPS | 7 FPS | 5 FPS | Status |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| BM1688 | Person detection | 7 | 15 | ≥16 | ≥16 | Preliminary |
| BM1688 | Safety-helmet detection | 7 | ≥12* | ≥16 | ≥16 | Preliminary |
| CV186X | Person detection | ≥3* | ≥8* | ≥11* | ≥15* | Preliminary |
| CV186X | Safety-helmet detection | ≥3* | ≥8* | ≥11* | ≥15* | Preliminary |
| RK3576 | Person detection | 6 | 12 | 15 | ≥16 | Preliminary |
| RK3576 | Safety-helmet detection | 5 | 10 | 12 | 15 | Preliminary |

## Comparability statement

The three platforms used platform-specific converted and validated artifacts. Internal algorithm identifiers are not public model identities and do not support a chip-performance ranking. Until model source and version, input shape, quantization, preprocessing, postprocessing, video, codec configuration, warm-up, steady-state duration, and repetition count are fully aligned, these results describe representative workloads per platform rather than a cross-chip benchmark ranking.

## Experimental VLM results

VLM results are excluded from the primary capacity claims. All three platforms used a 1→8 channel staircase with 120 seconds per step and a target of 0.1 FPS per channel. Analysis FPS was observed but excluded from PASS/FAIL; passing refers only to non-FPS gates such as discard rate, telemetry completeness, and system protection.

| Platform | Last non-FPS pass | Equivalent FPS/channel at that point | First stop | Status |
| --- | ---: | ---: | --- | --- |
| BM1688 | ≥8 | 0.040 | Highest configured point observed | Experimental |
| CV186X | ≥8 | 0.080 | Highest configured point observed | Experimental |
| RK3576 | 7 | 0.063 | Channel 8 average discard reached 22.75% | Experimental |

BM1688 and CV186X use device-provided per-channel observations. RK3576 exposes a shared task counter, so its attachment freezes the reviewed one-to-eight-channel equivalent series: `0.100 / 0.120 / 0.116 / 0.115 / 0.091 / 0.076 / 0.063 / 0.057`. VLM remains outside the formal capacity table until target-FPS, completion-count, missing-rate, and latency gates are enabled and passed.

## Test environment

The CosmoEdge 1.1 source baseline is frozen to `feat/model-guard-v2.3` commit `209bc2b52849864a15bdad91beb61f5bc982c17f`, tree `f64a98bce05b9ee8dc64dda8e56ad50f9d15687f`. It includes the RK3576 VLM inference path and performance changes used for this release line; subsequent changes at the freeze point affect Web linkage and upgrade-cache behavior plus formatting only, not inference, media, or memory lifecycle semantics.

BM1688 and CV186X were tested with the same Open package, SHA-256 `8aee0bdb146d80647b4f517114c2920781ed6760e90e5bdf951fefd982dbecb2`. The packaged `cosmo-engine` and both running engines share SHA-256 `bc7274327896384bcf68abf7fc42ce9e133f15131f3be21cb265b8e4deb55d11`. The package does not embed a source commit and predates the final source freeze; this is therefore an explicit device/package binding, not a claim that the package was reproducibly built from the final source commit. The original RK3576 package was not recovered, so those results are bound to the installed version, model identity, environment, and captured evidence rather than a package digest.

| Platform | Public device | OS | Runtime / media | Memory and storage |
| --- | --- | --- | --- | --- |
| BM1688 | BM1688 reference device | Ubuntu 22.04.5 LTS | libsophon/BMRT 0.4.12; Sophon FFmpeg/GStreamer 2.0.0 | 2,160,271,360 B system; 1,536 + 4,096 MiB accelerator heaps; 9,260,003,328 / 49,366,970,368 B system/data filesystems |
| CV186X | CV186X reference device | Ubuntu 22.04.5 LTS | libsophon/BMRT 0.4.12; Sophon FFmpeg/GStreamer 2.0.0 | 2,160,451,584 B system; 1,536 + 4,096 MiB accelerator heaps; 9,260,003,328 / 49,375,051,776 B system/data filesystems |
| RK3576 | Rockchip RK3576 EVB1 V10 | distribution not exposed by the read-only API | exact RKNN/Driver/RGA/MPP versions not exposed; Rockchip MPP/RGA media paths confirmed | 7,917 MiB shared system memory; device API reported 11.56 GB used and 2.13 GB available storage |

See `models/` for artifact identities, I/O contracts, platform-scoped repository paths, and available SHA-256 values. The byte-identical BM1688 and CV186X detector/classifier files are distributed under `data/resource/aiboxresource_bm1688/models/` and `data/resource/aiboxresource_cv186x/models/` respectively. The video SHA-256 is `ec77182a264f3059a091b68c4973942dba3b80e93f20feaf4d7e146885caf9d2`; ScenarioBench version and source-file hashes are in `release-manifest.json`. Unknown RKNN/Driver/RGA/MPP versions and the RK VLM artifact hash remain explicit unknowns rather than inferred values.

## Limitations

- Results apply only to the bound models, video, device, runtime, and package.
- The multi-task staircases are short runs; they are not official recommended channel counts until repeat and soak validation passes.
- Results are not theoretical chip compute limits.
- Different model artifacts cannot be compared directly.
- A metric with a disabled gate does not count as a performance pass.
- A task-binding failure is a blocked test, not a performance limit.
- Any environment, model, media-path, or package change requires revalidation.

## Reproduction

The public pack contains the methodology, sanitized scenarios, machine-readable results, environment templates, and checksums. Device serial numbers, internal channel IDs, internal algorithm IDs, local absolute paths, customer media, and full debugging logs remain in the private evidence archive.

GitHub's Code view displays checked-in HTML as source. The `open` links below use the same rendered documentation-site pattern as v1.0; they open as standalone reports after the documentation site is deployed and are expected to return 404 before that publication step.

The release material includes separate `summary.json`, `metrics.json`, `command.txt`, sanitized log, and HTML attachments for single-detector, dual-detector, and VLM workloads. Before execution, resolve the public model references to device-local identifiers as described in `methodology.md`; device addresses, credentials, and internal identifiers are intentionally absent.

| Platform | Single-detector staircase | Dual-detector staircase | VLM observation | Machine-readable summary |
| --- | --- | --- | --- | --- |
| BM1688 | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/single-detector/report.html) | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/dual-detector/report.html) | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/bm1688/vlm-observation/report.html) | [summary.json](results/bm1688/summary.json) |
| CV186X | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/single-detector/report.html) | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/dual-detector/report.html) | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/cv186x/vlm-observation/report.html) | [summary.json](results/cv186x/summary.json) |
| RK3576 | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/single-detector/report.html) | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/dual-detector/report.html) | [open](https://www.cosmowander.ai/docs/benchmarks/scenario-bench/v1.1/results/rk3576/vlm-observation/report.html) | [summary.json](results/rk3576/summary.json) |

## Product-release evidence boundary

- final Protected package SHA-256 and controlled build provenance;
- final RK3576 package SHA-256 and source provenance;
- SHA-256 for the final RK3576 VLM artifact;
- repeat, soak, customer-journey, and accuracy qualification before any recommended-profile claim.

These items do not block publication of this performance report. They do prevent the short-run boundary from being marketed as a recommended profile, and this report does not replace a complete product qualification report. The open detector/classifier files are present in the BM1688 and CV186X platform resource sets; the sample video and all other model binaries are not redistributed by this benchmark. Recorded SHA-256 values identify the exact artifacts.
