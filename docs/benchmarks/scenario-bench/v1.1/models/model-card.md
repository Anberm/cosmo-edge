# Model identity and redistribution record

This benchmark uses platform-specific target artifacts. They implement similar public workloads, but they are not proven to be byte-equivalent conversions of one frozen source checkpoint. The report therefore describes typical workload capacity and does not rank chips.

| Platform | Public model | Version | Input contract | Target artifact identity | Redistribution |
| --- | --- | --- | --- | --- | --- |
| BM1688 | YOLOv8n detector | V1.0.0 | `1x3x640x640`, RGB, `1/255`, letterbox 114 | SHA-256 `56b207ef…b6c5c8` | Not included; preset model |
| BM1688 | Helmet classifier | V1.0.0 | `1x3x224x224`, RGB, `1/255` | SHA-256 `33b0fb4b…cd7c8a` | Not included; preset model |
| BM1688 | CosmoEdge VL Judge 0.8B | V1.0.0 | image plus text prompt/tokenizer | SHA-256 `31a03f48…4bd767` | Not included; preset model |
| CV186X | Pedestrian detector | V8.0.1 | `1x3x640x640` | SHA-256 `50dfb31d…33f080` | Not included; preset model |
| CV186X | Pedestrian helmet classifier | V6.0.1 | `1x3x224x224` | SHA-256 `851a7467…c68810` | Not included; preset model |
| CV186X | Qwen 0.8B device VLM | V1.0.0 | image plus text prompt/tokenizer | SHA-256 `8d258ab0…f76f1b` | Not included; user-installed model |
| RK3576 | YOLOv8 detector | V1.0.0 | `1x3x640x640`, RGB, `1/255`, letterbox 114 | SHA-256 `26ed82e0…541e0` | Not included pending license decision |
| RK3576 | Helmet classifier | V1.0.0 | `1x3x224x224`, RGB, `1/255` | SHA-256 `471d1de3…d67ea` | Not included pending license decision |
| RK3576 | Qwen 0.8B RKLLM VLM | V1.0.0 | image plus text prompt/tokenizer | Device catalog identity only; file SHA unavailable | Not included |

Full hashes and per-platform notes are in `bm1688.json`, `cv186x.json`, and `rk3576.json`.

## Accuracy scope

The present package contains capacity and stability evidence, not a business-accuracy qualification. Precision, recall, F1, fixed-recall false-positive rate, source-to-target drift, hard-case evaluation, and production dynamic-set results must be published separately before claiming model accuracy.

## Reproduction rule

Model binaries are deliberately not redistributed here. A reproducer must install a licensed artifact matching the public input/output contract and record its own SHA-256. If the hash differs from this card, the run is a community reproduction and must not be presented as a byte-identical rerun of the release evidence.
