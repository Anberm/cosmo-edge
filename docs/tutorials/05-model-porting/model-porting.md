---
title: 第三方模型接入：转换、上传与验证
description: 确认第三方模型的支持条件，完成转换、上传、配置、运行和端到端验收。
prev:
  text: 算法编排
  link: /tutorials/04-pipeline-orchestration/pipeline-orchestration
next: false
---

# 第三方模型接入：转换、上传与验证

| 项目 | 说明 |
| --- | --- |
| 适合谁 | 需要把自有检测或分类模型接入 CosmoEdge 的算法工程师和集成开发者 |
| 完成后能做什么 | 判断模型是否满足运行条件，转换并上传模型，配置解析参数，完成图片、视频和持续运行验证 |
| 使用前提 | 已理解 Pipeline；掌握模型输入、输出、预处理、后处理和标签顺序 |
| 预计时间 | x86 ONNX 路径约 40–60 分钟；Sophon 转换路径通常需要额外 30–60 分钟 |
| 是否需要设备 | x86 路径需要 ONNX Runtime 版 CosmoEdge；Sophon 路径需要 BM1688/CV186X 设备及匹配转换工具链 |
| 最终验收结果 | 模型可加载、推理输出可解析、图片与视频结果正确，并在目标设备上持续运行无资源错误 |

第三方模型接入按以下顺序完成：

1. 确认支持条件。
2. 导出或转换模型。
3. 在转换主机上校验。
4. 上传并配置模型。
5. 先做图片推理。
6. 接入视频 Pipeline。
7. 验证解析结果和持续运行。

“文件上传成功”只证明文件被接收，不证明算子、输入形状、输出布局和后处理与 CosmoEdge
兼容。

## 1. 确认支持条件

### 1.1 当前后端和文件格式

| 目标后端 | “添加模型”接受的文件 | 模型包导入时的主文件 | 当前运行方式 | 设备条件 |
| --- | --- | --- | --- | --- |
| x86 CPU | `.onnx` | `model.onnx` | ONNX Runtime CPU | x86_64 主机和对应 CosmoEdge 构建 |
| Sophon | `.bmodel` | `model.nn` | Sophon BMRT | BM1688 或 CV186X，转换产物必须匹配芯片 |

`model.nn` 是 CosmoEdge 模型包中的内部文件名，封装的是设备侧模型；通过页面单独添加
Sophon 模型时应选择 `.bmodel`，不要把文件扩展名手工改成 `.nn`。

PyTorch `.pt`、TensorFlow SavedModel 或其他训练框架产物不能直接上传。它们必须先导出
为 ONNX；Sophon 还要使用匹配工具链把 ONNX 转为目标芯片的 `.bmodel`。

### 1.2 格式之外还要匹配的契约

| 契约 | 接入前必须知道 |
| --- | --- |
| 模型类型 | 检测、分类、关键点、特征或其他；页面子类型决定使用哪种解析器 |
| 输入 | 名称、数据类型、形状、批量、动态维度是否固定 |
| 预处理 | RGB/BGR、缩放方式、补边颜色、归一化均值和缩放系数 |
| 输出 | 张量名称、形状、维度顺序、是否已包含 NMS |
| 后处理 | 模型家族、置信度、NMS/IoU、坐标格式和最大保留数 |
| 标签 | 类别 ID 与名称的精确顺序 |
| 资源 | 模型文件大小、运行内存、并发路数和目标帧率 |
| 许可 | 模型权重、训练数据和导出工具是否允许目标使用和分发方式 |

CosmoEdge 当前已有 `YOLOV8_DET` 等解析路径，但“任意 ONNX”并不自动兼容。自定义输出、
内置 NMS、动态形状或未支持算子可能需要新的解析器或运行时代码。

### 1.3 已验证能力与条件性兼容

- **由当前代码直接支持**：x86 添加 `.onnx`、Sophon 添加 `.bmodel`，以及模型包中的
  `model.onnx` / `model.nn`。
- **仓库中已有参考证据**：YOLOv8 检测模型在 x86 ONNX 路径完成过模型导入、实时 OSD
  和事件输出。
- **仍需在目标候选版本上验证**：你的具体模型、Sophon 转换产物、性能、资源占用、
  多路并发和长期稳定性。
- **不能仅凭格式承诺**：其他 ONNX 模型家族、其他输出布局或未经验证的芯片/量化组合。

## 2. 可复现实例：x86 YOLOv8n 人员检测

本例使用固定 Ultralytics 版本导出公开的 YOLOv8n 权重，在
`data/test-video/Safety Helmet.mp4` 中检测 COCO 类别 `person`。它验证单阶段目标检测接入，
不等同于“未戴安全帽”分类任务。

### 2.1 准备固定环境和模型

参考环境：

| 项目 | 版本 |
| --- | --- |
| Python | `3.13.11` |
| Ultralytics | `8.2.84` |
| ONNX | `1.20.1` |
| ONNX Runtime | `1.26.0` |
| 导出输入 | `1 × 3 × 640 × 640` |

创建隔离环境：

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install \
  "ultralytics==8.2.84" \
  "onnx==1.20.1" \
  "onnxruntime==1.26.0"
```

下载固定发布资产并记录来源文件哈希：

```bash
curl -L \
  -o yolov8n.pt \
  https://github.com/ultralytics/assets/releases/download/v8.2.0/yolov8n.pt
sha256sum yolov8n.pt
```

macOS 可用 `shasum -a 256 yolov8n.pt`。

导出：

```bash
yolo export \
  model=yolov8n.pt \
  format=onnx \
  imgsz=640 \
  batch=1 \
  dynamic=False
sha256sum yolov8n.onnx
```

保留命令输出、Python 和包版本、源权重哈希与 ONNX 哈希。即使文件名相同，哈希不同也应
视为不同候选模型。

### 2.2 转换前检查

在仓库根目录使用统一检查脚本，让 ONNX checker 和 ONNX Runtime 完成一次零输入加载与推理：

```bash
python tools/check_onnx_model.py yolov8n.onnx
```

动态输入可重复传入 `--shape images=1,3,640,640`；需要机器可读记录时使用
`--json <输出路径>`。脚本会记录实际依赖版本和模型 SHA-256，不保存推理张量。

通过标准：

- `onnx.checker` 无错误；
- ONNX Runtime 能创建会话并执行一次推理；
- 输入是预期的 `1 × 3 × 640 × 640` 浮点张量；
- 输出形状与实际导出日志一致。

零输入测试只验证图可加载，不验证检测准确性。

### 2.3 准备模型元数据

本例采用 YOLOv8 原始检测输出：

| 配置 | 本例值 |
| --- | --- |
| 主类型 | 检测算法 |
| 子类型 | `YOLOV8_DET` |
| 输入尺寸 | `[640, 640]`，顺序按页面的“高、宽”说明 |
| 缩放 | 等比缩放并居中补边 |
| 补边色 | `114, 114, 114` |
| 颜色 | RGB |
| 归一化 | `0–1`，缩放系数约 `1/255` |
| 输出 | YOLOv8 原始检测张量，由 CosmoEdge 执行阈值和 NMS |
| 标签 | COCO 80 类原始顺序；本例在 Pipeline 中只启用 ID `0` 的 `person` |

从导出模型打印标签，避免手工重排：

```bash
python - <<'PY'
from ultralytics import YOLO
for class_id, name in YOLO("yolov8n.pt").names.items():
    print(f"{class_id}\t{name}")
PY
```

如果实际 ONNX 输出已经包含 NMS、形状不是导出记录中的 YOLOv8 原始布局，或者标签数量
不一致，应停止上传并修正导出或实现匹配解析器。

## 3. Sophon 路径：把同一 ONNX 转为 bmodel

仅在目标设备为 Sophon 时执行本节。转换工具版本、目标芯片和模型候选必须一起记录。

如果把任务交给编码智能体，先阅读[智能体辅助二次开发](/development/agent-assisted-development)。
智能体应先生成本次运行的任务契约并执行 `scripts/agent/doctor.sh`；环境满足后，推荐通过
`scripts/agent/convert_model.sh` 和 `scripts/agent/verify.sh` 留下工具链、命令、哈希与
分层证据。不要手工编造任务契约或实例记录。下面的命令仍保留为人工执行和排障参考。

先把“基础环境”和“编译器包”分开。算能的
[BM1688 TPU-MLIR 环境说明](https://doc.sophgo.com/bm1688_sdk-docs/v1.7/docs_latest_release/docs/tpu-mlir/quick_start_en/02_env.html)
要求 Ubuntu 22.04 和 Python 3.10；宿主已满足时可直接使用隔离 Python 环境，不满足时才用
`sophgo/tpuc_dev:v3.2` 作为基础环境。该镜像本身不包含完整 TPU-MLIR 编译器，只有镜像不能
证明 `model_transform.py`、`model_deploy.py` 可用。

```bash
docker pull sophgo/tpuc_dev:v3.2
docker run --rm -it \
  -v "$PWD:/workspace" \
  -w /workspace \
  sophgo/tpuc_dev:v3.2 \
  bash
```

上面的 Docker 步骤是条件性的。在宿主或容器内，另行安装并冻结 TPU-MLIR 包。本页在
2026-07-30 核对的上游发布是
[TPU-MLIR v1.28.1](https://github.com/sophgo/tpu-mlir/releases/tag/v1.28.1)，其 wheel
SHA-256 为 `28f45f878b32f3f328a09f06cc5b14a0d1b8c35169aa09f05d7dc363ee06b4c8`：

```bash
python3 -m venv .venv-tpu-mlir
source .venv-tpu-mlir/bin/activate
curl -L \
  -o tpu_mlir-1.28.1-py3-none-any.whl \
  https://github.com/sophgo/tpu-mlir/releases/download/v1.28.1/tpu_mlir-1.28.1-py3-none-any.whl
printf '%s  %s\n' \
  28f45f878b32f3f328a09f06cc5b14a0d1b8c35169aa09f05d7dc363ee06b4c8 \
  tpu_mlir-1.28.1-py3-none-any.whl | sha256sum -c -
python -m pip install './tpu_mlir-1.28.1-py3-none-any.whl[onnx]'
python -c 'from importlib.metadata import version; print(version("tpu_mlir"))'
python "$VIRTUAL_ENV/bin/model_transform.py" --help >/dev/null
python "$VIRTUAL_ENV/bin/model_deploy.py" --help >/dev/null
```

安装、拉取镜像或下载大文件都会改变环境或消耗网络，应先取得对应授权。版本更新时可以选择
其他已核验版本，但必须重新记录包版本、来源摘要和实际命令；不要把本页日期或版本扩展成所有
模型的强制要求。

下面是 BM1688/F16 的人工命令参考。显式使用当前环境的 Python 调用入口脚本，也能识别入口
文件存在但 shebang 已因环境搬迁而失效的情况：

```bash
python "$VIRTUAL_ENV/bin/model_transform.py" \
  --model_name yolov8n \
  --model_def yolov8n.onnx \
  --input_shapes '[[1,3,640,640]]' \
  --pixel_format rgb \
  --mlir yolov8n.mlir

python "$VIRTUAL_ENV/bin/model_deploy.py" \
  --mlir yolov8n.mlir \
  --quantize F16 \
  --chip bm1688 \
  --model yolov8n_bm1688_f16.bmodel

python "$VIRTUAL_ENV/bin/model_tool" --info yolov8n_bm1688_f16.bmodel
sha256sum yolov8n_bm1688_f16.bmodel
```

通过较新 ONNX 环境完成 x86 预检，不代表同一文件一定被所选 TPU-MLIR 版本接受。转换前要用
本次冻结的工具链检查实际候选的 IR、opset 和算子；不兼容时应从源权重重新导出受支持的 ONNX，
不得直接篡改模型的 `ir_version` 冒充兼容。

CV186X 必须使用支持该芯片的工具链和芯片参数，不能把 BM1688 产物上传到 CV186X 设备。
如果工具报告未支持算子、输出不一致或编译失败，转换没有完成；更换文件扩展名不能解决。

转换后校验至少包括：

- 工具链版本、芯片参数和完整命令；
- `.bmodel` 哈希；
- 工具提供的模型信息检查；
- 同一张图片在源框架、ONNX 和目标设备上的框、类别与分数对比；
- F16 或量化造成的精度差异。

智能体执行路径会把这些结果写入当前运行的 `execution-manifest.json` 和 `evidence.md`。
只有使用固定工具链完成两次真实录制并通过张量比对，记录才可进入仓库的已验证实例索引；
普通候选可以按自己的任务目标交付，但不得借用其他实例的固定形状或哈希宣称成功。

![Sophon 添加模型页面要求选择 bmodel 文件](images/img_15.webp)

## 4. 上传并配置模型

### 4.1 添加模型

1. 打开 **模型仓库**。
2. 点击 **添加模型**，不要选择用于完整模型包的“导入模型”。
3. 填写主类型、子类型、模型名称、归一化和颜色通道。
4. x86 上传 `yolov8n.onnx`；Sophon 上传对应设备的 `.bmodel`。
5. 保存。

![模型仓库中的模型列表和添加模型入口](images/img_13.webp)

![x86 环境中的 YOLOv8 ONNX 模型导入结果](../../en/tutorials/06-ultralytics-yolo-edge/images/model-import.webp)

上传完成后确认模型条目存在，并记录系统分配的模型 ID。此时还不能判定推理可用。

### 4.2 配置输入、后处理和标签

打开模型 **配置**，逐项与导出记录核对：

- 输入尺寸和缩放/补边方式；
- RGB/BGR 和归一化；
- 检测置信度和 NMS 阈值；
- 最大保留目标数；
- 类别 ID、名称和顺序；
- 页面显示的输出或高级配置。

![配置模型输入尺寸、阈值、NMS 和类别标签](images/img_17.webp)

保存后再次进入配置页，确认值已持久化。不要用截图中的 VisDrone 标签替代本例 COCO 标签。

## 5. 图片验证：先证明加载和解析

1. 新建数据源类型为 **图片分析**、任务类型为 **检测/分析** 的任务。
2. 在算法编排中只添加 **目标检测算法**。
3. 选择刚上传的 YOLOv8n，并只启用 `person` 标签。
4. 保存后打开 **图片分析**。
5. 上传一张含清晰人员的正样本和一张无人员的负样本。

![图片分析任务中的第三方目标检测节点](images/img_23.webp)

通过标准：

- 模型初始化没有报错；
- 正样本的人员框位置合理，类别显示 `person` 而不是错误 ID；
- 负样本没有大量人员误框；
- 置信度是有限且合理的数值，不是空值、NaN 或固定异常值。

![图片分析结果中的目标框、类别和置信度](images/img_28.webp)

图片验证失败时不要继续创建视频任务。

## 6. 接入视频 Pipeline

创建“YOLOv8n 人员检测验证”视频任务，使用最小链路：

1. **视频解码**
2. **目标检测算法**：选择上传的 YOLOv8n，只启用 `person`
3. **类别过滤**：保留 `person`，最小尺寸从 `60` 开始
4. **区域告警判断**：使用主区域
5. **事件上报**：首次验证保留抓拍图

![在 Pipeline 中选择第三方目标检测模型和标签](images/img_32.webp)

把任务分配给使用 `data/test-video/Safety Helmet.mp4` 的离线通道，绘制覆盖人员活动范围的
区域，使当前时间位于运行策略内，然后保存并启用。

## 7. 端到端验收

### 7.1 模型加载

- 任务从停止切换到运行，没有持续初始化错误；
- 日志显示模型加载成功；
- 内存或设备内存没有持续增长直到任务被系统终止。

### 7.2 推理输出

- 实时展示持续播放；
- 人员位置出现框；
- 类别为 `person`，分数和坐标会随画面变化；
- 无人员片段不出现大量固定框。

![第三方模型在实时展示中的检测叠加](images/img_42.webp)

### 7.3 结果解析和事件

- ROI 内人员满足规则后形成事件；
- 事件抓拍图中的框、类别、通道和时间正确；
- 事件中心能按任务和通道查询。

![事件中心中的第三方模型检测记录](images/img_45.webp)

### 7.4 持续运行

至少让离线视频完整循环一次，并在项目验收中设置明确的持续运行窗口。期间记录：

- 进程是否重启或崩溃；
- 推理耗时和实际帧率是否稳定；
- 主机内存、设备内存和磁盘是否持续增长；
- 事件是否能持续解析，而不是只成功第一帧；
- 停止并再次启动任务后能否恢复。

生产使用还必须按目标并发路数、分辨率和运行时长重新做容量与稳定性验收。本例的单路
功能成功不能证明生产容量。

## 8. 失败路径

### 上传成功但模型无法运行

1. 对比上传后文件哈希和导出产物。
2. 确认 x86 使用 ONNX、Sophon 使用目标芯片对应的 bmodel。
3. 查看第一个模型初始化错误：未支持算子、形状、文件损坏或资源不足。
4. 在转换主机重新执行 ONNX Runtime 或目标工具检查。
5. 确认 Pipeline 选择的是新模型 ID。

### 能运行但输出格式不匹配

典型表现是零目标、坐标越界、类别全部相同或置信度异常。按顺序核对：

1. 子类型是否选择正确解析器；
2. 输出名称、数量、形状和维度顺序；
3. 导出是否内置 NMS；
4. 坐标是 `xywh` 还是 `xyxy`，是否已经归一化；
5. 标签数量和顺序；
6. 预处理的 RGB/BGR、缩放、补边和归一化。

若输出契约与现有解析器不同，需要实现或适配后处理，不应通过随意改阈值掩盖。

### 资源不足

1. 停止其他模型任务，验证单模型最小链路。
2. 记录加载前后内存和设备内存。
3. 降低取帧频率不一定降低模型常驻内存；模型本身无法加载时应换更小模型或适当精度。
4. Sophon 量化或 F16 转换必须重新检查精度。
5. 按目标并发路数做容量准入，不要从单路结果线性外推。

### 图片正确但视频错误

检查视频预处理是否与图片路径一致、ROI 是否覆盖目标、取帧频率是否合理，以及跟踪、过滤
或事件规则是否删除了正确检测结果。把 Pipeline 暂时缩减到“视频解码 + 目标检测”，再
逐个恢复规则节点。

## 完成验收

- [ ] 候选模型有来源、版本、输入输出说明和 SHA-256。
- [ ] 文件格式、目标后端和设备芯片匹配。
- [ ] 转换前和转换后检查均通过。
- [ ] 模型配置与预处理、后处理和标签顺序一致。
- [ ] 正负图片样本验证通过。
- [ ] 视频 Pipeline 输出正确的框、类别和事件。
- [ ] 持续运行窗口内无崩溃、资源持续增长或解析中断。
- [ ] 验收记录绑定 CosmoEdge 版本、模型哈希、设备和配置。
