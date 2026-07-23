---
title: 场景任务可复现压测工具设计方案
description: 面向 CosmoEdge 场景任务的性能与稳定性压测工具设计，覆盖控制 PC 运行架构、场景包规范、执行链路、接口复用、报告输出与实施计划。
prev:
  text: 测试范围与测试用例说明书
  link: /guide/test-cases
next: false
---

# 场景任务可复现压测工具设计方案

## 一、背景与目标

CosmoEdge 当前已有单元测试、CI 构建检查、接口推送测试服务，以及测试用例文档中定义的性能压测和稳定性验证要求。但对于真实业务场景，例如玩手机检测、绊线检测、安全帽检测、区域入侵等，仍缺少一套可复现、可对比、可自动生成报告的压测工具。

本方案建议建设一个面向场景任务的压测工具，工具运行在控制 PC 上，通过现有 HTTP API、MQTT/HTTP 推送能力和视频源控制能力驱动边缘设备完成压测。

目标不是重新实现一个完整配置平台，而是围绕已导出的场景任务模板建立可复现测试流程：

```text
场景模板 + 视频样本 + 并发阶梯 + 指标采样 + 自动判定 + 报告输出
```

## 二、总体架构

压测工具建议运行在控制 PC，而不是运行在边缘设备上。

```text
控制 PC
  ├─ scenario-bench 压测工具
  ├─ 测试视频/RTSP 推流器
  ├─ HTTP/MQTT 事件接收服务
  ├─ 报告生成器
  └─ 场景包与历史报告归档

        HTTP API / MQTT / RTSP

边缘设备 CosmoEdge
  ├─ cosmo-engine
  ├─ Web/API 服务
  ├─ 场景任务运行
  ├─ 算法推理与事件上报
  └─ 硬件资源采样
```

控制 PC 负责压测编排、视频源准备、事件接收和报告生成。边缘设备只负责运行 CosmoEdge 业务服务与算法任务，避免压测工具自身消耗影响设备端性能数据。

## 三、运行边界

### 控制 PC 负责

- 解压并解析场景导出包。
- 导入或更新算法编排模板。
- 创建或复用视频通道。
- 将同一场景任务批量绑定到多路通道。
- 按压测阶梯启停任务。
- 周期采集任务运行详情和硬件资源。
- 接收 HTTP/MQTT 报警事件。
- 生成 JSON、HTML 测试报告，并保留后续扩展 XLSX/图表报告的空间。

### 边缘设备负责

- 提供 CosmoEdge HTTP API。
- 运行视频接入、解码、推理、OSD、事件上报链路。
- 提供 `/gtw/cwai/Task/RunningDetail` 任务运行详情。
- 提供 `/gtw/cwai/System/QueryHardwareResource` 硬件资源指标。
- 将事件推送到控制 PC 的 HTTP/MQTT 接收服务。

## 四、场景包规范

建议将一个可复现压测场景定义为一个目录，而不是单个导出 JSON。

```text
scenarios/play-phone/
  algorithm-template.json
  scenario.yml
  play-phone.mp4
```

### 算法模板 JSON

该文件来自系统导出的场景任务或算法编排包。例如“玩手机检测”导出包中包含：

- `algorithmCode`
- `algorithmName`
- `algorithmCategory`
- `algorithmUsage`
- `algorithmMetadata`
- `algorithmProcessdata`
- `atomicList`
- `confVersionId`
- `confVersionName`
- `remark`

这些字段可直接映射到现有编排保存接口 `/gtw/cwai/algorithm/layout/save`。

### scenario.yml

`scenario.yml` 是唯一配置入口，描述通道池、任务列表、容量扫描范围和判定阈值。单任务和多任务使用同一套模型；单任务只是 `tasks` 里只有一个元素。

```yaml
name: play-phone
displayName: 玩手机检测
sampleIntervalSec: 5

channels:
  mode: local
  repeatCount: 0
  sources:
    - name: play-phone-01
      file: play-phone.mp4

tasks:
  - id: play-phone
    displayName: 玩手机检测
    type: cv
    algorithmId: "22"
    scheduleId: "e89c6c6385e5454b35cde0d1653vg"
    template: algorithm-template.json

loadProfile:
  - channels: 1
    holdSec: 60
  - channels: 4
    holdSec: 120
  - channels: 8
    holdSec: 120
  - channels: 16
    holdSec: 600

thresholds:
  pass:
    maxCriticalPathLatencyMs: 200
    maxDetectorLatencyMs: 150
    avgDiscardRate: 0.02
    maxPacketDiscardRate: 0.01
```

### 输入模式

`channels.mode` 描述视频源来源。提供三种 `mode`，对应不同的测试目的，而非简单的好坏之分。

::: tip 取帧率基准的实测结论
编排模板 `AA_00001` 节点的 `configObject.params.fps`（例如 `3`）是算法节点的取帧节流参数。但在 `local` 模式下真机实测：本地文件以**源视频原生帧率全速解码推帧**（实测 ~42fps，而编排设定 fps=3），算法节点的 fps 节流未对 demux 层生效。因此：

- 对 CV 任务，`targetFps`（编排设定值）仍作为 `minFpsRatio` 的分母，但其语义在 local 模式下是**下限基准**（实测 fps 必然远大于它，ratio 通常 ≫ 1，该阈值恒 PASS，仅用于兜底发现"完全跑不动"的情况）。
- 对直接 VLM 任务，工具会把 `targetFps` 注入为 `param.videoReadFps`（`Keys.h` 的 `CHANNEL_SOURCE_FPS`），让 demux 按分析频率取帧，避免 0.1fps 级别任务被源视频原生帧率压爆。
- 对包含 VLM 的 workload，`loadProfile[].holdSec` 省略时默认 60s；纯 CV workload 仍默认 30s。显式配置始终优先，例如需要长一点的稳态窗口时可写 120s。
- CV 容量判断仍主要看**并发路数上去后实测 fps 是否被压低、丢弃率是否上升**；VLM 容量判断看稳定窗口内的推理完成吞吐、采样缺失率和可选端到端延时。
:::

| mode | 用途 | 能覆盖 | 不能覆盖 | 判定指标侧重 |
| --- | --- | --- | --- | --- |
| `local` | 短时容量基准、可复现对比（MVP 默认） | NPU/CPU/业务内存/模型内存、eMMC、算法链路短时稳定性 | RTSP 摄取链路韧性、长稳循环、`packetDiscardUtilization`（恒为 0） | FPS 天花板、丢弃率、资源曲线 |
| `rtsp-fidelity` | 生产保真验证 | RTSP 解码、丢包、断流重连、网络韧性 | 输入可复现性（受网络与源影响，对比性弱） | 丢包率、重连次数、摄取链路稳定性 |
| `rtsp-deterministic` | 可复现 + 走网络摄取路径（折中） | 本地文件经 ffmpeg/MediaMTX 打包为 RTSP，内容可控又走完整网络摄取链路 | 真实摄像头的忙闲波动 | 兼顾容量指标与摄取链路指标 |

选择建议：

- `local`：MVP 与日常短时回归的唯一模式。输入完全一致，跨版本/跨设备对比才有效，没有网络抖动混淆变量。本地视频默认只读 1 轮，读完后任务自动停止；但工具已通过 `ApplyParamsBatch` 的 `taskConfig.params` 注入 `param.videoRepeatCount=0`（见 `Keys.h` 的 `CHANNEL_SOURCE_REPEAT`，`AlgChannelDemux` 将 `video_repeat_count_ <= 0` 视为无限循环），使 local 视频持续循环产生负载。可在 `scenario.yml` 的 `channels.repeatCount` 覆盖（如设为 `1` 做单遍验证）。即便如此，同一短片段反复播放画面复杂度恒定、`packetDiscardUtilization` 恒为 0，只适合容量隔离测试，不宜作为 24h/72h 长稳默认模式。
- `rtsp-fidelity`：专项验证摄取链路，与容量压测分开排期，判定标准也不同（看丢包/重连，而非 FPS 天花板）。
- `rtsp-deterministic`：当你既要可复现又要测网络韧性时使用。需要额外部署推流服务，见第八节运行依赖。

```yaml
channels:
  mode: local            # local | rtsp-fidelity | rtsp-deterministic
  repeatCount: 0
  sources:
    # local 模式：设备本地视频文件（Camera/AddVideo）。
    # file: 指向运行压测工具的控制机文件。工具先查询上传能力，再分片上传，
    #       最后用服务端签发的 uploadId 调用 Camera/AddVideo 建渠道。
    # 安全边界不接受调用方指定的设备侧 filePath。
    - name: play-phone-01
      file: play-phone.mp4
```

## 五、核心执行流程

```text
1. 登录设备，获取 token
2. 查询设备信息（devInfoList），记录设备型号、软件版本、SN
3. 解析场景包 algorithm-template.json（含二次 JSON.parse 与 fps 提取）
4. 导入或更新算法编排模板（layout/save，或 --skip-import 跳过）
5. 创建视频通道（local: uploadTemp 分片上传 → Camera/AddVideo；rtsp: Camera/Add）
6. 按阶梯增量绑定：每步对新增通道 ApplyParamsBatch（带 videoRepeatCount=0，自动开启并循环）
7. 每步保持期周期采样 RunningDetail + HardwareResource
8. 接收或查询事件结果
9. 阶梯结束统一 BatchSwitchTask 全关，并清理本次创建的通道（--cleanup）
10. 输出 metrics.json、summary.json、report.html
```

## 六、现有接口映射

### 登录

```text
POST /gtw/cwai/login/dologin
```

用于获取后续请求所需鉴权信息。

### 设备信息

```text
POST /gtw/cwai/System/QueryDeviceInfo
```

用于记录设备型号、固件版本、软件版本、设备 SN、运行时长。

### 硬件资源

```text
POST /gtw/cwai/System/QueryHardwareResource
```

现有实现可返回：

- `cpuUtilization`
- `generalMemoryUtilization`
- `npuUtilization`
- `modelMemoryUtilization`
- `pictureMemoryUtilization`
- `TPPMemoryUtilization`
- `eMMCUtilization`
- `packetDiscardUtilization`

### 算法编排导入或更新

MVP 阶段建议由工具解析导出 JSON 后调用：

```text
POST /gtw/cwai/algorithm/layout/save
```

后续可补正式导入接口：

```text
POST /gtw/cwai/algorithm/layout/import
```

当前前端已有该接口调用，但后端路由和 `IAlgorithmLayout` 尚未补齐导入方法。

### 视频通道

普通通道：

```text
POST /gtw/cwai/Camera/Add
```

本地视频通道：建渠道分**两步**（真机验证）。所有请求都需要携带当前登录会话的 `mtk`。

第一步，先查询 `uploadCapabilities`，再按服务端返回的 `maxChunkSize` 分片上传。当前默认分片为 8 MB（8 × 1024 × 1024 bytes），这只是单次请求边界，不是视频总量上限：

```text
POST /gtw/cwai/atomic/model/uploadCapabilities
POST /gtw/cwai/atomic/model/uploadTemp
# 首片字段: file(blob), purpose=video, chunkIndex=0, totalChunks,
#           totalSize, chunkSize, clientRequestId
# 后续分片额外携带服务端返回的 uploadId
# 响应字段: resData.uploadId, nextChunkIndex, complete
```

`clientRequestId` 在同一文件的重试和重启恢复期间必须稳定；服务端返回的 `uploadId` 是不透明标识，客户端不得推导或访问设备临时路径。

第二步，用最终的 `uploadId` 建本地视频渠道：

```text
POST /gtw/cwai/Camera/AddVideo
```

当前 HTTP 合同：

```json
{
  "channelName": "压测通道01",
  "channelCode": "benchmark-01",
  "uploadId": "<server-issued-upload-id>"
}
```

后端会校验该会话属于当前用户、用途为 `video` 且内容未被替换，再从受管文件中获取路径和大小。`filePath`/`contentLength` 仅保留旧版兼容入口，新工具不得发送或记录服务端文件系统路径。

响应 `resData.id` 即视频通道 ID（`videoChannelId`）。

### 任务绑定

单路保存或更新：

```text
POST /gtw/cwai/Task/SaveOrUpdate
```

多路复制：

```text
POST /gtw/cwai/Task/ApplyParamsBatch
```

多路复制适合压测工具使用。它会把同一 `taskConfig`、`scheduleId`、`algorithmId` 应用到多个 `targetChannelIds`，并自动启用任务。`taskConfig.params` 还会下发到通道层（`AlgChannel::SetParams`），可携带 `param.videoRepeatCount`（循环次数，0=无限）和 `param.videoReadFps`（取帧率）等动态参数。

请求体必须包含顶层 `channelId`（取 `targetChannelIds[0]`），否则 `MsgChannelTask::from_json` 的 `j.at("channelId")` 报 code 24（参数异常）：

```json
{
  "channelId": "LX0000000001",
  "algorithmId": "15",
  "scheduleId": "e89c6c6385e5454b35cde0d1653vg",
  "taskConfig": {
    "params": [{ "key": "param.videoRepeatCount", "value": "0" }],
    "areas": []
  },
  "targetChannelIds": ["LX0000000001", "LX0000000002"]
}
```

### 绑定策略：增量绑定（实测结论）

阶梯压测的绑定策略经过真机验证后做了关键调整。原计划是"一次性绑定全部通道→全关→逐级打开"，但实测发现 **local（本地文件）视频任务一旦 `TaskStop` 后再 `SwitchTask(enable=1)` 不会重新打开 demux、任务不会恢复运行**（`CameraServiceImpl::MonitorCameraEntity` 在 `AlgDemuxReadEnd` 时 `TaskStop` 并置 `is_enabled_=false`；`TaskStop` 拆解 demux，`TaskStart` 不会重启已读尽的本地文件）。因此 OFF→ON 路径对 local 视频无效。

MVP 改用**增量绑定**：每个阶梯步只对本步新增的通道调用 `ApplyParamsBatch`（带 `videoRepeatCount=0` 自动开启并持续循环），已绑定的通道保持运行不被打断。整个压测期间**不对运行中的通道做 OFF→ON**，仅在最终 teardown 时统一 `BatchSwitchTask` 全关。这样每路 local 视频只开一次、持续循环到压测结束，采样全程有效。

`taskId` 约定为 `videoChannelId + "_" + algorithmCode`（如 `LX0000000001_15`），工具在本地计算，`RunningDetail` 按 `taskId` 过滤即可，无需额外查询。

后续工程化阶段可补一个"批量保存但不启用"的任务接口，使 rtsp 模式也能用全量绑定+逐级打开的策略。

### 任务启停

单路启停：

```text
POST /gtw/cwai/Task/SwitchTask
```

批量启停：

```text
POST /gtw/cwai/Task/BatchSwitchTask
```

示例：

```json
{
  "tasks": [
    {
      "id": "bench-01",
      "channelId": "channel-01",
      "algorithmId": "22",
      "enable": 1
    }
  ]
}
```

### 任务运行详情

```text
POST /gtw/cwai/Task/RunningDetail
```

请求体只需一个字段，用于过滤要采样的任务 ID。注意这里传的是 `taskId`，不是 `channelId`：

```json
{ "tasks": ["task-01", "task-02"] }
```

工具需要在任务绑定阶段记录 `channelId -> taskId` 映射，后续报告可仍按通道展示，但调用 `RunningDetail` 时必须使用 `taskId` 过滤。

该接口可返回每个任务的 action 状态、队列吞吐、丢弃计数、报警计数和节点耗时信息。压测工具可从以下字段计算吞吐和稳定性：

- `insertCountPeriod`
- `processCountPeriod`
- `discardCountPeriod`
- `periodMs`
- `holdCount`
- `alarmCount`
- `nodeDurationInfos`

实现注意事项：

- 接口会**静默过滤掉 action 数量 ≤ 2 的任务**，使其不出现在响应中。压测时若发现某路通道采不到样，通常是因为该场景的 action 节点不足，而非任务未启动。工具应在采样前统计预期通道数，与响应数量做差并对缺失路标记为"采样缺失"，避免误判。
- `processCountPeriod / (periodMs / 1000)` 可用于推算实测处理帧率，与编排模板的设定 fps 对比。注意 `periodMs` 是累计窗口（从任务启动起算），不是固定周期；更稳健的做法是用相邻两次采样的 `processCount`（累计值）差分除以采样间隔，两者实测一致。
- `nodeDurationInfos` 的耗时字段为 **`durationAvgUs`（微秒）**，换算毫秒需 `/1000`。报告中 `maxAvgNodeLatencyMs` 取各节点 `durationAvgUs` 之和（近似端到端链路耗时）。
- 接口当前**不返回 task 级实测 FPS 与连续丢帧统计**（源数据中有 `maxTaskFps`、`continuousDiscardCountMax`，但未映射到对外响应）。因此报告中的"实测 FPS"为推算值；连续丢弃最长持续时间这一稳定性指标需在第三阶段后端增强后才能支持。

### 事件验证

系统事件查询：

```text
POST /gtw/cwai/event/page
```

事件导出：

```text
POST /gtw/cwai/event/ExportAlarm
```

HTTP 推送配置：

```text
POST /gtw/cwai/System/SetHttpInterfaceParam
```

MQTT 推送配置：

```text
POST /gtw/cwai/System/SetMqttAdapterParam
```

事件接收服务现有 `test/push-test-service`（HTTP 接收 + 内置 MQTT broker），但其事件仅通过 socket.io 在前端 UI 实时展示，**没有暴露可被程序查询的统计接口**（如按时间段返回收到的事件数）。压测工具需要的是事后统计而非实时查看，因此不能直接复用，二选一：

1. 为 `test/push-test-service` 增加一个 `/stats` 端点（按 scenarioId / 时间窗聚合事件计数），压测工具读取该端点；或
2. 工具自带一个轻量 HTTP/MQTT 接收器（仅计数 + 落盘 JSONL），不复用现有服务。

推荐方案 1，避免重复造轮子。注意该服务实际路径为 `test/push-test-service`（其自身 README 误写为 `scripts/push-test-service`）。

## 七、报告指标

### 基础信息

- 测试时间
- 测试人员或执行机器
- 设备型号
- 软件版本
- 固件版本
- 场景名称
- 算法 ID 和算法版本
- 视频规格
- 并发阶梯

### 性能指标

- 每阶段通道数
- 每路平均 FPS
- 每路最低 FPS
- 总处理 FPS
- 队列输入、处理、丢弃计数
- 丢弃率
- 节点平均耗时
- 节点最大耗时
- 报警数量

### 资源指标

- CPU 使用率曲线
- 内存使用率曲线
- NPU 使用率曲线
- 模型内存、图片内存、TPP 内存曲线
- 磁盘使用率
- 系统丢包率

### 稳定性指标

- 任务异常次数
- 任务重启次数
- 设备 API 超时次数
- 事件推送失败次数
- 内存增长斜率
- 连续丢弃最长持续时间

### 判定结果

每个阈值都应输出：

- 阈值
- 实测值
- 结果：PASS 或 FAIL
- 失败原因

## 八、工具实现建议

建议新增：

```text
tools/scenario-bench/
  package.json
  README.md
  src/
    cli.js
    cosmo-client.js
    logger.js
    scenario-package.js
    channel-manager.js
    task-runner.js
    task-strategies.js
    metrics-sampler.js
    report-writer.js
```

模块职责：

- `cosmo-client.js`：封装 CosmoEdge HTTP API。请求头携带 `mtk`/`token`，鉴权惯例参考前端 `src/web/src/utils/request.js`。
- `logger.js`：命令行日志输出。
- `scenario-package.js`：解析 `scenario.yml` 和任务模板。模板中 `algorithmProcessdata`、`atomicList` 等字段值可能为 JSON 字符串，需二次 `JSON.parse`；取帧率基准从 `AA_00001` 节点的 `configObject.params` 中 `fps` 项提取。
- `channel-manager.js`：创建、查询和复用视频通道。`local` 模式走 `Camera/AddVideo`，`rtsp-*` 模式走 `Camera/Add`。
- `task-runner.js`：任务绑定、批量启停、阶梯压测。多路并发启停需处理时序、超时与失败回滚。
- `task-strategies.js`：封装 CV/VLM 的吞吐、延时和阈值判定策略。
- `metrics-sampler.js`：采样运行详情和硬件资源。注意 action 数量 ≤ 2 的任务会被 RunningDetail 静默过滤。
- `report-writer.js`：输出 JSON、HTML 报告。

`event-listener.js`、`streamer.js`、XLSX 报告和图表报告属于后续扩展，不作为当前默认工具面的一部分。

### 运行依赖

- `local` 模式（MVP 默认）：无额外依赖，仅需将视频样本上传到设备本地。
- `rtsp-deterministic` 模式：控制 PC 需部署 ffmpeg 与 MediaMTX（或 rtsp-simple-server）。
- `rtsp-fidelity` 模式：真实摄像头或独立推流源。

## 九、命令行示例

```bash
node tools/scenario-bench/src/cli.js run \
  --device http://192.168.1.10:8080 \
  --user admin \
  --password admin \
  --scenario scenarios/play-phone \
  --output reports/play-phone-20260630
```

## 十、实施计划

### 第一阶段：MVP

- 支持解析 `scenario.yml` 和任务模板 JSON（处理字段值为 JSON 字符串的二次解析，提取编排设定 fps 作为吞吐基准）。
- 支持调用 `/algorithm/layout/save` 导入或更新编排。
- 支持 `local` 模式（`uploadTemp` 分片上传 + `Camera/AddVideo`，`videoRepeatCount=0` 循环）作为短时容量基准的默认与唯一视频源模式。
- 支持 `ApplyParamsBatch` 增量绑定任务（每阶梯只绑定新增通道，已运行通道不打断）。
- 支持阶梯压测（增量绑定 + 保持期采样 + 最终全关）。
- 支持采样 `RunningDetail` 和 `QueryHardwareResource`（处理 action 数量 ≤ 2 被过滤的缺失路标记）。
- 输出 `metrics.json` 和基础 HTML 报告。

### 第二阶段：报告完善与链路验证

- 增加 XLSX 报告。
- 增加资源曲线图。
- 增加节点耗时排名。
- 增加 PASS/FAIL 阈值判定（基于 `minFpsRatio` 等阈值与编排设定 fps 比对）。
- 接入 `test/push-test-service` 统计 HTTP/MQTT 事件（需为其补 `/stats` 端点）。
- 增加 `rtsp-fidelity` / `rtsp-deterministic` 两种模式，专项验证摄取链路丢包率、重连次数；长稳测试优先落在 `rtsp-deterministic` 循环推流模式。

### 第三阶段：工程化

- 补后端 `/algorithm/layout/import`。
- 补本地视频 OFF→ON 重启能力（当前 local 任务 stop 后无法再 start，只能靠增量绑定规避；补齐后 rtsp 模式也可用全量绑定+逐级打开策略）。
- 增强 `RunningDetail`，直接返回 task 级实测 FPS、连续丢帧统计、stream live、last frame 时间。
- 支持多设备对比。
- 支持 nightly 长稳测试。
- 将历史报告纳入版本基线比较。

## 十一、风险与注意事项

- 场景模板必须版本化，避免算法模板变化导致性能结果不可比。导出包顶层 `configVersionList` 可作为算法版本基线。
- 视频样本必须固定，避免输入源差异导致误判性能波动。`local` 模式的可复现性最强，应作为短时跨版本/跨设备对比的基准模式。
- `local` 模式已支持循环播放（`param.videoRepeatCount=0`），但 local 视频任务**不能 OFF 后再 ON**（demux 不会重启本地文件）。因此压测采用增量绑定、全程不关任务，仅最终 teardown 全关。同一短片段反复播放画面复杂度恒定、事件在每一轮同一时刻触发、`packetDiscardUtilization` 恒为 0，只适合容量隔离测试，不能模拟真实部署的忙闲波动。
- `local` 模式下 CV 默认仍以**源视频原生帧率全速推帧**（实测 ~42fps，编排设定 fps=3 未在 demux 层生效）。直接 VLM 任务会自动注入 `param.videoReadFps=targetFps`，否则低频大模型会被全速输入压出不符合手工测试的高延时。
- VLM 的默认阶梯保持时间为 60s，避免 30s 窗口被模型加载、低频完成计数和采样边界放大；需要压长稳态时仍应在场景包中显式写更长时间。
- `rtsp-*` 模式下控制 PC 推流压力需要监控，避免视频源自身成为瓶颈。
- 长稳测试应记录设备重启、服务重启和 API 超时情况。
- 第一版不要开放过多业务参数，避免压测工具演变成另一个配置系统。

## 十二、结论

当前工程已经具备场景任务压测所需的大部分基础能力，包括通道创建、任务批量绑定、任务启停、运行详情采样、硬件资源采样和事件推送验证。真正需要新增的是控制 PC 侧的自动化编排工具、场景包规范和标准报告输出。

推荐优先以外部工具方式落地 `scenario-bench`，先完成一个典型场景的可复现压测闭环，再逐步补齐后端导入接口和更细粒度的运行指标。
