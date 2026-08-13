---
title: 构建指南
description: x86 Docker、Sophon、RK3576 和 CPU 测试构建路径。
prev:
  text: 文档首页
  link: /
next:
  text: RK3576 / RKNN 集成
  link: /guide/rk3576-rknn-development
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
| RK3576 稳定版构建 | 使用 RKNN、MPP 和 RGA 交叉编译发布包与测试程序 | 否 | `build_output/rk3576/` |
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
# 省略型号时默认 bm1688
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package

# 显式选择型号
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip bm1688
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

Windows PowerShell：

```powershell
# 省略型号时默认 bm1688
.\scripts\build_sophon_package.ps1

# 显式选择型号
.\scripts\build_sophon_package.ps1 -Chip bm1688
.\scripts\build_sophon_package.ps1 -Chip cv186x
```

两个支持的配置使用相互隔离的输出目录：

| 配置 | 用途 | 输出目录 | 部署状态 |
| --- | --- | --- | --- |
| Open（内部配置 `public-runtime`，默认） | 使用仓库内运行时 SDK 完成公开的 aarch64 编译、链接、打包和测试验证 | `build_output/public-runtime/` | 明文模型，无需设备授权 |
| Protected（内部配置 `production-release`） | 在受控环境中使用完整正式 SDK 和设备授权工具构建 | `build_output/production-release/` | 加密模型，需要设备授权 |

两种配置都生成 `cosmo-V<版本号>-<32位md5>.tar.gz`。同一格式既可以在 main
分支部署的管理页面升级，也可以在后续任意版本继续升级。应用包不签名；两种配置
只在模型是否加密以及是否包含 `cosmo-model-provision` 上有区别。

### 在Sophon设备上安装构建包

包内的`scripts/install.sh`由CMake从兼容迁移安装器生成，用于把应用安装到已准备好的
Sophon Linux设备并创建、启用`cosmo.service`。将构建输出中的唯一包名代入占位符：

```bash
scp build_output/public-runtime/<安装包>.tar.gz root@<设备IP>:/tmp/
ssh root@<设备IP>
cd /tmp
install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
tar -xzf <安装包>.tar.gz -C "$install_dir"
cd "$install_dir"/cosmo-V*/
sudo ./scripts/install.sh
sudo reboot
```

已有CosmoEdge正常运行时，也可以在 **系统管理 → 系统维护 → 软件升级** 上传同一个包。
两条路径完成后都要重新登录，并核对 **软件版本** 与安装包版本一致。SSH安装器安装应用和
服务，但不是任意空白硬件的操作系统镜像安装器。

维护人员在包含完整 Guard SDK 和授权工具的受控环境中使用一条命令构建：

```bash
COSMO_MODEL_GUARD_BUILD_PROFILE=production-release \
  docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package --chip cv186x
```

上例构建 CV186X Protected 包；构建 BM1688 时把末尾型号改为 `bm1688`，或省略型号。

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
- Docker Compose 接受芯片型号参数：`cosmo-sophon-package --chip bm1688` 或
  `cosmo-sophon-package --chip cv186x`。省略 `--chip` 时默认使用 `bm1688`。
- `scripts/build_sophon_package.sh` 把芯片型号传给 `scripts/build.sh -T -c <型号>`；
  `build.sh` 再选择对应资源目录，用户无需传入模型路径。
- 只导出构建产物，不启动服务。
- 芯片型号不会改变 CPack 或 MD5 重命名逻辑；各配置的输出仍隔离到
  `build_output/<profile>/`，包名仍为 `cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz`。

## RK3576 构建产物

RK3576 公开构建入口使用已固定 digest 的 GHCR 镜像，镜像包含 aarch64 工具链、
RKNN Runtime、MPP 和 RGA 开发文件，无需登录即可拉取：

```bash
docker compose -f docker-compose.rk3576.yml pull cosmo-rk3576-package
docker compose -f docker-compose.rk3576.yml run --rm cosmo-rk3576-package
sha256sum build_output/rk3576/cosmo-*.tar.gz
```

该入口已确认：

- 在 `linux/amd64` 构建容器中执行 aarch64 交叉编译。
- 构建前清理 `build_rknn/`，再调用 `scripts/build_rknn.sh -T`，避免复用部分缓存。
- 将唯一发布包导出到 `build_output/rk3576/`，不启动应用服务。
- 同时生成 `build_rknn/cosmo-tests`、`cosmo-rknn-backend-smoke` 和
  `cosmo-rknn-fastpath-qualify` 三个 aarch64 验证程序。
- 使用宿主机网络解析构建依赖，但不发布应用端口。

稳定版支持范围、运行时选择、模型约定和板端证据边界见
[RK3576 / RKNN 集成指南](./rk3576-rknn-development.md)。

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
