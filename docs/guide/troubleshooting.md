---
title: 故障排查
description: 构建、运行、端口、Sophon 镜像、日志和文档站常见问题。
prev:
  text: 运行配置
  link: /guide/configuration
next:
  text: 架构概览
  link: /guide/architecture
---

# 故障排查

本文收集当前项目最常见的构建和运行问题。

## Web 控制台打不开

确认使用的是主机端口 `8080`：

```text
http://127.0.0.1:8080
```

检查容器状态：

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml ps
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml ps
  ```

查看日志：

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml logs -f
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml logs -f
  ```

## 端口冲突

x86 Compose 会发布：

- `8080`
- `1936`
- `1985`
- `18088`
- `8000/udp`

如果端口被占用，可以修改 `docker-compose.x86.yml` (或 Windows 上的 `docker-compose.x86.windows.yml`) 的主机端口，或停止占用端口的服务。

## `build_output/` 没有构建产物

使用完整运行命令：

- **Linux**:

  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```

- **Windows (PowerShell/CMD)**:

  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```

Sophon 路径使用：

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package
ls -lh build_output/public-runtime/
```

Sophon 产物不会直接写在 `build_output/` 根目录，而是按
`COSMO_MODEL_GUARD_BUILD_PROFILE` 隔离：

- SOURCE 构建（内部配置 `public-runtime`）：`build_output/public-runtime/`；
- 受控正式构建：`build_output/production-release/`。

默认文件名包含
`-SOURCE-<edge-commit>-<build-identity>-<archive-sha256>`。它是可安装的源码
构建，但不是正式签名发布；受保护 preset 模型仍要求通过独立的受控授权流程安装
一张与本机绑定的设备证书，不存在逐模型 license。

注意：`docker compose build` 只构建镜像，不一定执行导出产物的容器命令。

## Sophon 构建失败

Sophon 构建使用自包含的 `Dockerfile.sophon`（基于 `ubuntu:22.04`），无需外部基础镜像。

如果构建失败，请检查 Docker 构建日志：

```bash
docker compose -f docker-compose.sophon.yml run --rm cosmo-sophon-package 2>&1 | tail -50
```

常见问题：

- 网络问题导致 apt/npm/cargo 镜像下载失败 — 检查 `SOPHON_APT_MIRROR` 等环境变量。
- 磁盘空间不足 — 构建过程需要约 3GB 空间。
- `COSMO_MODEL_GUARD_BUILD_PROFILE` 取值不受支持——只接受
  `public-runtime` 和 `production-release`。
- 在非受控发布环境选择 `production-release`——缺少正式 SDK、设备初始化、
  信任身份、签发者或发布引导输入时按设计拒绝构建。普通源码修改应使用
  SOURCE，不要绕过正式发布检查。

## nginx / SRS / cosmo-engine 未启动

运行脚本：

```text
${INSTALLPATH}/scripts/run_start.sh
```

启动顺序包括：

1. 停止已有进程。
2. 启动 nginx。
3. 启动 SRS。
4. 启动 `cosmo-engine`。

检查日志：

```text
/data/cwaiuserdata/log/logs
```

## 软件升级后页面一直等待

升级期间设备会离线，页面会等待新的 Linux `bootId`，最长显示 15 分钟。如果重启清空登录会话，页面会在“已观察到离线”且新服务返回鉴权响应后进入登录页。这个交互超时不会取消设备端升级；重新登录后仍需核对软件版本。

在 Sophon 设备上检查：

```bash
systemctl status cosmo --no-pager -l
journalctl -u cosmo -b --no-pager -n 200
stat -c '%F %a %U:%G %n' /data/cwaiuserdata/upload/sessions
```

正常情况下 `cosmo.service` 应为 `active (running)`，暂存根目录应是真实目录并保持 `0700`。如果启动日志出现致命初始化异常，进程会返回非零状态并由 `Restart=on-failure` 重试。不要通过递归放宽整个 `/data/cwaiuserdata` 的权限来规避检查。

## 文档站构建失败

先安装依赖：

```bash
npm ci
```

再构建：

```bash
npm run docs:build
```

在 Windows PowerShell 中如果遇到 `npm.ps1` 执行策略问题，可以使用：

```powershell
npm.cmd run docs:build
```

## `vitepress` 未找到

说明还没有安装文档站依赖：

```bash
npm ci
```

## npm audit 提示漏洞

当前文档站依赖可能会出现 npm audit 提示。不要盲目升级依赖；升级前应确认 VitePress、主题配置和 GitHub Pages workflow 仍能构建通过。

## Windows 本机 CPU 构建

当前仓库没有确认可用的 Windows 本机 CPU 构建脚本。不要把旧脚本或旧命令写成公开支持路径。
