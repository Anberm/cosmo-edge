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

如果端口被占用，可以修改 `docker-compose.x86.yml` 的主机端口，或停止占用端口的服务。

Windows 的 Hyper-V / WSL 可能保留一段 TCP 端口，即使 `netstat` 没显示监听进程，Docker 仍会报告端口绑定失败。可以先查看系统保留范围：

```powershell
netsh interface ipv4 show excludedportrange protocol=tcp
```

`docker-compose.x86.windows.yml` 支持通过 `COSMO_X86_WEB_PORT` 改 Web 主机端口，无需修改受版本控制的文件。例如使用 `8280`：

```powershell
$env:COSMO_X86_WEB_PORT = "8280"
docker compose -f docker-compose.x86.windows.yml up -d --build
```

随后访问 `http://127.0.0.1:8280`。不设置该变量时仍默认使用 `8080`。

## Windows 构建脚本提示 `No such file or directory`

如果 Docker 构建在执行 `configure`、`config` 或 `Configure` 时报告文件存在但无法执行，通常是 Git for Windows 将无扩展名脚本检出为 CRLF，导致容器无法识别 shebang。

仓库根目录的 `.gitattributes` 会把自动识别出的文本文件（包括这些无扩展名脚本）固定为 LF。拉取最新规则后，请在没有未保存修改的全新 clone 或干净 worktree 中重试。可以用以下命令确认规则：

```powershell
git check-attr text eol -- 3rd/mp4v2-2.0.0/configure 3rd/openssl-3.5.3/config 3rd/srs-6.0-r0/trunk/configure
```

三个文件都应显示 `text: auto` 和 `eol: lf`。

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

## 受保护 preset 无法加载

设备只需要以下一个 Guard 状态文件：

```text
/data/cwaiuserdata/model-guard/device-certificate.bin
```

先检查证书状态和服务日志：

```bash
sudo test -f /data/cwaiuserdata/model-guard/device-certificate.bin
sudo journalctl -u cosmo.service -b --no-pager -n 200
```

如果受控 provisioner 仍在设备的临时目录，还可以运行
`sudo /临时目录/cosmo-model-provision status` 直接校验证书和本机绑定；SOURCE
包本身不提供该工具。

- `-2001`（`CMG_V2_CERTIFICATE_UNAVAILABLE`）：证书文件不存在或无法读取。
- `-2002`（`CMG_V2_CERTIFICATE_REJECTED`）：证书损坏、签名无效，或证书不是
  为本机签发。

不要生成逐模型 license，也不要复制另一台设备的证书。使用本机生成的新请求在
受控离线环境重新签发证书，再执行
`cosmo-model-provision install --certificate <证书绝对路径>`。SOURCE 安装器
不会创建、删除或修复该证书。

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
