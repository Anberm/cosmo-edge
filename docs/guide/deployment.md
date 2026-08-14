---
title: 部署指南
description: 当前运行目录、服务进程、端口、升级包和 systemd 行为。
prev:
  text: macOS Docker Preview
  link: /guide/macos-docker-preview
next:
  text: 架构概览
  link: /guide/architecture
---

# 部署指南

本文根据当前运行脚本整理，主要涉及：

- `scripts/docker-entrypoint.x86.sh`
- `scripts/start.sh`
- `scripts/run_start.sh`
- `scripts/install.sh`

## x86 Docker 运行环境

启动：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml up -d --build
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml up -d --build
  ```
- **Apple Silicon macOS (Preview)**:
  ```bash
  ./scripts/macos-docker-preview.sh up
  ```

停止：

- **Linux**:
  ```bash
  docker compose -f docker-compose.x86.yml down
  ```
- **Windows (PowerShell/CMD)**:
  ```powershell
  docker compose -f docker-compose.x86.windows.yml down
  ```
- **Apple Silicon macOS (Preview)**:
  ```bash
  ./scripts/macos-docker-preview.sh down
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
- **Apple Silicon macOS (Preview)**:
  ```bash
  ./scripts/macos-docker-preview.sh logs --follow
  ```

## 运行目录

| 路径 | 说明 |
| --- | --- |
| `<INSTALLPATH>` | 主安装目录，由 Dockerfile 或部署脚本设定 |
| `<INSTALLPATH>/resource` | 运行资源目录 |
| `<DATADIR>` | 用户持久化数据目录，默认位于持久化卷上 |
| `<DATADIR>/log/logs` | 日志目录 |
| `<DATADIR>/upload/sessions` | 可恢复分片上传会话；目录权限固定为 `0700` |
| `<DATADIR>/upgrade` | 升级包目录 |

## 运行进程

启动脚本会拉起：

- `nginx` (system, `/usr/sbin/nginx`)
- `srs`
- `cosmo-engine`

对应路径：

`${INSTALLPATH}` 由 Dockerfile 中的 `INSTALLPATH` 环境变量设置（默认见运行配置）。
具体路径：
```text
/usr/sbin/nginx  (system nginx)
${INSTALLPATH}/bin/srs
${INSTALLPATH}/bin/cosmo-engine
```

## 默认端口

| 端口 | 来源 | 用途 |
| --- | --- | --- |
| `8080 -> 80` | x86 Compose 文件；Mac Preview 仅绑定 `127.0.0.1` | x86 Docker Web 控制台 |
| `1936` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | RTMP |
| `1985` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | SRS API |
| `18088` | `docker-compose.x86.yml` / `docker-compose.x86.windows.yml` / SRS | HTTP stream |
| `8000` | `src/app/AppConstants.h`（`kDefaultHttpPort`，TCP） | 后端 HTTP 常量（容器内监听 TCP） |
| `9000` | `src/app/AppConstants.h`（`kDefaultWebSocketPort`，TCP） | 后端 WebSocket（容器内监听 TCP） |

> 端口暴露说明：`8080 -> 80`、`1936`、`1985`、`18088` 是 x86 Docker 对**主机暴露**的端口。`8000`、`9000` 是容器内进程端口；其中 `8000` 在 `docker-compose.x86.yml` 中以 `8000:8000/udp` 形式映射到主机（用于设备发现等 UDP 场景），与后端 HTTP 的 TCP 监听不同。主机侧访问后端 HTTP/WebSocket API 通常经由 nginx（容器内 `80`，映射到主机 `8080`）反向代理，而不是直接访问主机的 `8000`。

生产环境的 UDP 设备发现协议仅允许 `probe` 查询。修改网卡、写入硬件信息和授权码操作不再通过多播执行；只能通过已实现的身份验证管理 API 调用，尚未提供安全替代 API 的操作将被拒绝。

运行脚本设置的流媒体环境变量：

```bash
COSMO_STREAM_PLAY_MODE=srs
COSMO_STREAM_RTMP_BASE=rtmp://127.0.0.1:1936/live
COSMO_STREAM_RTC_API_PORT=1985
COSMO_STREAM_HTTP_PORT=18088
```

## 发布包结构

安装/升级脚本期望发布包中包含：

- `bin`
- `files`
- `font`
- `scripts`
- `web`

可选或按存在处理：

- `lib`
- `resource`

升级包文件名必须匹配以下格式：

```text
cosmo-V<major>.<minor>.<patch>-<32-char-md5>.tar.gz
```

Web 控制台的本地升级流程如下：

1. 查询设备状态并记录当前 Linux `bootId`。
2. 按设备返回的上传能力分片传输安装包，界面显示实际上传百分比。
3. 后端校验文件名、MD5、归档安全、目录结构和实时磁盘预算。
4. Sophon 设备重启后，启动脚本再次校验 MD5 并安装。Open 与 Protected 包永久使用同一升级流程。
5. 页面在看到新的 `bootId` 后返回登录页。如果重启使登录会话失效，则必须先观察到设备离线，再收到新服务的鉴权响应，才能判定服务已恢复并返回登录页。

页面等待恢复的 15 分钟是交互超时，不会中止设备端已经开始的升级。超时后应保持供电，并通过设备网络和 systemd 日志确认状态。重新登录后还应核对软件版本与本次发布包；页面恢复只证明重启与服务恢复，不替代版本验收。

## SSH安装路径

除了Web升级，包内`scripts/install.sh`还提供从main版本迁移及后续兼容安装的SSH入口。
它会安装应用、替换并启用`cosmo.service`，然后由重启启动服务：

```bash
scp build_output/public-runtime/<chip>/<安装包>.tar.gz root@<设备IP>:/tmp/
ssh root@<设备IP>
cd /tmp
install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
tar -xzf <安装包>.tar.gz -C "$install_dir"
cd "$install_dir"/cosmo-V*/
sudo ./scripts/install.sh
sudo reboot
```

该路径假设Sophon Linux基础系统和运行依赖已经准备好，不是任意空白硬件的操作系统
镜像安装流程。安装前记录当前版本和恢复方案；安装器会替换当前应用树。

## systemd 服务

已配置设备的服务启动命令为：

```text
ExecStart=/appfs/cosmo_wander/cwai_data/scripts/inte_run_start.sh
```

`scripts/install.sh`负责升级事务，不创建空白设备的systemd unit。服务以`root`
运行并使用`Restart=on-failure`。

部分 Sophon 系统会在启动时把持久化数据树的属主恢复为设备管理账户。上传暂存服务允许 `sessions` 目录继承一个不可被 group/other 写入的直接父目录属主，同时继续要求：

- `sessions` 是真实目录而不是符号链接，且权限为 `0700`；
- 运行期间属主、设备号和 inode 不变；
- 每个会话目录和载荷仍由当前服务账户创建并保持私有权限。

不要对整个 `<DATADIR>` 做宽泛的递归 `chmod` 或 `chown`。

## 接口文档静态链接

打包接口文件：

- `data/Interface/ai-box-interface_v1.0.html`
- `data/Interface/mqtt_v1.0.html`

运行时会链接到：

- `web/staticfile/httpInterface.html`
- `web/staticfile/mqttInterface.html`
