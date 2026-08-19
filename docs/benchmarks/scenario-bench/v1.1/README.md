# CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark

> Person detection, no-safety-helmet analysis, and concurrent-task results on BM1688, CV186X, and RK3576. RV1126B is included as an additional experimental platform.

Entry points: [English report](report.html) · [中文报告](report.zh-CN.html) · [methodology](methodology.md) · [machine-readable index](results/index.json)

## Concurrent-task results

Each channel runs person detection and no-safety-helmet analysis concurrently at 5 FPS per task.

| Platform | Passing channels | Task bindings | Hold per step |
| --- | ---: | ---: | ---: |
| BM1688 | ≥16 | 32/32 | 30 s |
| CV186X | ≥8 | 16/16 | 30 s |
| RK3576 | ≥8 | 16/16 | 30 s |
| RV1126B (additional experimental platform) | ≥4 | 8/8 | 30 s |

## Single-task capacity matrix

Values are the last passing channel count. `≥` means the highest configured count passed. `*` means the next channel was blocked during task binding. `†` means the expansion run was blocked by its storage precondition before measurement.

| Platform | Task | 24 FPS | 10 FPS | 7 FPS | 5 FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| BM1688 | Person detection | ≥8 | ≥16 | ≥16 | ≥16 |
| BM1688 | No-safety-helmet analysis | ≥7* | ≥14* | ≥16 | ≥16 |
| CV186X | Person detection | ≥8* | ≥15* | ≥16 | ≥16 |
| CV186X | No-safety-helmet analysis | 6 | ≥13* | ≥16 | ≥16 |
| RK3576 | Person detection | 6 | 12 | ≥16 | ≥8† |
| RK3576 | No-safety-helmet analysis | 6 | 10 | 12 | ≥16 |

### Additional RV1126B results

| Task | 24 FPS | 10 FPS | 7 FPS | 5 FPS |
| --- | ---: | ---: | ---: | ---: |
| Person detection | 2 | ≥4 | ≥4 | ≥4 |
| No-safety-helmet analysis | 2 | ≥4 | ≥4 | ≥4 |

## Controlled setup

- CosmoEdge source: `89c73a7464a81ef378686447d7c1eeb88b988455`, tree `6857fbcce72c7af64e6cb23a27e66a405e9df9af`.
- Input: fixed local-loop H.264 1920×1080, 24 FPS sample, SHA-256 `3e1c5b97cd5bcc081e47ec631f84c36e72f075c8b9da6a19de3d9705fb887f92`.
- Add one channel per step; hold 30 seconds; sample about every 3 seconds; preview disabled.
- Gates: 80% FPS compliance, zero telemetry missing rate, and at most 5% average discard.
- The no-safety-helmet task is a two-stage detector-plus-classifier pipeline; both nodes receive the same target FPS.

BM1688 and CV186X use byte-identical detector/classifier artifacts. RK3576 and RV1126B use platform-specific RKNN artifacts with the same public I/O contracts. Full hashes are in the [model card](models/model-card.md).

## Independent reports

The refresh contains 49 small-model case attachments. Every case includes bilingual HTML, `summary.json`, `metrics.json`, `command.txt`, and a sanitized `test.log`.

| Platform | Platform summary | All cases | Single-task summary | Concurrent-task summary | Existing VLM |
| --- | --- | --- | --- | --- | --- |
| BM1688 | <a href="./results/bm1688/report.html">open</a> | <a href="./results/bm1688/cases/report.html">open</a> | <a href="./results/bm1688/single-detector/report.html">open</a> | <a href="./results/bm1688/dual-detector/report.html">open</a> | <a href="./results/bm1688/vlm-observation/report.html">open</a> |
| CV186X | <a href="./results/cv186x/report.html">open</a> | <a href="./results/cv186x/cases/report.html">open</a> | <a href="./results/cv186x/single-detector/report.html">open</a> | <a href="./results/cv186x/dual-detector/report.html">open</a> | <a href="./results/cv186x/vlm-observation/report.html">open</a> |
| RK3576 | <a href="./results/rk3576/report.html">open</a> | <a href="./results/rk3576/cases/report.html">open</a> | <a href="./results/rk3576/single-detector/report.html">open</a> | <a href="./results/rk3576/dual-detector/report.html">open</a> | <a href="./results/rk3576/vlm-observation/report.html">open</a> |
| RV1126B | <a href="./results/rv1126b/report.html">open</a> | <a href="./results/rv1126b/cases/report.html">open</a> | <a href="./results/rv1126b/single-detector/report.html">open</a> | <a href="./results/rv1126b/dual-detector/report.html">open</a> | — |

This refresh updates small-model results only; the existing VLM attachments for the three release platforms remain unchanged.

## Reproduction files

- [release-manifest.json](release-manifest.json): source, tool, input, and platform identities.
- [methodology.md](methodology.md): procedure and result interpretation.
- [scenarios](scenarios/README.md): sanitized public workload descriptors.
- <a href="./SHA256SUMS">SHA256SUMS</a>: hashes for the public pack.
