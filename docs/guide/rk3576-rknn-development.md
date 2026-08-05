# RK3576 / RKNN integration guide

## Scope

This integration adds the RK3576 CV backend without changing the behavior of
the CPU, CUDA or Sophon backends:

- RKNN Runtime 2.3.2 executes the static-batch detector and classifier models.
- Rockchip MPP performs H.264/H.265 decode and encode.
- The decoder uses delayed Copy-out: frames are sampled or discarded before a
  host I420 copy is requested.
- RGA performs the Rockchip frame-processing operations required by preview and
  OSD paths.
- Full DMA-BUF zero-copy is not part of this engineering integration.

The currently validated engineering envelope is four channels at 5 FPS per
channel. It is not a release-capacity claim.

## Repository and evidence boundary

The repository owns product code, build definitions, unit tests, reproducible
model tooling, deployable RKNN resources and two reusable acceptance scenarios:

- `tools/scenario-bench/scenarios/rk3576-no-helmet-customer-journey`
- `tools/scenario-bench/scenarios/rk3576-no-helmet-longrun-4x5fps`

Raw device logs, metrics streams, screenshots, exported events and generated
HTML/XML/JSON reports are external validation artifacts and must not be added to
the source tree. An external evidence manifest must bind results to the source
commit and tree, final package SHA-256, device/firmware/runtime versions, model
and dataset hashes, thresholds, cleanup status and measured values.

Device addresses, account data, local backup paths and reusable credentials do
not belong in source-controlled configuration or evidence.

## Frozen toolchain identities

The machine-readable toolchain and model-input lock is
`config/rknn/toolchain-lock.json`. The current integration is based on:

- RKNN-Toolkit2 2.3.2
- RKNN Model Zoo 2.3.2
- Ubuntu 22.04 x86_64 conversion host with Python 3.10
- RK3576 Ubuntu 22.04 aarch64 target with kernel 6.1.118 and RKNPU driver 0.9.8

Changing a locked SDK, runtime, input model or preprocessing contract requires
new conversion and device evidence.

## Runtime safety boundary

Keep the board's system RKNN runtime as the rollback baseline. Package RKNN
Runtime 2.3.2 beside CosmoEdge and select it with executable RPATH or a
task-local `LD_LIBRARY_PATH`; do not overwrite `/usr/lib/librknnrt.so` during
development or acceptance. Production inference uses the native C API and does
not depend on `rknn_server`.

## Model and preprocessing contract

The first supported models are:

1. Helmet classification: `1x3x224x224`, ONNX opset 19.
2. YOLOv8 detection: `1x3x640x640`, converted to ONNX opset 19 / IR 9.

CosmoEdge owns resize, channel order and normalization. Conversion must not
bake in a second mean/std transform. CosmoEdge supplies float32 NCHW tensors;
the RKNN boundary performs one explicit NCHW-to-NHWC copy because Runtime 2.3.2
rejects NCHW on this input-conversion path. Outputs are requested as float32 so
the existing postprocessors remain authoritative.

The production YOLO candidate exposes three box/class head pairs. The
`yolov8_dfl_v1` host adapter applies DFL and sigmoid, then reconstructs the
logical `[1,84,8400]` contract. A single quantized output is not supported
because its shared scale collapses confidence precision.

## Reproducible conversion

Prepare the verified offline bundle at an operator-selected path:

```bash
./scripts/rknn/prepare_offline_env.sh "$RKNN_OFFLINE_BUNDLE"
```

The locked YOLO conversion sequence is:

```bash
python tools/rknn/convert_onnx_opset.py \
  --input model-opset22.onnx --output yolov8-opset19-ir9.onnx \
  --opset 19 --ir-version 9

python tools/rknn/extract_yolov8_heads.py \
  --input yolov8-opset19-ir9.onnx --output yolov8-heads.onnx

python tools/rknn/prepare_validation_data.py \
  --spec config/rknn/models/yolov8.json --video "$VALIDATION_VIDEO" \
  --output-dir yolov8-calibration --samples 32

python tools/rknn/convert_model.py \
  --spec config/rknn/models/yolov8.json --model yolov8-heads.onnx \
  --output yolov8-heads-int8.rknn --quantize \
  --dataset yolov8-calibration/dataset.txt
```

Calibration and numerical-parity samples are unlabeled. They do not replace a
labeled precision/recall/F1 acceptance set.

## Build and deployment

Build against a pinned RKNN runtime root. The base resource directory supplies
common actions, layouts and fonts; the RKNN resource directory supplies the
RK3576 algorithms and models.

```bash
docker run --rm \
  -v "$COSMO_EDGE_ROOT:/workspace" \
  -v "$RKNN_RUNTIME_ROOT:/opt/rknn:ro" \
  -w /workspace cosmo_dev:latest \
  bash -lc './scripts/build_rknn.sh \
    -r /opt/rknn -m data/resource/aiboxresource_x86 -t -T'
```

Keep mutable and packaged roots separate at runtime:

```bash
export COSMO_DATA_DIR=/data/cwaiuserdata
export COSMO_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data
export LD_LIBRARY_PATH="$COSMO_APP_DATA_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

`COSMO_DATA_DIR` contains configuration, databases, uploads and events.
`COSMO_APP_DATA_DIR` contains packaged resources, models, libraries and
binaries. Use the packaged launcher so transitive shared-library dependencies
resolve from the candidate being tested.

## Reusable acceptance scenarios

The customer-journey scenario runs one channel at 5 FPS for a bounded window.
Acceptance includes login, model/task/channel visibility, real raw and
algorithm HTTP-FLV playback, OSD difference, events, reconnect, stop/start
recovery and cleanup.

The long-run scenario holds four channels at 5 FPS for 12 hours. Run it with
algorithm-preview clients enabled and audit it with `--gate-hours 12`. The
runner stops when the configured disk fuse is reached. Use `--password-stdin`
so credentials do not enter process arguments.

Preview validation requires real `ffmpeg` and `ffprobe` executables. The tool
preflights them before mutating device configuration.

## Current engineering boundary

- Four channels at 5 FPS completed the 12-hour gate with CPU p95 of 54 percent,
  zero media failure/fallback deltas and stable memory-pool accounting.
- Real raw and algorithm playback, hardware decode/encode, OSD, reconnect and
  task restart recovery passed on the tested candidate.
- Delayed Copy-out discarded frames before host copies and is the selected
  optimization for this phase.
- Eight-channel operation is not an accepted capacity profile.
- RK3576 NPU utilization is currently unavailable for acceptance: the devfreq
  value can report 100 percent while the vendor per-core interface reports
  idle. Use throughput, stage latency, discard and failure counters instead.

These observations are candidate-bound and must be rerun after source, model,
runtime or package changes. Before a release claim, additionally require an
immutable package, labeled business-accuracy results, credential-safe rotated
logs, event-retention acceptance and a corrected or disabled NPU utilization
collector.
