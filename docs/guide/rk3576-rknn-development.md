---
title: RK3576 / RKNN 集成指南
description: Rockchip RK3576 稳定版的构建、运行时、模型和验证边界。
prev:
  text: 构建指南
  link: /guide/build
next:
  text: macOS Docker Preview
  link: /guide/macos-docker-preview
---

# RK3576 / RKNN 集成指南

## 能力范围

RK3576 集成增加了面向生产的 CV 后端，不改变 CPU、CUDA 或 Sophon 后端的行为：

- RKNN Runtime 2.3.2 执行静态 batch 的检测和分类模型。
- Rockchip MPP 执行 H.264/H.265 解码与编码。
- 解码器使用延迟 Copy-out：先对帧进行采样或丢弃，再按需复制宿主机 I420 数据。
- RGA 执行预览与 OSD 路径所需的 Rockchip 图像处理操作。
- 完整 DMA-BUF 零拷贝不属于当前稳定版支持边界。

推荐部署起点为已完成 12 小时验证的 4 路 × 5 FPS 单算法配置。最新短时阶梯中，
单算法覆盖到 16 路 × 5 FPS，双算法覆盖到 8 路 × 每任务 5 FPS；这些是指定模型与
门禁下的实测边界，不直接替代推荐配置。详见 [ScenarioBench v1.1](/benchmarks/scenario-bench/v1.1/report.zh-CN.html)。

## 仓库与证据边界

仓库负责产品代码、构建定义、单元测试、可复现模型工具、可部署 RKNN 资源以及
两个可复用验收场景：

- `tools/scenario-bench/scenarios/rk3576-no-helmet-customer-journey`
- `tools/scenario-bench/scenarios/rk3576-no-helmet-longrun-4x5fps`

板端原始日志、指标流、截图、导出事件以及生成的 HTML/XML/JSON 报告属于外部
验证产物，不应加入源码树。发布证据 manifest 应绑定源码 commit 与 tree、最终
安装包 SHA-256、设备/固件/运行时版本、模型与数据集哈希、阈值、清理状态和实测值。

设备地址、账号数据、本地备份路径和可复用凭据不得进入版本控制配置或证据。

## 固定工具链标识

机器可读的工具链与模型输入锁文件为 `config/rknn/toolchain-lock.json`。当前支持
的集成基于：

- RKNN-Toolkit2 2.3.2
- RKNN Model Zoo 2.3.2
- Ubuntu 22.04 x86_64 转换主机与 Python 3.10
- RK3576 Ubuntu 22.04 aarch64 目标机、内核 6.1.118、RKNPU 驱动 0.9.8

修改已锁定的 SDK、运行时、输入模型或预处理约定后，必须重新生成转换和板端证据。

## 运行时安全边界

保留板端系统 RKNN 运行时作为回滚基线。将 RKNN Runtime 2.3.2 与 CosmoEdge 一起
打包，通过可执行文件 RPATH 或任务局部 `LD_LIBRARY_PATH` 选择它；不要覆盖
`/usr/lib/librknnrt.so`。生产推理使用原生 C API，不依赖 `rknn_server`。

## 模型与预处理约定

首批支持的模型为：

1. 安全帽分类：`1x3x224x224`，ONNX opset 19。
2. YOLOv8 检测：`1x3x640x640`，转换为 ONNX opset 19 / IR 9。

CosmoEdge 负责 resize、通道顺序和归一化。转换过程不得再次固化 mean/std 变换。
CosmoEdge 提供 float32 NCHW 张量；由于 Runtime 2.3.2 在该输入转换路径拒绝 NCHW，
RKNN 边界执行一次显式 NCHW 到 NHWC 拷贝。输出请求为 float32，以现有后处理器
为最终行为基准。

生产 YOLO 模型提供三组 box/class head。`yolov8_dfl_v1` 宿主适配器执行 DFL 和
sigmoid，再重建逻辑 `[1,84,8400]` 约定。不支持单个量化输出，因为共享 scale
会压缩置信度精度。

## 可复现转换

在操作人员选定的路径准备已验证离线包：

```bash
./scripts/rknn/prepare_offline_env.sh "$RKNN_OFFLINE_BUNDLE"
```

锁定的 YOLO 转换顺序为：

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

校准样本和数值一致性样本没有标签，不能替代带标签的 precision/recall/F1 验收集。

## 构建与部署

公开构建以 digest 固定的基础镜像为起点，并从 Rockchip 官方仓库的固定 commit 安装
RKLLM Runtime v1.3.0；最终环境包含 aarch64 工具链、RKNN Runtime、RKLLM Runtime、
MPP 和 RGA 开发文件。基础资源目录提供通用动作、布局和字体；RKNN 资源目录提供
RK3576 算法与模型。

```bash
./scripts/docker-compose.sh -f docker-compose.rk3576.yml build --pull cosmo-rk3576-package
./scripts/docker-compose.sh -f docker-compose.rk3576.yml run --rm cosmo-rk3576-package
sha256sum build_output/rk3576/cosmo-*.tar.gz
```

基础镜像和 RKLLM 官方文件均可公开获取，无需 `docker login`。辅助脚本会自动选择
Docker Compose V2/V1。该命令使用 Rockchip 媒体后端构建 Release 包，并在
`build_rknn/cosmo-tests` 保留 aarch64 测试程序；不会启用 `COSMO_DEV_MODE`。

RKLLM 是正式 RK3576 包的强制依赖：头文件、`librkllmrt.so` 或许可证任一缺失都会让
配置阶段失败，不能用缺少 Qwen3.5 的降级包作为发布候选。

正式入口在构建前删除旧的 `build_rknn`，避免将部分交叉编译缓存误作发布证据。
构建时依赖解析使用宿主机网络；一次性构建服务不发布或监听应用端口。

RK3576 的板端网络由系统 NetworkManager 管理，不由 CosmoEdge 的 Sophon netplan 路径
接管。清空 `/data/cwaiuserdata` 会重新生成默认 JSON，但不会把现有 NetworkManager
连接改成 `192.168.100.1`；部署和恢复时应以 `ip -4 addr`/`nmcli` 的实际地址为准。

运行时应隔离可变数据目录和包内应用目录：

```bash
export COSMO_DATA_DIR=/data/cwaiuserdata
export COSMO_APP_DATA_DIR=/appfs/cosmo_wander/cwai_data
export LD_LIBRARY_PATH="$COSMO_APP_DATA_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

`COSMO_DATA_DIR` 保存配置、数据库、上传内容和事件；`COSMO_APP_DATA_DIR` 保存包内
资源、模型、库和可执行文件。使用包内启动器，确保传递依赖从正在验证的产物解析。

## 可复用验收场景

客户旅程场景在有界时间内运行 1 路 × 5 FPS。验收范围包括登录、模型/任务/通道
可见性、真实原始与算法 HTTP-FLV 播放、OSD 差异、事件、重连、停止/启动恢复和清理。

长稳场景保持 4 路 × 5 FPS 运行 12 小时。运行时启用算法预览客户端，并使用
`--gate-hours 12` 审计。达到配置的磁盘熔断线后 runner 会停止。使用
`--password-stdin`，避免凭据进入进程参数。

预览验证需要真实的 `ffmpeg` 和 `ffprobe` 可执行文件。工具会在修改设备配置前
完成环境预检。

## 已验证发布边界

- 4 路 × 5 FPS 完成 12 小时门禁，媒体失败/fallback 增量为 0，内存池统计稳定；
  对应 CPU 实测值保留在该次历史证据记录中。
- 真实原始与算法播放、硬件解码/编码、OSD、重连和任务重启恢复在被测产物上通过。
- 延迟 Copy-out 会在宿主拷贝前丢弃无需处理的帧，是本版本选定的优化方案。
- v1.1 公开报告记录了单算法 5 FPS 的 16 路阶梯和双算法 5 FPS 的 8 路阶梯；两者
  均为短时实测边界，尚未升级为官方推荐配置。
- RK3576 NPU 指标使用 `/sys/kernel/debug/rknpu/load` 的厂商忙碌时间计数器；健康卡片
  展示最忙核心，加速器 payload 保留所有核心。启动脚本仅将该只读文件暴露到
  `/run/cosmo-edge/metrics/rknpu-load`；devfreq governor 信号不会被当作 NPU 负载。
- RK3576 NPU 和媒体分配共享系统 DDR。加速器指标标记为
  `memoryDomain=shared-system`；面板只显示一次系统内存容量，不再将同一内存池重复
  计为独立显存。

这些结论与产物绑定；源码、模型、运行时或安装包变化后必须重新验证。已接受的发布
记录应保留不可变安装包 SHA-256、业务精度结果、凭据安全日志、事件留存结果、清理
状态和实测值。原始验证产物继续保留在源码树之外。
