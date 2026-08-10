---
title: 构建指南
description: x86 Docker、Sophon 构建产物和 CPU 测试构建路径。
prev:
  text: 文档首页
  link: /
next:
  text: macOS Docker Preview
  link: /guide/macos-docker-preview
---

# 构建指南

本文只记录当前仓库中已经确认的构建路径。历史文档或旧脚本中出现过、但当前仓库无法验证的路径，不作为公开支持路径。

> **💡 Docker Compose 版本提示**
> 本文档统一使用最新的 Docker Compose V2 命令格式 (`docker compose`)。如果你使用的是旧版 Docker 环境（如自带独立的 V1 插件），请将文中的 `docker compose` 替换为带横杠的 `docker-compose`。

## 构建路径总览

| 路径 | 用途 | 是否启动服务 | 输出 |
| --- | --- | --- | --- |
| x86 Docker 开发运行环境 | 首次体验、开发评估、生成 x86 发布包 | 是 | `build_output/` |
| macOS Docker Preview | Apple Silicon 上体验单路 x86 工作流 | 是 | `build_output/macos-x86/` |
| Sophon SOURCE 构建 | 交叉编译可安装的源码构建包 | 否 | `build_output/public-runtime/` |
| CPU 测试构建 | 构建 `cosmo-tests` | 否 | `build_cpu/cosmo-tests` |

## x86 Docker 开发运行环境

Linux:

```bash
docker compose -f docker-compose.x86.yml up -d --build
```

Windows (PowerShell/CMD):

```powershell
docker compose -f docker-compose.x86.windows.yml up -d --build
```

Apple Silicon macOS (Preview):

```bash
./scripts/macos-docker-preview.sh doctor
./scripts/macos-docker-preview.sh up
```

Mac 路径显式运行 `linux/amd64`，使用独立卷并只绑定回环地址。它不启用
Model Guard，也不构成原生 arm64 或 NPU 性能证据。完整说明和验收范围见
[macOS Docker Preview](./macos-docker-preview.md)。

该路径来自：

- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
- `docker-compose.x86.macos.yml` (Apple Silicon macOS Preview)
- `Dockerfile.x86`
- `scripts/build_cpu.sh`

已确认构建参数：

| 参数 | 值 |
| --- | --- |
| `COSMO_TARGET_ARCH` | `x86_64` |
| `COSMO_NN_USE_SOPHON_BACKEND` | `OFF` |
| `COSMO_NN_USE_CPU_BACKEND` | `ON` |
| `COSMO_ENABLE_OPENH264` | `ON` |
| `COSMO_DEV_MODE` | `ON` |
| `RESOURCE_DIR` | `data/resource/aiboxresource_x86` |

构建完成后：

- Web 控制台通过 `http://127.0.0.1:8080` 访问。
- 发布包和构建产物导出到 `build_output/`。
- 运行数据保存在 Docker volume `cosmo-x86-data`。
- 资源目录挂载到 Docker volume `cosmo-x86-app-resource`。

## Sophon 构建产物

公开构建入口默认使用
`COSMO_MODEL_GUARD_BUILD_PROFILE=public-runtime`：

Linux / Bash：

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
```

Windows PowerShell：

```powershell
.\scripts\build_sophon_package.ps1
```

两个支持的配置使用相互隔离的输出目录：

| 配置 | 用途 | 输出目录 | 部署状态 |
| --- | --- | --- | --- |
| Open（内部配置 `public-runtime`，默认） | 使用仓库内运行时 SDK 完成公开的 aarch64 编译、链接、打包和测试验证 | `build_output/public-runtime/` | 明文模型，无需设备授权 |
| Protected（内部配置 `production-release`） | 在受控环境中使用完整正式 SDK 和设备授权工具构建 | `build_output/production-release/` | 加密模型，需要设备授权 |

两种配置都生成 `cosmo-V<版本号>-<32位md5>.tar.gz`。同一格式既可以在 main
分支部署的管理页面升级，也可以在后续任意版本继续升级。应用包不签名；两种配置
只在模型是否加密以及是否包含 `cosmo-model-provision` 上有区别。

### 在已有设备上安装构建包

构建完成后，在已有 CosmoEdge 的设备上登录管理页面，进入
**系统管理 → 系统维护 → 软件升级**，选择对应输出目录中的
`cosmo-V<版本号>-<32位md5>.tar.gz` 并确认。升级期间保持设备供电和网络连接。
设备重启、页面恢复并重新登录后，在设备信息中核对 **软件版本** 与安装包版本一致。

这个公开流程只覆盖已有 CosmoEdge 系统的升级。仓库当前不提供空白设备的公开工厂
首装流程；升级包也不是供用户解压后直接安装的离线安装器。

维护人员在包含完整 Guard SDK 和授权工具的受控环境中使用一条命令构建：

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
  docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
```

如果受控 SDK 中缺少 `cosmo-model-provision`，Protected 构建会直接失败。
受控生产 SDK 应放在宿主机的
`build_output/model-guard-sdk-production/`，该目录通过现有 Compose 挂载进入
容器且不会提交到 Git。Protected 构建会自动优先使用它；Open 构建不受影响。

Protected 的 CPack 产物本身就是管理页面接受的升级包，不再需要离线应用签名步骤。
Guard 设备证书和模型加密秘密仍属于受控输入，不得写入公开仓库。

该路径来自：

- `docker-compose.sophon.yml`
- `scripts/build_sophon_package.ps1`（Windows：构建前自动修复 `.so` 软链接）
- `scripts/build.sh`

已确认行为：

- 基础镜像使用预先构建的 GHCR 镜像：`ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_sophon:v1`（统一的编译环境，加速了本地启动时间）。
- 使用 `scripts/build.sh -T -m data/resource/aiboxresource` 构建发布候选产物和 `cosmo-tests`（不启用 dev mode，故不传 `-t`）。
- 只导出构建产物，不启动服务。
- 各配置的输出隔离到 `build_output/<profile>/`。

## CPU 测试构建

```bash
bash scripts/build_cpu_test.sh
```

该脚本会使用 CPU 后端配置 CMake，并开启：

```text
BUILD_TESTS=ON
```

目标产物：

```text
build_cpu/cosmo-tests
```
