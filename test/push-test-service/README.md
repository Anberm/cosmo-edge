# AI Box 推送测试服务

这个小服务用于联调平台对外推送能力：

- HTTP：接收事件推送，打印关键字段，并返回平台期望的 JSON。
- MQTT：内置一个本地 MQTT broker，接收设备注册、心跳、业务消息，并按协议自动回包。

## 启动

```bash
cd test/push-test-service
npm install
npm start
```

默认监听：

- HTTP: `http://127.0.0.1:18080/events`
- MQTT: `127.0.0.1:1883`

可以通过环境变量调整：

```bash
HTTP_PORT=18080 MQTT_PORT=1883 npm start
```

Windows PowerShell:

```powershell
$env:HTTP_PORT="18080"; $env:MQTT_PORT="1883"; npm start
```

## 平台侧配置建议

HTTP 推送地址配置为：

```text
http://测试机IP:18080/events
```

MQTT 地址配置为：

```text
测试机IP:1883
```

测试服务会关注这些 MQTT topic：

- `/d2p/aibox`
- `/d2p/aibox/heartbeat`
- `/p2d/aibox/{sn}`
- `/p2d/aibox/heartbeat/{sn}`

## 常用验证

HTTP 手工发送一条事件：

```bash
curl -X POST http://127.0.0.1:18080/events \
  -H "Content-Type: application/json" \
  -d "{\"messageId\":\"msg-1\",\"recordId\":\"rec-1\",\"devId\":\"box-1\",\"algorithmCode\":\"face\",\"property\":{\"face\":{\"confidence\":0.98}}}"
```

携带检测目标（`targets`，对应入侵/越界等检测类告警，commit `3fcabdc8`）：

```bash
curl -X POST http://127.0.0.1:18080/events \
  -H "Content-Type: application/json" \
  -d "{\"messageId\":\"msg-2\",\"recordId\":\"rec-2\",\"devId\":\"box-1\",\"algorithmName\":\"入侵检测\",\"category\":\"alarm\",\"targets\":[{\"label\":\"person\",\"confidence\":0.93,\"trackId\":\"T12\",\"box\":{\"x\":120,\"y\":80,\"width\":240,\"height\":360}}],\"property\":{}}"
```

页面上每条告警卡片会展示：

- 头部算法名旁的 `🎯 N` 徽标 = 本事件的检测目标数量；
- 「检测目标」行：每个目标一个徽标，含类别、置信度、跟踪 ID，鼠标悬停可看像素目标框 `(x, y, w, h)`。

无独立目标的统计类事件（如人流统计）设备端不下发 `targets`，卡片上不显示该区块。

服务正常时返回：

```json
{"resCode":1,"resMsg":[]}
```
