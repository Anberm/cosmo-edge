# Methodology

## Scope

The 2026-08-19 refresh covers three small-model workloads:

1. person detection at 24, 10, 7, and 5 FPS;
2. no-safety-helmet analysis at 24, 10, 7, and 5 FPS;
3. a concurrent mixed workload combining person detection with no-safety-helmet analysis at 5 FPS per business task.

BM1688, CV186X, RK3576, and RV1126B form the CosmoEdge 1.1 release-platform report.

## Fixed variables

- Source commit and tree: see `release-manifest.json`.
- Input: one fixed H.264 1920×1080, 24 FPS sample, looped locally on every channel.
- Preview load: disabled.
- Ramp: add one channel at a time.
- Hold: 30 seconds per channel-count step.
- Sampling: approximately every 3 seconds; the second half of each step is the steady window.
- Models: platform identities and hashes are in `models/`.

The no-safety-helmet workflow contains one detector node followed by one classifier node. In the concurrent mixed workload, each channel therefore contains two business tasks and three model stages: one person detector plus the detector-and-classifier helmet-analysis pipeline. ScenarioBench applies the requested FPS to both business tasks and to both stages of the helmet-analysis pipeline.

## Gates

For each complete step:

- minimum processing-FPS ratio: 0.80;
- telemetry missing rate: 0;
- average discard rate: at most 0.05;
- critical-path latency: at most 200 ms when reported;
- detector-node latency: at most 150 ms when reported.

`PASS` means the complete steady window met every enabled gate. A performance stop records the last passing step and the first failed metric. If the next channel cannot be bound, the report records the completed passing steps and labels the next step as binding-blocked. A precondition failure before step 1 is recorded as an execution block, not a measured step.

## Result selection

When both an 8-channel qualification run and a 16-channel expansion run exist:

1. use the completed expansion run for the main matrix;
2. retain the qualification run as a separate canonical case when it carries independent measurements;
3. if expansion is blocked before measurement, keep the completed qualification result;
4. if task binding fails after a steady step completed, count that completed step.

Each selected run is stored once in `results/<platform>/cases.json`. The entry contains its measured steps and the SHA-256 of the original frozen summary. Aggregate indexes, matrices, and bilingual pages are derived from those four canonical files during the documentation build.

## 72-hour dual-CV observation

The 2026-08-21 qualification uses the same controlled local-loop 1080p24 input and runs person detection plus no-safety-helmet analysis on every channel. Each channel therefore carries two business tasks across three model stages. Both tasks are configured at 5 FPS. BM1688, CV186X, and RK3576 run 8 channels and 16 business-task bindings; RV1126B runs 4 channels and 8 bindings. Preview load remains disabled.

All platforms share one 72-hour evidence window with checkpoints at 24, 48, and 72 hours. The monitor samples once per minute, giving 4320 expected samples per platform. A checkpoint is complete only when all of the following hold:

- observed samples cover at least 95% of the expected window;
- the first- and final-sample boundary lags are each at most 180 seconds;
- the maximum gap between adjacent samples is at most 180 seconds;
- the minimum observed task FPS is at least 80% of the 5 FPS target;
- observed discard is zero;
- collector-error, incomplete-binding, missing-binding, and open-critical-incident counts are zero.

CPU, memory, and disk peaks are observations, not pass/fail gates. Accelerator telemetry is excluded from the public comparison because its source units are not uniform across platforms.

The timed-restart policy saves the initial setting, forces scheduled restart off, verifies it at startup and through the run, and leaves it disabled when the observation completes. Every platform recorded 80 successful checks, with no failure or corrective write. The private monitor record also reports completed task/channel cleanup, restored layouts, zero remaining owned channels, and no cleanup errors. Because the historical monitor did not emit a separate final-state artifact, the cleanup conclusion is explicitly limited to that monitor record.

`PASS` means the configured channel profile completed the full controlled observation and met the checks above. It does not establish an exact capacity limit, a higher channel boundary, RTSP resilience, a production recommendation, or product-release qualification. The canonical public projection is stored once in `results/dual-cv-72h.json`; raw device identities, internal task/model/channel identifiers, addresses, commands, and local paths remain in private evidence referenced by SHA-256.

## VLM

The 2026-08-20 VLM refresh uses the same fixed local-loop 1080p24 input, adds one channel per step, holds each step for 60 seconds, and samples approximately every 3 seconds. Throughput is derived from each task's local completion queue instead of the shared Qwen worker-batch counter. ScenarioBench reports the newest route's completion FPS separately from the minimum across all active routes; publication evaluation uses the latter because every active route must satisfy the workload target.

BM1688 was rerun with the final readiness protocol: every newly added route had to advance its task-local completion counter and expose direct Qwen latency before formal sampling began. These readiness probes occur before the formal hold window and never enter FPS, discard, or telemetry-rate statistics. CV186X and RK3576 were measured with the immediately preceding protocol, which applied a 30-second warmup to the first route but did not probe readiness for every later route. Their first 7-channel failure remains a measured zero-tolerance missing-telemetry gate failure, but it occurred in early hold samples before the new route had completed startup. It is therefore retained as a startup-sensitive raw observation, not classified as a performance failure or a demonstrated capacity limit.

BM1688's public source hashes identify its native completed run. The CV186X and RK3576 public `sourceSummarySha256` and `sourceMetricsSha256` identify sanitized first-failure projections cut at the first non-FPS gate stop. Their device runs continued beyond that cutoff, but per the evaluation stop contract, steps after the first gate failure are not comparable and are excluded from the public staircase; the canonical source block separately freezes the original-run summary and metrics hashes.

VLM FPS gating remained disabled in the executed runs. `PASS` in the raw data means only that the enabled missing-telemetry and discard gates passed. The release pages do not rewrite that history. Instead, they apply a separate conservative publication evaluation to the contiguous prefix of steps where:

1. the minimum FPS across every active route is at least 80% of the 0.1 FPS-per-channel target; and
2. the recorded non-FPS window is complete.

The resulting performance display boundaries are BM1688 6 channels, CV186X 6 channels, and RK3576 4 channels. BM1688 first falls below the reference at 7 channels; RK3576 first falls below it at 5 channels. CV186X's 7-channel startup-sensitive window is excluded from performance judgment, so 6 remains the last complete interpretable step. This is a conservative publication display, not an official capacity, exact hardware limit, or long-running qualification claim. RV1126B has no VLM observation in this pack.

## Reproduction

1. Verify the source, ScenarioBench, model, and sample hashes.
2. Resolve the public scenario descriptors to the device-local model and task configuration.
3. Run each case with its recorded target FPS and maximum channel count.
4. Keep raw summaries, commands, and sanitized logs in private evidence; project the public measurements and their source hashes into the canonical files, including the separate 72-hour observation.
5. Run `npm run benchmarks:v1.1:validate` to check case semantics, public scrub, links, deterministic report generation, and checksums.
6. Run the documentation build to generate bilingual HTML, aggregate indexes, matrices, and a checksum inventory for the built output.

When the separately held small-model evidence archive is available, pass it to the validator with `--archive <path>`. That optional check verifies the archive hash and compares all 49 canonical small-model cases with their original frozen summaries. The refreshed VLM canonical file carries separate source-summary, source-metrics, source-tree, and tool-patch hashes.

The Git repository contains only canonical measurements and compact public metadata. The separately hashed full archive covers the small-model cases and is prepared but not published; refreshed VLM and 72-hour raw runs remain separate private evidence referenced by their source hashes. No public form may contain a device address, credential, device serial, internal model/task identifier, channel identifier, or local absolute path.
