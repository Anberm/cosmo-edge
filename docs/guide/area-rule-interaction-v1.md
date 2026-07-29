# 区域规则交互 V1 兼容契约

区域规则交互 V1 只重构前端表达，不修改 `BA_00005` 的运行时协议。

## 兼容原则

- 保留 `areaAlarmType=0..6` 的现有含义和编号。
- 通过 `areaAlarmType=1` 与 `countBreakAreaType` 的组合区分区域数量统计和进出流量统计。
- `areaAlarmType=6` 作为旧版多节点数量条件读取和写回，不自动迁移为类型 0。
- 无法识别的枚举进入保护状态，不允许普通表单静默覆盖。
- 保存采用原参数与本次表单结果合并的方式；隐藏、非当前规则及未知扩展字段继续保留。
- 区域规则只产生候选告警或统计数据，最终事件仍由下游事件上报节点过滤和生成。

## UI 与运行时映射

| UI 规则 | 运行时识别/写回 |
| --- | --- |
| 区域目标数量条件 | `areaAlarmType=0` |
| 区域目标数量条件（旧版多节点） | `areaAlarmType=6` |
| 区域目标数量统计 | `areaAlarmType=1, countBreakAreaType=0` |
| 进出流量统计 | `areaAlarmType=1, countBreakAreaType=103` |
| 目标越线 | `areaAlarmType=2` |
| 垂直方向异常 | `areaAlarmType=3` |
| 目标区域停留 | `areaAlarmType=4` |
| 区域内存在目标 | `areaAlarmType=5` |

## V1 明确不做

- 不修改区域判断和事件上报的 C++ 执行逻辑。
- 不实现“目标数量变化立即绕过统计周期上报”；界面按当前真实行为描述为周期数据变化标记。
- 不让 `param.targetCalcType` 决定统计分支；当前真实分支仍由 `countBreakAreaType` 决定。
- 不删除类型 6 或任何历史参数键。
- 不批量迁移或重写已保存任务。

## 验证

在 `src/web` 下运行：

```bash
npm run area-rule:check
npm run build
```

兼容检查覆盖类型 1 拆分、类型 6 保留、未知类型保护和未知字段无损合并。
