# Methodology

## Scope

The 2026-08-19 refresh covers three small-model workloads:

1. person detection at 24, 10, 7, and 5 FPS;
2. no-safety-helmet analysis at 24, 10, 7, and 5 FPS;
3. person detection plus no-safety-helmet analysis at 5 FPS per task.

BM1688, CV186X, and RK3576 form the CosmoEdge 1.1 release-platform report. RV1126B is listed as an additional experimental platform.

## Fixed variables

- Source commit and tree: see `release-manifest.json`.
- Input: one fixed H.264 1920×1080, 24 FPS sample, looped locally on every channel.
- Preview load: disabled.
- Ramp: add one channel at a time.
- Hold: 30 seconds per channel-count step.
- Sampling: approximately every 3 seconds; the second half of each step is the steady window.
- Models: platform identities and hashes are in `models/`.

The no-safety-helmet workflow contains one detector node followed by one classifier node. ScenarioBench applies the requested FPS to both nodes.

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

## VLM

This refresh does not change the existing BM1688, CV186X, or RK3576 VLM evidence. Those observations are consolidated in `results/vlm-observations.json`; RV1126B has no VLM observation in this pack. Because VLM FPS gating was disabled, the data remains an experimental runtime observation and not a capacity claim.

## Reproduction

1. Verify the source, ScenarioBench, model, and sample hashes.
2. Resolve the public scenario descriptors to the device-local model and task configuration.
3. Run each case with its recorded target FPS and maximum channel count.
4. Keep raw summaries, commands, and sanitized logs in the full evidence archive; project the complete measured steps into the platform canonical case file.
5. Run `npm run benchmarks:v1.1:validate` to check case semantics, public scrub, links, deterministic report generation, and checksums.
6. Run the documentation build to generate bilingual HTML, aggregate indexes, matrices, and a checksum inventory for the built output.

When the separately held full evidence archive is available, pass it to the validator with `--archive <path>`. That optional check verifies the archive hash and compares all 49 canonical cases and three retained VLM observations with their original frozen summaries.

The Git repository contains only canonical measurements and compact public metadata. The separately hashed full evidence archive is prepared but not published. Neither form may contain a device address, credential, device serial, internal model/task identifier, channel identifier, or local absolute path.
