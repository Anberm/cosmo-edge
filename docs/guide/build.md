---
title: 构建指南
description: x86 Docker、Sophon 构建产物和 CPU 测试构建路径。
prev:
  text: 文档首页
  link: /
next:
  text: 部署指南
  link: /guide/deployment
---

# 构建指南

本文只记录当前仓库中已经确认的构建路径。历史文档或旧脚本中出现过、但当前仓库无法验证的路径，不作为公开支持路径。

> **💡 Docker Compose 版本提示**
> 本文档统一使用最新的 Docker Compose V2 命令格式 (`docker compose`)。如果你使用的是旧版 Docker 环境（如自带独立的 V1 插件），请将文中的 `docker compose` 替换为带横杠的 `docker-compose`。

## 构建路径总览

| 路径 | 用途 | 是否启动服务 | 输出 |
| --- | --- | --- | --- |
| x86 Docker 开发运行环境 | 首次体验、开发评估、生成 x86 发布包 | 是 | `build_output/` |
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

该路径来自：

- `docker-compose.x86.yml` (Linux)
- `docker-compose.x86.windows.yml` (Windows)
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
| SOURCE（内部配置 `public-runtime`，默认） | 使用仓库内运行时 SDK 完成公开的 aarch64 编译、链接、打包和测试验证 | `build_output/public-runtime/` | 可安装的源码构建，不是正式签名发布 |
| `production-release` | 在受控环境中使用完整正式 SDK、设备初始化工具、发布信任身份和发布引导输入构建 | `build_output/production-release/` | 输出仅供空机首装的 `FACTORY-BASE`；OTA 仍需离线签名发布包 |

SOURCE 归档文件名以
`-SOURCE-<edge-commit>-<build-identity>-<archive-sha256>.tar.gz` 结尾。
它包含 Guard 运行时 SDK 和 SOURCE 安装资产，但不包含 provisioner、发布引导、
私有签名材料或生产签名事务入口。它可以在已经单独完成设备准备的机器上安装修改后
的应用代码，但不能初始化空白设备，也不能通过重命名变成正式签名发布。

### 在设备上安装 SOURCE 构建

解压前，先将归档文件名中的 SHA-256 与归档文件的实际 SHA-256 对比。解压后
进入唯一的包目录并执行：

```bash
sudo ./install-device.sh install
sudo ./install-device.sh status
```

设备没有 `/appfs` 时，安装器会自动创建 `/appfs/cosmo_wander`，验证解压后的
payload，停止 `cosmo.service`，删除现有 `cwai_data`，安装新应用树并启动 SOURCE
服务。它不创建应用备份，也不提供回滚命令。最终健康检查失败时，命令会报告失败
并保留新安装的应用树，便于直接诊断或重新安装。`status` 会显示当前模式、build
ID、Edge 基准 commit、Guard SDK release 和服务状态。

设备已经完成配置时，
`/data/cwaiuserdata/model-guard/device-certificate.bin` 保持不变。这一张与本机
绑定的设备证书授权加载使用同一产品模型密钥发布的当前及以后全部 preset 模型，
不存在逐模型 license。空白设备上 SOURCE 可以安装应用和服务，但受保护 preset
必须先通过独立的受控授权流程安装设备证书后才能运行。SOURCE 的 `install` 和
`status` 始终不修改
`/data/cwaiuserdata/model-guard`。

维护人员只能在受控发布环境中选择正式配置。仅使用基础 Compose 文件会按
设计失败；必须通过审核后的 override 以只读方式分别挂载完整 SDK 和各项公开
信任输入，并设置全部正式构建变量：

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
  docker compose -f docker-compose.sophon.yml \
  -f /path/to/approved-production.override.yml \
  run --rm cosmo-sophon-package
```

PowerShell 入口会验证同一个配置变量并据此选择输出目录，但只设置该变量并
不能提供受控输入；除非组织的正式发布自动化补充这些输入，否则构建会安全
失败。

`production-release` 生成的 CPack 产物不是 OTA 包，设备更新器不会接受它。
它只能按登记的 SHA-256 在受控空机首装流程中作为 `FACTORY-BASE`。正常安装和
升级仍须由离线发布流程生成设备接受的已签名发布归档。发布私钥不得写入仓库，
也不得传给普通 Compose 构建。

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
