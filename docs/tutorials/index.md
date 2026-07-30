---
title: CosmoEdge 系统使用指南
description: 从首次检测到 AI 能力配置、Pipeline 编排和第三方模型接入的任务式使用路径。
prev:
  text: 文档首页
  link: /
next:
  text: 快速开始：部署、登录与首次检测
  link: /tutorials/01-quickstart/quickstart
---

# CosmoEdge 系统使用指南

这套指南围绕真实任务组织内容。每篇都从明确目标开始，以可观察、可复测的结果结束。
第一次使用时建议按顺序完成；已有经验的读者可以按下面三组直接进入所需任务。

## 基础使用

| 指南 | 适合场景 | 最终结果 |
| --- | --- | --- |
| [快速开始：部署、登录与首次检测](./01-quickstart/quickstart.md) | 第一次部署或连接系统 | 视频通道运行，实时展示与事件中心验证第一条检测 |
| [场景任务配置：通道、区域、参数与告警](./02-scenario-config/scenario-config.md) | 把内置算法配置成业务任务 | 区域、参数和策略持久化，正负样本证明告警规则生效 |

## AI 能力配置

| 指南 | 适合场景 | 最终结果 |
| --- | --- | --- |
| [VLM 与 DINO：提示词驱动的视觉任务](./03-vlm-guide/vlm-guide.md) | 验证状态判断或开放类别定位 | 固定样本证明 VLM 判断和 DINO 检测框符合提示词定义 |

## 高级扩展

| 指南 | 适合场景 | 最终结果 |
| --- | --- | --- |
| [算法编排：修改与创建 Pipeline](./04-pipeline-orchestration/pipeline-orchestration.md) | 修改规则链路或创建新业务流程 | Pipeline 节点可持久化，正负样本和重启验证通过 |
| [第三方模型接入：转换、上传与验证](./05-model-porting/model-porting.md) | 将自有模型带入 CosmoEdge | 模型加载、推理、解析、事件和持续运行均有证据 |

## 推荐路径

- **首次体验**：快速开始 → 场景任务配置。
- **长尾需求验证**：场景任务配置 → VLM 与 DINO。
- **自定义业务逻辑**：场景任务配置 → 算法编排。
- **自有模型部署**：算法编排 → 第三方模型接入。

不要跳过前一篇的最终验收。模型能上传、任务能保存或页面有检测框，都只是中间状态；
每条路径都应完成相应的结果和失败路径验证。

## 相关文档

- [部署指南](../guide/deployment.md)
- [运行配置](../guide/configuration.md)
- [故障排查](../guide/troubleshooting.md)
- [架构概览](../guide/architecture.md)
- [模型与资源](../reference/models.md)
- [HTTP Webhook 参考](../reference/webhook.md)
- [MQTT 接入参考](../reference/mqtt.md)
- [CI 与质量检查](../development/ci.md)
