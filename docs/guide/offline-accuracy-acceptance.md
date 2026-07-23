# Offline Business Accuracy Acceptance

This gate compares the exported business outputs of any reference backend and
candidate backend. It is intentionally separate from tensor-consistency,
capacity, media, and stability tests. Release acceptance still requires a
versioned offline dataset prepared and reviewed by the acceptance team.

## Workspace contract

Create the directory skeleton once:

```bash
python3 tools/offline_accuracy_compare.py init \
  --output /absolute/path/to/acceptance-v1
```

The command creates these paths without copying models, videos, or credentials:

```text
acceptance-v1/
  inputs/                    model, videos, config, and labels
  results/reference/         reference per-frame JSONL
  results/candidate/         candidate per-frame JSONL
  reports/                   immutable comparison outputs
  manifest.template.json     copy to manifest.json and fill every SHA-256
```

The completed manifest fixes the dataset ID, both backend identities, model,
every video, preprocessing and postprocessing config, label table,
confidence/NMS thresholds, both result files, and their SHA-256 values.
Relative paths are resolved from the manifest. The tool rejects placeholders,
missing files, hash drift, duplicate samples, and different sample sets.

## Per-frame result format

Each result file is UTF-8 JSON Lines with one unique `sample_id` per line.
Detection uses post-NMS pixel-space `xyxy` boxes:

```json
{"sample_id":"video-01:frame=000001","task_type":"detection","detections":[{"class_id":0,"confidence":0.95,"box_xyxy":[10.0,20.0,100.0,200.0]}],"nms":{"pre_count":2,"post_count":1}}
```

Classification exports the complete probability vector:

```json
{"sample_id":"video-01:frame=000001","task_type":"classification","probabilities":[0.03,0.97],"top1":1}
```

Both exporters must use identical decode, resize, normalization, label order,
confidence threshold, and NMS configuration. The comparator performs spatial
greedy matching and reports class consistency, box IoU median/P5, confidence
error P99, unmatched/NMS differences, classifier Top-1 consistency, and
probability error P99.

## One-command comparison

After both exporters have produced and hashed their JSONL files:

```bash
python3 tools/offline_accuracy_compare.py compare \
  --manifest /absolute/path/to/acceptance-v1/manifest.json \
  --output /absolute/path/to/acceptance-v1/reports/candidate-<git-sha>
```

Exit `0` means all configured engineering gates passed, exit `1` means
valid evidence failed at least one gate, and exit `2` means the evidence is
invalid. The output contains `accuracy-report.json`, a reviewable Markdown
summary, and `SHA256SUMS`.

Default gates require detector class consistency of at least 99%, box IoU
median of at least 0.95 and P5 of at least 0.90, confidence absolute-error P99
of at most 0.05, classifier Top-1 consistency of 100%, and probability
absolute-error P99 of at most 0.02. Unmatched and post-NMS frame difference
rates default to at most 1%. Dataset owners may tighten these values. Any
relaxation must be explicitly approved and retained in release evidence.

This comparison does not replace human review of false positives, false
negatives, lighting, occlusion, site-specific behavior, or alert semantics.
