---
title: CV186X 快速开始
description: 在已准备好的 CV186X Linux 设备上安装 CosmoEdge 1.1，并完成首次检测。
prev:
  text: 构建指南
  link: /guide/build
next:
  text: RK3576 / RKNN 集成
  link: /guide/rk3576-rknn-development
---

# CV186X 快速开始

本指南适用于已经配置好 Sophon 运行依赖和网络的 CV186X Linux 设备。CosmoEdge 1.1
的 CV186X 路径使用 BMRT 与面向 CV186X 编译的 `.nn` 模型；BM1688 模型不能直接复用。

## 1. 获取并核对安装包

从 [GitHub Release](https://github.com/cosmo-wander-ai/cosmo-edge/releases) 获取 CosmoEdge
1.1 Sophon Open 包和发布页列出的 SHA-256，然后在构建机上核对：

```bash
sha256sum cosmo-V1.1.0-*.tar.gz
scp cosmo-V1.1.0-*.tar.gz root@<device_ip>:/tmp/
```

## 2. 安装并启动

```bash
ssh root@<device_ip>
install_dir=$(mktemp -d /tmp/cosmo-install.XXXXXX)
tar -xzf /tmp/cosmo-V1.1.0-*.tar.gz -C "$install_dir"
cd "$install_dir"/cosmo-V*/
./scripts/install.sh
reboot
```

设备恢复后确认 `cosmo.service` 为 active，并从设备管理页面核对软件版本为 1.1。
已有 CosmoEdge 的设备也可以在 **系统管理 → 系统维护 → 软件升级** 上传同一安装包。

## 3. 导入模型并创建首个事件

1. 登录 Web 控制台，进入 **模型仓库**，导入 `chip_type=CV186X` 的 `.nn` 模型目录。
2. 在 **视频接入** 添加一路测试视频，再在 **场景任务** 创建检测任务并绑定该视频。
3. 打开算法预览，确认 OSD 和事件；随后检查任务状态与服务日志无持续报错。

模型目录至少应包含匹配的 `config.json` 和模型文件。输入尺寸、量化方式、前后处理和
输出张量必须与配置一致；详细合同见[模型适配指南](/tutorials/05-model-porting/model-porting)。

## 升级、恢复与证据边界

- 安装和 Web 升级共用 `cosmo-V<version>-<md5>.tar.gz` 生命周期；保持供电，恢复后同时
  核对服务状态和软件版本。
- 若服务未恢复，先检查 `systemctl status cosmo.service` 与服务日志；不要重复上传同一
  包掩盖首次失败。
- 完整目录、端口、持久化、失败恢复和回滚边界见[部署指南](/guide/deployment)。
- 当前公开容量结果见 [ScenarioBench v1.1](/benchmarks/scenario-bench/v1.1/README)；短时最高点
  不是官方推荐配置，生产容量需按实际模型、视频和精度要求复验。
