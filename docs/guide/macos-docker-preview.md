---
title: macOS Docker Preview
description: 在 Apple Silicon Mac 上通过隔离的 linux/amd64 Docker 环境体验 CosmoEdge x86 工作流。
prev:
  text: RK3576 / RKNN 集成
  link: /guide/rk3576-rknn-development
next:
  text: 部署指南
  link: /guide/deployment
---

# macOS Docker Preview

> **状态：Preview。** 这条路径面向本地开发和单路离线视频体验，不是原生
> arm64 构建、Sophon 设备仿真或生产部署方案。Apple Silicon 通过 Docker
> Desktop 运行 `linux/amd64` 镜像，因此首次构建和推理速度都低于原生 x86 Linux。

## 能做什么

Mac Preview 复用 x86 CPU/ONNX Runtime 后端，目标是让开发者在没有边缘设备时完成：

- 登录 Web 控制台；
- 上传本地视频和查看持久化通道；
- 使用随仓库提供的 x86 ONNX 模型创建任务、设置 ROI 和启停分析；
- 查看实时 OSD、告警截图和事件记录；
- 导出非空告警 CSV；
- 停止并重新启动容器后继续使用原有配置和上传数据。

它不提供 NPU 性能等价、USB 摄像头直通、局域网设备发现、多路容量结论或本地
VLM 能力。更完整的能力边界见[验收与边界](#验收与边界)。

## 准备环境

当前 Preview 的准入环境是 Apple Silicon Mac 和 Docker Compose V2。建议预留：

- 至少 20 GiB 可用磁盘空间；
- 至少 8 GiB Docker 虚拟机内存；
- Rosetta 2，用于改善 amd64 仿真体验。

安装和首次启动 Docker Desktop 前，请自行确认并接受其独立的
[Docker Subscription Service Agreement](https://docs.docker.com/subscription/)。
[Docker 的 Mac 安装说明](https://docs.docker.com/desktop/setup/install/mac-install/)
列出了当前支持的 macOS、内存和 Rosetta 要求。

对于这个 amd64 工作负载，Docker Desktop 当前使用 Apple Virtualization
Framework 并开启 Rosetta 时通常更快。Docker 的
[设置说明](https://docs.docker.com/desktop/settings-and-maintenance/settings/)
指出 Rosetta 选项只在 Apple Virtualization Framework 下可用；Docker VMM
目前不通过 Rosetta 加速 amd64 仿真。这个设置是性能建议，不是 CosmoEdge
脚本自动修改的系统配置。

## 启动

在仓库根目录运行只读准入检查：

```bash
./scripts/macos-docker-preview.sh doctor
```

检查会确认 Apple Silicon、Docker Desktop、Compose 配置、Docker 内存、磁盘、
Rosetta 和本地端口，但不会安装组件或修改 Docker 设置。通过后启动：

```bash
./scripts/macos-docker-preview.sh up
```

脚本会在镜像缺失时构建 `docker-compose.x86.macos.yml`，已有镜像时则直接复用，
随后等待 nginx、SRS、`cosmo-engine` 和 Web 响应全部健康，然后输出：

```text
http://127.0.0.1:8080
```

首次构建需要下载 amd64 构建镜像和依赖，并在仿真环境内编译，可能明显慢于后续启动。
Mac Compose 通过摘要固定已验证的 amd64 builder 和 Debian 运行基础镜像，避免标签
更新让同一候选使用不同基础环境。
Mac Compose 默认使用一个编译 job，以避开 amd64 仿真下嵌套 GNU Make jobserver 的
文件描述符兼容问题。`COSMO_X86_BUILD_JOBS` 可以覆盖该值，但提高并行度属于实验性
调优，应重新完成本页的两轮验收。

源代码、Dockerfile 或构建资源变化后，显式重建：

```bash
./scripts/macos-docker-preview.sh up --build
```

普通 `up` 会复用现有镜像，不会因工作区中无关文件变化再次触发耗时构建。

如果 Web 端口 `8080` 已被占用，可以只覆盖 Web 端口：

```bash
COSMO_X86_WEB_PORT=8280 ./scripts/macos-docker-preview.sh up
```

随后访问 `http://127.0.0.1:8280`。流媒体端口 `1936`、`1985` 和 `18088` 保持固定，
不能用作 Web 端口，因为前端预览链路依赖当前 SRS 端口契约。

Mac Preview 默认直接通过本机回环端口 `18088` 播放 SRS HTTP-FLV，不先尝试 WebRTC。
Web 控制台端口即使通过 `COSMO_X86_WEB_PORT` 改为其他值，流媒体端口仍保持 `18088`。
这样可以避开 Docker Desktop 本地 WebRTC 建连失败后最长十余秒的媒体超时与回退窗口；
这只是本机 Preview 的确定性设置，不改变其他部署默认采用的 WebRTC 模式。

## 日常命令和数据隔离

```bash
# 查看容器和健康状态
./scripts/macos-docker-preview.sh status

# 查看最近日志；加 --follow 持续跟踪
./scripts/macos-docker-preview.sh logs
./scripts/macos-docker-preview.sh logs --follow

# 停止服务，但保留配置、上传文件和模型资源
./scripts/macos-docker-preview.sh down
```

Mac Preview 使用独立的 Compose 项目、容器、镜像、卷和构建输出：

| 对象 | 名称或路径 |
| --- | --- |
| Compose 项目 | `cosmo-x86-macos-preview` |
| 容器 | `cosmo-x86-macos-preview` |
| 运行数据卷 | `cosmo-x86-macos-preview-data` |
| 模型资源卷 | `cosmo-x86-macos-preview-app-resource` |
| 发布包输出 | `build_output/macos-x86/` |

默认 `down` 不删除命名卷，因此不会因为一次普通停止而清空数据。

## 验收与边界

发布为社区 Preview 前，至少应在 Apple Silicon Mac 上连续完成两次以下流程，且
两次之间执行一次 `down` / `up`：

1. 容器健康，Web 控制台可以登录；
2. 上传仓库内 `data/test-video/Safety Helmet.mp4`，刷新后通道仍存在；
3. 使用随仓库提供的未戴安全帽 ONNX 模型创建任务并设置 ROI；
4. 启动任务，实时展示出现视频和 OSD；单个已启用任务应直接进入 OSD，不先建立原始流；
5. 事件中心生成带截图的告警；
6. 导出的告警 CSV 非空；
7. 重启后配置、视频和任务仍可使用，容器没有崩溃或异常重启。

验收结论只覆盖实际测试的这一层：

| 能力 | Mac Preview 结论 |
| --- | --- |
| Web 控制台、视频上传、任务、ROI、OSD、告警、CSV | Preview 目标范围，按上面的连续验收记录结论 |
| x86 ONNX Runtime CPU 推理 | 运行于 `linux/amd64` 仿真，不能代表原生性能 |
| Sophon / Rockchip NPU、USB 摄像头、局域网发现 | 未覆盖 |
| 多路性能、长稳、生产部署 | 未覆盖，不能从单路本地视频外推 |
| Model Guard、Sophon Protected 包、CEMC 模型和设备授权 | 未启用、未验证 |

`scripts/build_cpu.sh` 明确关闭 Sophon 后端并启用 CPU 后端，CMake 因此关闭
Model Guard。Mac Preview 基于 Model Guard 开发分支并不等于在 Mac 上运行了
Model Guard；CEMC 受保护模型仍属于 Sophon Protected 运行路径。

## 本地安全默认值

`docker-compose.x86.macos.yml` 将 Web、RTMP、SRS API 和 HTTP stream 端口
全部绑定到 `127.0.0.1`，不发布 UDP 设备发现端口、不请求 `NET_ADMIN`，也不映射
`/dev/video*`。这适合单机开发，但不会让同一局域网的其他主机直接访问。若要开放
远程访问，需要另行评估身份验证、TLS、防火墙、视频数据和端口暴露；不要直接删除
回环地址绑定并把 Preview 当成生产配置。

## 常见问题

- `Docker Desktop is not ready`：打开 Docker Desktop，完成首次启动和协议确认，
  等状态为 Running 后重新执行 `doctor`。
- 构建很慢：确认 Docker 使用的 VMM 和 Rosetta 设置，并给 Docker 至少 8 GiB
  内存。amd64 仿真仍然只属于 best-effort Preview。
- 服务没有变为 healthy：运行 `./scripts/macos-docker-preview.sh logs`，先检查
  nginx、SRS、`cosmo-engine` 中第一个失败的进程。
- Web 端口冲突：设置 `COSMO_X86_WEB_PORT`；其他固定端口冲突需要先停止占用者。
- 修改源码后需要保留数据并重建：运行 `./scripts/macos-docker-preview.sh up --build`；
  不要删除两个 Preview 命名卷。仅重启时使用普通 `up`。
