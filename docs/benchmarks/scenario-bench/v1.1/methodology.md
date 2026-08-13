# Methodology and reproduction contract

## Scope

This package records three application workloads on BM1688, CV186X, and RK3576:

1. single-detector FPS gradients for personnel detection and safety-helmet detection;
2. two concurrent detector tasks per channel at 5 FPS;
3. an Experimental VLM runtime observation at 0.1 requested analysis FPS per channel.

The tests exercise CosmoEdge video decode, inference scheduling, post-processing, task orchestration, telemetry, and resource reporting. They are not synthetic NPU benchmarks and do not report theoretical chip throughput.

## Fixed input

Every channel loops the same 1920×1080, H.264, 24 FPS local sample. Its SHA-256 and media metadata are frozen in `release-manifest.json` and `dataset/dataset-card.md`. Preview/browser load is disabled.

## Staircase procedure

1. Warm and validate one channel.
2. Increase by one channel at each step.
3. Bind the same public workload to every active channel.
4. Hold for the duration recorded in the result attachment: 15 seconds for most BM1688/RK3576 CV steps, 30 seconds for CV186X CV steps, and 120 seconds for VLM steps.
5. Sample device and task telemetry approximately every 3 seconds for CV or 10 seconds for VLM.
6. Use the second half of each step as the stable evaluation window.
7. Stop on an enabled threshold, system protection, telemetry failure, or task-binding error.

The short CV steps discover an observed boundary; they are not soak tests. A binding error is `BLOCKED`, not a measured performance limit.

## Detector gates

- minimum per-task processing-FPS ratio: 0.80;
- maximum telemetry missing rate: 0;
- maximum average discard rate: 0.05;
- critical-path and detector-node latency gates are applied when present in the source scenario;
- local-video packet discard is not interpreted as a network-quality result.

`PASS` requires every enabled gate for the complete step. `FAIL` means at least one performance/stability gate failed. `BLOCKED` means the step did not create valid performance evidence.

## VLM interpretation

The VLM run requested 0.1 analysis FPS per channel, but the analysis-FPS ratio was intentionally disabled as a pass/fail gate. BM1688 and CV186X expose a per-channel observation directly. The RK3576 source telemetry exposes a shared task counter, so the public attachment uses the reviewed equivalent per-channel series from the corrected source report: `0.100, 0.120, 0.116, 0.115, 0.091, 0.076, 0.063, 0.057` for one through eight active channels. Raw shared-counter values are retained only in the private evidence archive. Consequently:

- `runtime-observation-pass` means only that enabled non-FPS gates passed;
- it does not mean the requested 0.1 FPS was sustained on every channel;
- it does not establish an official VLM channel capacity;
- BM1688 and CV186X reached the highest configured eight-channel observation point;
- RK3576 passed non-FPS gates through seven channels and stopped at eight because average discard reached 22.75%.

VLM can enter the formal capacity table only after enabling an FPS/completion-rate gate, sampling-miss gate, latency gate, fixed prompt/output contract, repeated runs, and a soak run.

## Recommended profile versus observed boundary

The report always separates:

- `Recommended profile`: repeated and soak-qualified configuration on the final package;
- `Observed boundary`: the last short-run step that passed the enabled gates.

This candidate has observed boundaries only. Recommended profiles remain pending.

## Reproduction

1. Check `release-manifest.json`, especially `qualification.readyToPublish` and the package-binding status.
2. Obtain the sample with `dataset/download-samples.sh`; verify the recorded SHA-256.
3. Install licensed platform model artifacts matching `models/<platform>.json` and record their SHA-256. Different model bytes produce a community reproduction, not a byte-identical rerun.
4. Resolve the public scenario descriptor to local device algorithm layouts and schedules. Keep credentials and internal IDs out of the public package.
5. Run the exact ScenarioBench version identified by the three source-file hashes in the manifest.
6. Export `summary.json`, sanitized `metrics.json`, the HTML report, the command record, and the environment identity.
7. Recalculate `SHA256SUMS` and run the validation script before publication.

Representative invocation after local resolution:

```text
node scenario-bench/src/cli.js run <resolved-scenario-directory> --output <result-directory>
```

The checked-in `command.txt` attachments explain why the public descriptor cannot contain a ready-to-run device address, credential, algorithm ID, or schedule ID.

## Publication rules

- No serial numbers, device addresses, credentials, internal channel/algorithm IDs, or local paths.
- No chip ranking unless source model, precision, preprocessing, post-processing, input, media path, timing, and repetitions are identical.
- No recommended-channel claim from a single short staircase.
- No package-qualified benchmark until the final Open/Protected package hashes and source binding are verified.
- No redistribution of models or media without a completed license/provenance decision.
