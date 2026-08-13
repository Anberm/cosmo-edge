# Runtime resource sets

CosmoEdge selects one resource set for each backend build. Sophon BM1688 and CV186X share
`aiboxresource`; the directory name is vendor-neutral even though two legacy model-directory names
retain a `prod_BM1688_` prefix.

| Target | Resource set | Open benchmark models |
| --- | --- | --- |
| BM1688 / CV186X | `aiboxresource` | `YOLOV8n V1.0.0` detector and `helmet V1.0.0` classifier |
| RK3576 | `aiboxresource_rknn` | Platform-specific RKNN resources |
| x86 | `aiboxresource_x86` | ONNX Runtime resources |

The two shared Sophon models used by the public ScenarioBench v1.1 workloads are:

| Model | Size | SHA-256 | Repository path |
| --- | ---: | --- | --- |
| YOLOV8n detector | 7,023,600 B | `56b207ef2876da76505e403a049d3c44a411b9fe707ab73dc64f1cd9d9b6c5c8` | `aiboxresource/models/prod_BM1688_6047042_YOLOV8n_V1.0.0/` |
| Helmet classifier | 6,001,416 B | `33b0fb4bcb29e41a92f9c1c518671aefc69cbf9207934deaba32ca7cd8cd7c8a` | `aiboxresource/models/prod_BM1688_7486163_helmet_V1.0.0/` |

Both hashes were verified against the exact files loaded by the CV186X benchmark device. This
binding applies only to these two artifacts and does not imply that every BM1688 model is
interchangeable with CV186X.

## 资源目录说明

BM1688 与 CV186X 的 Sophon Open 构建共用 `aiboxresource`。本次 CV186X 公开压测使用的
`YOLOV8n V1.0.0` 和 `helmet V1.0.0` 已在上表列出；它们与 CV186X 压测设备实际加载文件的
SHA-256 完全一致。目录中的 `prod_BM1688_` 是历史兼容前缀，不代表所有 BM1688 模型都可在
CV186X 上直接复用。
