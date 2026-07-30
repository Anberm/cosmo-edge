---
title: "Third-Party Model Integration: Convert, Upload, and Validate"
description: Confirm support conditions for a third-party model, then convert, upload, configure, run, and accept it end to end.
prev:
  text: Pipeline Orchestration
  link: /en/tutorials/04-pipeline-orchestration/pipeline-orchestration
next: false
---

# Third-Party Model Integration: Convert, Upload, and Validate

| Item | Details |
| --- | --- |
| Who this is for | ML engineers and integration developers bringing a custom detector or classifier to CosmoEdge |
| What you will accomplish | Evaluate runtime compatibility, convert and upload a model, configure parsing, and complete image, video, and sustained-run validation |
| Prerequisites | Understand Pipelines and know the model input, output, preprocessing, postprocessing, and label order |
| Estimated time | About 40–60 minutes for x86 ONNX; Sophon conversion commonly adds 30–60 minutes |
| Device required | x86 requires an ONNX Runtime CosmoEdge build; Sophon requires a BM1688/CV186X device and matching conversion toolchain |
| Final acceptance result | The model loads, its output is parsed correctly, image and video results pass, and it runs without resource failure on the target device |

Complete third-party integration in this order:

1. Confirm support conditions.
2. Export or convert the model.
3. Validate it on the conversion host.
4. Upload and configure it.
5. Run image inference first.
6. Connect it to a video Pipeline.
7. Validate parsing and sustained operation.

“Upload succeeded” proves only that the file was accepted. It does not prove that operators, input shape,
output layout, and postprocessing are compatible with CosmoEdge.

## 1. Confirm Support Conditions

### 1.1 Current Backends and File Formats

| Target backend | File accepted by Add Model | Main file in an imported model package | Current runtime | Device condition |
| --- | --- | --- | --- | --- |
| x86 CPU | `.onnx` | `model.onnx` | ONNX Runtime CPU | x86_64 host and matching CosmoEdge build |
| Sophon | `.bmodel` | `model.nn` | Sophon BMRT | BM1688 or CV186X; the artifact must target the actual chip |

`model.nn` is the internal file name in a CosmoEdge model package. It wraps the device model. When adding
an individual Sophon model in the UI, select its `.bmodel`; do not rename an extension to `.nn`.

PyTorch `.pt`, TensorFlow SavedModel, and other training-framework artifacts cannot be uploaded directly.
Export them to ONNX first. Sophon deployments then convert ONNX into a chip-specific `.bmodel`.

### 1.2 Contracts Beyond the File Format

| Contract | Required information |
| --- | --- |
| Model type | Detection, classification, keypoint, feature, or another type; the UI subtype selects a parser |
| Input | Name, type, shape, batch, and whether dynamic dimensions are fixed |
| Preprocessing | RGB/BGR, resize, padding color, normalization mean, and scale |
| Output | Tensor names, shapes, dimension order, and whether NMS is built in |
| Postprocessing | Model family, confidence, NMS/IoU, coordinate format, and maximum results |
| Labels | Exact class-ID and class-name order |
| Resources | File size, runtime memory, channel concurrency, and target frame rate |
| License | Whether model weights, training data, and export tools permit the intended use and distribution |

CosmoEdge currently includes parsers such as `YOLOV8_DET`, but “any ONNX file” is not automatically
compatible. Custom output, built-in NMS, dynamic shape, or unsupported operators may require a new parser
or runtime code.

### 1.3 Verified Capability vs Conditional Compatibility

- **Directly supported by current code**: Add `.onnx` on x86, add `.bmodel` on Sophon, and import packages
  containing `model.onnx` or `model.nn`.
- **Reference evidence in this repository**: a YOLOv8 detector has completed x86 ONNX import, live OSD,
  and event output.
- **Still required on the target candidate**: validate your exact model, Sophon artifact, performance,
  resource usage, concurrency, and long-term stability.
- **Not promised from format alone**: other ONNX model families, other output layouts, and untested
  chip/quantization combinations.

## 2. Reproducible Example: YOLOv8n Person Detection on x86

This example exports the public YOLOv8n weights with fixed Ultralytics packages and detects COCO class
`person` in `data/test-video/Safety Helmet.mp4`. It validates single-stage detection, not the separate
No Safety Helmet classification task.

### 2.1 Prepare a Fixed Environment and Model

Reference environment:

| Item | Version |
| --- | --- |
| Python | `3.13.11` |
| Ultralytics | `8.2.84` |
| ONNX | `1.20.1` |
| ONNX Runtime | `1.26.0` |
| Export input | `1 × 3 × 640 × 640` |

Create an isolated environment:

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install \
  "ultralytics==8.2.84" \
  "onnx==1.20.1" \
  "onnxruntime==1.26.0"
```

Download the pinned release asset and record the source hash:

```bash
curl -L \
  -o yolov8n.pt \
  https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n.pt
sha256sum yolov8n.pt
```

On macOS, use `shasum -a 256 yolov8n.pt`.

Export:

```bash
yolo export \
  model=yolov8n.pt \
  format=onnx \
  imgsz=640 \
  batch=1 \
  dynamic=False
sha256sum yolov8n.onnx
```

Keep the command output, Python and package versions, source-weight hash, and ONNX hash. Two files with the
same name but different hashes are different model candidates.

### 2.2 Pre-Conversion Checks

From the repository root, run the shared checker for ONNX validation and one zero-input inference:

```bash
python tools/check_onnx_model.py yolov8n.onnx
```

For dynamic inputs, repeat `--shape images=1,3,640,640`. Add `--json <output-path>` for a
machine-readable record. The script records dependency versions and the model SHA-256 without saving
inference tensors.

Pass criteria:

- `onnx.checker` reports no error;
- ONNX Runtime creates a session and performs one inference;
- input is the expected `1 × 3 × 640 × 640` float tensor;
- output shapes match the export log.

A zero-input test verifies loading, not detection accuracy.

### 2.3 Prepare Model Metadata

This example uses raw YOLOv8 detection output:

| Configuration | Example value |
| --- | --- |
| Main type | Detection |
| Subtype | `YOLOV8_DET` |
| Input size | `[640, 640]`, following the UI's height/width order |
| Resize | Keep aspect ratio and center-pad |
| Padding color | `114, 114, 114` |
| Color | RGB |
| Normalization | `0–1`, scale approximately `1/255` |
| Output | Raw YOLOv8 detection tensor; CosmoEdge applies thresholds and NMS |
| Labels | Original COCO 80-class order; only ID `0`, `person`, is enabled in the example Pipeline |

Print labels from the source model instead of reordering them manually:

```bash
python - <<'PY'
from ultralytics import YOLO
for class_id, name in YOLO("yolov8n.pt").names.items():
    print(f"{class_id}\t{name}")
PY
```

Stop and correct the export or implement a matching parser if the ONNX output already contains NMS, does
not use the expected raw YOLOv8 layout, or has a different label count.

## 3. Sophon Path: Convert the Same ONNX to bmodel

Run this section only for a Sophon target. Record the conversion tool version, target chip, and model
candidate together.

When delegating the task to a coding agent, first read
[Agent-Assisted Development](/en/development/agent-assisted-development). The agent should generate the
run-local task contract and run `scripts/agent/doctor.sh`. Once admitted, the recommended path is
`scripts/agent/convert_model.sh` followed by `scripts/agent/verify.sh`, which records the toolchain,
commands, hashes, and layered evidence. Do not handcraft task contracts or example records. The manual
commands below remain useful for direct execution and troubleshooting.

The repository's existing F16 reference toolchain uses:

```bash
curl -L \
  -o sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz \
  https://sophon-file.sophon.cn/sophon-prod-s3/drive/24/06/14/12/sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
sha256sum sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
docker load -i sophgo-tpuc_dev-v3.2_191a433358ad.tar.gz
docker run --rm -it \
  -v "$PWD:/workspace" \
  sophgo/tpuc_dev:v3.2 \
  bash
```

Inside the container:

```bash
cd /workspace
model_transform \
  --model_name yolov8n \
  --model_def yolov8n.onnx \
  --input_shapes '[[1,3,640,640]]' \
  --pixel_format rgb \
  --mlir yolov8n.mlir

model_deploy \
  --mlir yolov8n.mlir \
  --quantize F16 \
  --chip bm1688 \
  --model yolov8n_bm1688_f16.bmodel

sha256sum yolov8n_bm1688_f16.bmodel
```

CV186X requires a toolchain and chip option that support CV186X. A BM1688 artifact cannot be used on a
CV186X device. Unsupported operators, output mismatches, or compilation errors mean conversion failed;
renaming the extension does not fix them.

Post-conversion evidence must include:

- toolchain version, chip option, and full command;
- `.bmodel` hash;
- model inspection with the conversion tool;
- box, class, and score comparison across the source framework, ONNX, and target device on the same image;
- any F16 or quantization accuracy difference.

The agent path writes these results to the current run's `execution-manifest.json` and `evidence.md`.
An entry can join the verified-example index only after two real recordings with a fixed toolchain and
passing tensor comparison. A normal candidate can still be delivered against its own task acceptance,
but it must not borrow another example's fixed shapes or hashes as proof.

![The Sophon Add Model page requiring a bmodel file](images/img_15.webp)

## 4. Upload and Configure the Model

### 4.1 Add the Model

1. Open **Model Repository**.
2. Select **Add Model**, not **Import Model**, which is for a complete package.
3. Enter main type, subtype, model name, normalization, and color channel.
4. Upload `yolov8n.onnx` on x86 or the chip-specific `.bmodel` on Sophon.
5. Save.

![The model list and Add Model entry in Model Repository](images/img_13.webp)

![The imported YOLOv8 ONNX model in an x86 environment](../06-ultralytics-yolo-edge/images/model-import.webp)

Record the model ID assigned by the system. The model is not yet proven runnable.

### 4.2 Configure Input, Postprocessing, and Labels

Open **Configure** and compare every item with the export record:

- input size and resize/padding;
- RGB/BGR and normalization;
- confidence and NMS thresholds;
- maximum retained targets;
- class IDs, names, and order;
- output or advanced settings shown by the page.

![Configuring input size, confidence, NMS, and class labels](images/img_17.webp)

Save and reopen the page to prove persistence. Do not reuse the VisDrone labels shown in the screenshot for
this COCO example.

## 5. Image Validation: Prove Loading and Parsing First

1. Create a **Detection / Analysis** task with **Image Analysis** as its data source.
2. Select **Arrange Algorithm** and add only **Object Detection**.
3. Select the uploaded YOLOv8n and enable only the `person` label.
4. Save and open **Image Analysis**.
5. Upload one clear positive image containing a person and one negative image without a person.

![A third-party Object Detection node in an image-analysis task](images/img_23.webp)

Pass criteria:

- model initialization reports no error;
- the positive image has a reasonably placed box labeled `person`, not an incorrect ID;
- the negative image does not produce many person boxes;
- confidence values are finite and plausible, not empty, NaN, or a fixed abnormal value.

![Boxes, classes, and confidence in an image-analysis result](images/img_28.webp)

Do not proceed to video while image validation fails.

## 6. Connect the Model to a Video Pipeline

Create a “YOLOv8n Person Detection Validation” video task with this minimum chain:

1. **Video Decode**
2. **Object Detection**: select the uploaded YOLOv8n and enable only `person`
3. **Category Filter**: keep `person`, starting with **Min Pedestrian Size** at `60`
4. **Region Alarm**: use the main area
5. **Event Report**: retain at least a snapshot for the first test

![Selecting the third-party detector and labels in a Pipeline](images/img_32.webp)

Assign the task to an offline channel using `data/test-video/Safety Helmet.mp4`, draw a region over the
people's movement area, include the current time in the running strategy, save, and enable the service.

## 7. End-to-End Acceptance

### 7.1 Model Loading

- The task moves from stopped to running without a repeating initialization error.
- Logs report that the model loaded.
- Host or device memory does not grow until the task is terminated.

### 7.2 Inference Output

- Live Display plays continuously.
- People receive boxes at the expected positions.
- The class is `person`, and scores and coordinates vary with the image.
- Segments without people do not show many fixed boxes.

![Third-party model overlays in Live Display](images/img_42.webp)

### 7.3 Parsing and Events

- A person inside the ROI creates an event after the rule is satisfied.
- The event snapshot has the correct box, class, channel, and time.
- Event Center can query by task and channel.

![Third-party model detection records in Event Center](images/img_45.webp)

### 7.4 Sustained Operation

Run at least one full loop of the offline video and define a longer project-specific soak window. Record:

- process restart or crash;
- stable inference time and effective frame rate;
- host memory, device memory, and disk growth;
- continued parsing beyond the first frame;
- recovery after stopping and starting the task.

Production use must repeat capacity and stability acceptance at the target channel count, resolution, and
duration. A single-channel functional pass does not prove production capacity.

## 8. Failure Paths

### Upload Succeeds but the Model Cannot Run

1. Compare the uploaded-file hash with the export artifact.
2. Confirm ONNX for x86 and a bmodel for the exact Sophon chip.
3. Find the first model-initialization error: unsupported operator, shape, corruption, or insufficient memory.
4. Repeat ONNX Runtime or target-tool validation on the conversion host.
5. Confirm that the Pipeline selects the new model ID.

### The Model Runs but Output Parsing Is Wrong

Typical symptoms are no targets, out-of-range coordinates, one class for every target, or abnormal
confidence. Check:

1. subtype and parser;
2. output names, counts, shapes, and dimension order;
3. whether export built NMS into the graph;
4. `xywh` versus `xyxy` and normalized versus pixel coordinates;
5. label count and order;
6. RGB/BGR, resize, padding, and normalization.

If the output contract differs from an existing parser, implement or adapt postprocessing instead of hiding
the mismatch with arbitrary thresholds.

### Resources Are Insufficient

1. Stop other model tasks and test the minimum single-model chain.
2. Record memory before and after loading.
3. Lower frame rate only reduces work; it may not reduce resident model memory. Use a smaller model or
   suitable precision when the model itself cannot load.
4. Recheck accuracy after Sophon F16 or quantization.
5. Admit capacity at the target channel count instead of extrapolating linearly from one channel.

### Images Work but Video Fails

Check whether video preprocessing matches the image path, whether the ROI covers the target, whether frame
sampling is reasonable, and whether tracking, filtering, or event rules remove correct detections. Reduce
the Pipeline temporarily to **Video Decode + Object Detection**, then restore rule nodes one at a time.

## Acceptance Checklist

- [ ] The candidate has a source, version, input/output record, and SHA-256.
- [ ] File format, target backend, and device chip match.
- [ ] Pre-conversion and post-conversion checks pass.
- [ ] Model configuration matches preprocessing, postprocessing, and label order.
- [ ] Positive and negative image samples pass.
- [ ] The video Pipeline outputs correct boxes, classes, and events.
- [ ] The soak window has no crash, unbounded resource growth, or parsing interruption.
- [ ] Evidence is bound to the CosmoEdge version, model hash, device, and configuration.
