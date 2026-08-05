# RK3576 / RKNN development and acceptance guide

## Scope

The first release adds RK3576 inference for the existing static-batch CV path. It keeps CPU/FFmpeg media processing and the existing host preprocessing/postprocessing. MPP, RGA, DMA-BUF and zero-copy are explicitly outside P0-P4 and require a separate measured optimization gate.

## Frozen identities

- CosmoEdge base: `2eaf5fd7b096f98a9dca1ef298e03440484e15bc`
- TensorRT architecture reference: `57c578616ba826f01921bb4e43d7a695549b6b9e`
- RKNN-Toolkit2: `v2.3.2`, tag `42aa1d426c0a9e0869b6374edba009f7208a1926`
- RKNN Model Zoo: `v2.3.2`, tag `bad6c7334531becaf90a561988519b7bec34d0ab`
- Conversion host: Ubuntu 22.04 x86_64, Python 3.10
- Target: RK3576 Ubuntu 22.04 aarch64, kernel 6.1.118, RKNPU driver 0.9.8

The machine-readable lock is `config/rknn/toolchain-lock.json`. A candidate is not accepted when any locked identity is missing from its evidence manifest.

## Runtime safety boundary

The target's existing RKNN Runtime 2.1.0 is retained as the rollback baseline. The 2.3.2 runtime is packaged beside CosmoEdge and selected with executable RPATH or a task-local `LD_LIBRARY_PATH`; it must not replace `/usr/lib/librknnrt.so` during P0-P4. Production inference uses the native C API and does not depend on `rknn_server`.

The baseline backup on the current test box is:

```text
/home/moons/cosmo-rk3576-baseline-20260804
```

## Offline conversion host

Copy the verified bundle to the conversion host and run:

```bash
./scripts/rknn/prepare_offline_env.sh /home/yuyu/workspace/rk3576-offline-bundle
```

The script verifies every artifact before creating `.venv-rknn-2.3.2`. No network index is used.

## Model order and preprocessing ownership

1. Helmet classification (`1x3x224x224`, ONNX opset 19) is the first vertical slice.
2. YOLOv8 detection (`1x3x640x640`) follows after conversion to ONNX opset 19 / IR 9. The repository copy is opset 22 and is not a direct RKNN conversion input.

For P0-P4, CosmoEdge owns resize, channel order and normalization. RKNN conversion must not bake a second mean/std transform into the model. CosmoEdge produces float32 NCHW tensors; the RKNN boundary performs one explicit NCHW-to-NHWC copy because Runtime 2.3.2 rejects NCHW on its input-conversion path. RKNN then converts the NHWC float values to the model's native FP16 or INT8 input. Outputs are requested as float32 so existing postprocessors remain authoritative.

The direct single-output YOLO graph is not the production candidate. A quantized `[1,84,8400]` output shares one scale between coordinates and probabilities, which collapses confidence values to zero. It also keeps DFL box decoding on the NPU and showed avoidable FP16 box drift. `extract_yolov8_heads.py` therefore exposes the three box/class head pairs. The host-side `yolov8_dfl_v1` adapter applies DFL, sigmoid and concatenation back to the logical `[1,84,8400]` contract. Both FP16 and INT8 candidates use this six-output runtime contract.

## Reproducible model conversion

The locked conversion sequence on the Ubuntu host is:

```bash
python tools/rknn/convert_onnx_opset.py \
  --input model-opset22.onnx --output yolov8-opset19-ir9.onnx \
  --opset 19 --ir-version 9

python tools/rknn/extract_yolov8_heads.py \
  --input yolov8-opset19-ir9.onnx --output yolov8-heads.onnx

python tools/rknn/prepare_validation_data.py \
  --spec config/rknn/models/yolov8.json --video "Safety Helmet.mp4" \
  --output-dir yolov8-calibration --samples 32

python tools/rknn/convert_model.py \
  --spec config/rknn/models/yolov8.json --model yolov8-heads.onnx \
  --output yolov8-heads-int8.rknn --quantize \
  --dataset yolov8-calibration/dataset.txt
```

Helmet calibration uses person crops selected by the opset-19 detector. The generated manifests explicitly mark the 32-sample sets as unlabeled and suitable for representative calibration and numerical parity only. They are not accuracy benchmarks.

## P2 accepted candidates

The accepted board evidence uses RKNN Runtime 2.3.2 with RKNPU driver 0.9.8 and three fixed video samples. Exact metrics and hashes are recorded in `docs/evidence/rk3576/p2-model-validation.json`.

| Candidate | SHA-256 | Result | Mean of per-sample NPU means |
| --- | --- | --- | ---: |
| Helmet FP16 | `3207c64c848d0c249e8f37b47bdfe28325242a99dc2abf30edeca2647dda7205` | 3/3 top-1 match | 4.98 ms |
| Helmet INT8 | `471d1de315fd142d696066093eaf13e9657b61789c802cfe880606e209cd67ea` | 3/3 top-1 match | 4.09 ms |
| YOLOv8 heads FP16 | `68ddafae738e37791a5e65481deaa4a5902ceffad1cebc28b64907c87268045b` | 3/3 Pedestrian detection parity, F1 1.0 | 37.09 ms |
| YOLOv8 heads INT8 | `26ed82e076b06bf0bd757286cafd2bff7ae957366640d96559bf3215a6d541e0` | 3/3 Pedestrian detection parity, F1 1.0 | 26.46 ms |

These timings are P2 model probes, not P4 performance acceptance. Frequency scaling was not controlled and the sample set has no ground-truth labels. P4 must measure the complete CosmoEdge pipeline and run a labeled business-accuracy gate before production release.

## RKNN build and deployment boundary

Build the aarch64 package from the pinned runtime root. The base resource directory supplies common actions/layout/fonts, while the RKNN overlay replaces only RK3576-specific algorithms and models:

```bash
docker run --rm \
  -v /home/yuyu/workspace/cosmo-edge-rk3576:/workspace \
  -v /home/yuyu/workspace/rk3576-offline-bundle/runtime:/opt/rknn:ro \
  -w /workspace cosmo_dev:latest \
  bash -lc './scripts/build_rknn.sh \
    -r /opt/rknn -m data/resource/aiboxresource_x86 -t -T'
```

The writable and read-only roots must remain distinct at runtime:

```bash
export COSMO_DATA_DIR=/data/cwaiuserdata
export COSMO_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data
export LD_LIBRARY_PATH="$COSMO_APP_DATA_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

`COSMO_DATA_DIR` holds configuration, databases, uploads, events and other mutable state. `COSMO_APP_DATA_DIR` is the installed package root containing `resource/`, `files/`, `font/`, `lib/` and `bin/`. Pointing both variables at the writable directory starts the server but hides all packaged preset algorithms and models. `scripts/run_start.sh` therefore exports the actual `INSTALLPATH` as the default application root and preserves an explicit data-root override for side-by-side validation.

The package contains RKNN Runtime 2.3.2 and resolves `librknnrt.so` from its adjacent `lib/` directory. The launcher also sets `LD_LIBRARY_PATH` because other packaged shared libraries have transitive dependencies. Do not invoke the installed engine outside the launcher environment when qualifying a release.

## P3 accepted package

P3 is bound to `cosmo-V1.0.0-b3fc2e6fb7fc446a47379946b020f1ba.tar.gz`, SHA-256 `7b5a55f74345479d33a770c34a9dd8c80d2dcf32ceb73b39aebe993cc930e018`.

- 36 targeted RKNN/deployment tests and 115 assertions pass on the RK3576 box.
- The packaged Helmet output is byte-identical to the P2 reference.
- The C++ six-head YOLOv8 adapter matches the Python reference with cosine similarity `0.9999999999999929` and maximum absolute error `0.0001220703125`.
- The authenticated image journey creates the two-model task, analyzes three 1080p JPEGs, returns annotated JPEGs and 2/1/3 targets, then releases both RKNN contexts without lifecycle warnings.
- Device identity, NPU/shared-memory metrics, both model records and the `RK3576 Helmet Image Analysis` selector are visible through the management APIs. Chrome validation used a package with an identical web-tree SHA-256; the final package's candidate-bound backend APIs were revalidated separately.

Machine-readable results and the target evidence root are recorded in `docs/evidence/rk3576/p3-backend-validation.json`.

## Acceptance gates

- P0: source, SDK, models and device baseline are locked and recoverable.
- P1: the offline environment imports RKNN-Toolkit2 and a 2.3.2 C API probe loads on the board without changing system libraries.
- P2: FP16 and INT8 artifacts include converter logs, hashes, preprocessing contract and CPU/RKNN comparison evidence.
- P3: the RKNN backend, model import, device identity, metrics and side-by-side package are integrated and unit-tested.
- P4: file/video and RTSP journeys, task persistence, alarms, 1/2/4/8-channel measurements, restart recovery and 24/72-hour soak evidence pass the candidate-specific thresholds.

Vendor model-only FPS is not an end-to-end CosmoEdge acceptance metric. Report decode, preprocessing, NPU run, postprocessing, queue/discard, memory, temperature and NPU utilization separately.
