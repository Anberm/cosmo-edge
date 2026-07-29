export const AREA_RULE_UI_TYPES = Object.freeze({
  TARGET_LIMIT: 'target-limit',
  AREA_COUNT: 'area-count',
  PASS_FLOW: 'pass-flow',
  TRIPWIRE: 'tripwire',
  DIRECTION: 'direction',
  DURATION: 'duration',
  PRESENCE: 'presence',
  LEGACY_TARGET_LIMIT: 'legacy-target-limit',
  UNKNOWN: 'unknown'
})

const getParamValue = (params, key, fallback = '') => {
  const item = (Array.isArray(params) ? params : []).find((param) => param?.key === key)
  return item?.value ?? fallback
}

const setParamValue = (params, key, value) => {
  const item = params.find((param) => param?.key === key)
  if (item) {
    item.value = String(value)
  } else {
    params.push({ key, value: String(value) })
  }
}

export const inferAreaRuleUiType = (params) => {
  const alarmType = String(getParamValue(params, 'areaAlarmType', '0'))
  if (alarmType === '0') return AREA_RULE_UI_TYPES.TARGET_LIMIT
  if (alarmType === '2') return AREA_RULE_UI_TYPES.TRIPWIRE
  if (alarmType === '3') return AREA_RULE_UI_TYPES.DIRECTION
  if (alarmType === '4') return AREA_RULE_UI_TYPES.DURATION
  if (alarmType === '5') return AREA_RULE_UI_TYPES.PRESENCE
  if (alarmType === '6') return AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT
  if (alarmType !== '1') return AREA_RULE_UI_TYPES.UNKNOWN

  const breakType = String(getParamValue(params, 'countBreakAreaType', ''))
  if (breakType === '0') return AREA_RULE_UI_TYPES.AREA_COUNT
  if (breakType === '103') return AREA_RULE_UI_TYPES.PASS_FLOW
  return AREA_RULE_UI_TYPES.UNKNOWN
}

export const getAreaRulePurpose = (type) => {
  if ([AREA_RULE_UI_TYPES.AREA_COUNT, AREA_RULE_UI_TYPES.PASS_FLOW].includes(type)) {
    return 'statistics'
  }
  if (type === AREA_RULE_UI_TYPES.UNKNOWN) return 'compatibility'
  return 'alarm'
}

/**
 * Patch only the discriminator fields understood by the existing runtime.
 * All inactive, legacy and unknown fields are intentionally retained.
 */
export const applyAreaRuleUiType = (params, type) => {
  const next = (Array.isArray(params) ? params : []).map((param) => ({ ...param }))
  const alarmTypeByUiType = {
    [AREA_RULE_UI_TYPES.TARGET_LIMIT]: '0',
    [AREA_RULE_UI_TYPES.AREA_COUNT]: '1',
    [AREA_RULE_UI_TYPES.PASS_FLOW]: '1',
    [AREA_RULE_UI_TYPES.TRIPWIRE]: '2',
    [AREA_RULE_UI_TYPES.DIRECTION]: '3',
    [AREA_RULE_UI_TYPES.DURATION]: '4',
    [AREA_RULE_UI_TYPES.PRESENCE]: '5',
    [AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT]: '6'
  }
  const alarmType = alarmTypeByUiType[type]
  if (alarmType === undefined) return next

  setParamValue(next, 'areaAlarmType', alarmType)
  if (type === AREA_RULE_UI_TYPES.AREA_COUNT) {
    setParamValue(next, 'countBreakAreaType', '0')
  } else if (type === AREA_RULE_UI_TYPES.PASS_FLOW) {
    setParamValue(next, 'countBreakAreaType', '103')
  }
  return next
}

export const mergeParamsPreservingUnknown = (original, generated) => {
  const result = (Array.isArray(generated) ? generated : []).map((param) => ({ ...param }))
  const generatedKeys = new Set(result.map((param) => param?.key))
  ;(Array.isArray(original) ? original : []).forEach((param) => {
    if (!generatedKeys.has(param?.key)) result.push({ ...param })
  })
  return result
}

export const mergeMetaParamsPreservingUnknown = (original, generated) =>
  mergeParamsPreservingUnknown(original, generated)

const AREA_RULE_FIELD_TEXT = Object.freeze({
  inputAreaType: {
    name: '判断区域',
    description: '选择使用主区域还是关联区域进行规则判断。'
  },
  'param.areaLimitTargetCount': {
    name: '目标数量阈值',
    description: '用于和区域内实际有效目标数量比较。'
  },
  'param.areaLimitTargetType': {
    name: '触发条件',
    description: '选择实际目标数量与阈值之间的比较关系。'
  },
  'param.areaLimitDuration': {
    name: '条件持续时间',
    description: '数量条件需要连续满足的时间；0表示按当前帧判断。'
  },
  'param.areaLimitDurationTimeType': {
    name: '持续时间单位',
    description: '条件持续时间使用的时间单位。'
  },
  'param.areaCalcDuration': {
    name: '统计上报周期',
    description: '周期性产生区域数量或进出流量统计数据。'
  },
  'param.areaCalcDurationTimeType': {
    name: '上报周期单位',
    description: '统计上报周期使用的时间单位。'
  },
  breakAreaType: {
    name: '越线方向',
    description: '目标按警戒线标记方向穿越时命中。'
  },
  'param.trippingWireType': {
    name: '触发所需警戒线数量',
    description: '同一跟踪目标需要穿越的警戒线数量。'
  },
  durationBreakAreaType: {
    name: '停留开始条件',
    description: '选择只要求目标位于区域内，还是必须先观察到目标从区域外进入。'
  },
  'param.areaDuration': {
    name: '最短停留时间',
    description: '同一跟踪目标在区域内连续停留多久后命中。'
  },
  'param.areaDurationTimeType': {
    name: '停留时间单位',
    description: '最短停留时间使用的时间单位。'
  },
  'param.retroDirect': {
    name: '禁止移动方向',
    description: '当前实现按画面上下方向判断，需要稳定的目标跟踪。'
  },
  'param.retroDistance': {
    name: '最小异常位移',
    description: '相对画面高度的比例；0.05表示画面高度的5%。'
  },
  areaLimitTargetCount: {
    name: '目标数量阈值',
    description: '旧版多节点配置使用的目标数量阈值。'
  },
  areaLimitTargetType: {
    name: '触发条件',
    description: '旧版多节点配置使用的数量比较关系。'
  },
  areaLimitDuration: {
    name: '条件持续时间',
    description: '旧版多节点配置使用的条件持续时间。'
  },
  areaDurationTimeType: {
    name: '持续时间单位',
    description: '旧版多节点配置使用的时间单位。'
  },
  targetCountChange: {
    name: '周期数据变化标记',
    description: '开启后，在周期上报时标记目标数量是否发生变化；当前不会绕过上报周期。'
  }
})

export const getAreaRuleFieldText = (key) => AREA_RULE_FIELD_TEXT[key] || null

const COMPARE_TEXT = {
  '0': '少于',
  '1': '多于',
  '2': '不多于',
  '3': '不少于',
  '4': '等于'
}

const COMPARE_SYMBOL = {
  '0': '<',
  '1': '>',
  '2': '≤',
  '3': '≥',
  '4': '='
}

const UNIT_TEXT = {
  '1': '毫秒',
  '1000': '秒',
  '60000': '分钟',
  '3600000': '小时'
}

export const getAreaRuleOptionLabel = (key, value, fallback) => {
  const stringValue = String(value)
  if (['param.areaLimitTargetType', 'areaLimitTargetType'].includes(key)) {
    return COMPARE_TEXT[stringValue] || fallback
  }
  if (key === 'param.trippingWireType') {
    return stringValue === '1' ? '穿越1条警戒线' : stringValue === '2' ? '穿越2条警戒线' : fallback
  }
  if (key === 'durationBreakAreaType') {
    return stringValue === '0' ? '目标位于区域内' : stringValue === '2' ? '目标从区域外进入' : fallback
  }
  if (key === 'param.retroDirect') {
    return stringValue === '0' ? '从画面下方向上移动' : stringValue === '1' ? '从画面上方向下移动' : fallback
  }
  return fallback
}

const summaryParamValue = (params, primaryKey, legacyKey, fallback = '未设置') =>
  getParamValue(params, primaryKey, getParamValue(params, legacyKey, fallback))

export const buildAreaRuleSummary = (params, type) => {
  const duration = (valueKey, unitKey) => {
    const value = getParamValue(params, valueKey, '0')
    const unit = UNIT_TEXT[String(getParamValue(params, unitKey, '1000'))] || '秒'
    return value === '0' ? '按当前帧判断' : `持续${value}${unit}`
  }
  if ([AREA_RULE_UI_TYPES.TARGET_LIMIT, AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT].includes(type)) {
    const legacy = type === AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT
    const count = summaryParamValue(
      params,
      legacy ? 'areaLimitTargetCount' : 'param.areaLimitTargetCount',
      legacy ? 'param.areaLimitTargetCount' : 'areaLimitTargetCount',
      '0'
    )
    const compare = summaryParamValue(
      params,
      legacy ? 'areaLimitTargetType' : 'param.areaLimitTargetType',
      legacy ? 'param.areaLimitTargetType' : 'areaLimitTargetType',
      '0'
    )
    const durationKey = legacy ? 'areaLimitDuration' : 'param.areaLimitDuration'
    const unitKey = legacy ? 'areaDurationTimeType' : 'param.areaLimitDurationTimeType'
    const expression = COMPARE_SYMBOL[String(compare)]
      ? `有效目标数 ${COMPARE_SYMBOL[String(compare)]} ${count}`
      : '有效目标数与阈值满足所选关系'
    return `将区域内有效目标数与目标数量阈值进行比较，支持 <、>、≤、≥、=。当前配置：${expression}；条件${duration(durationKey, unitKey)}时产生候选告警。`
  }
  if (type === AREA_RULE_UI_TYPES.AREA_COUNT) {
    return `按配置周期上报区域内当前有效目标数，包括目标数为0的情况；这是统计数据，不是异常告警。`
  }
  if (type === AREA_RULE_UI_TYPES.PASS_FLOW) {
    return `按配置周期上报警戒线的进入数、离开数和累计流量；该规则依赖稳定的目标跟踪。`
  }
  if (type === AREA_RULE_UI_TYPES.TRIPWIRE) {
    const count = getParamValue(params, 'param.trippingWireType', '1')
    return `同一跟踪目标按标记方向穿越${count}条警戒线时产生候选告警。`
  }
  if (type === AREA_RULE_UI_TYPES.DIRECTION) {
    const direction = getAreaRuleOptionLabel(
      'param.retroDirect',
      getParamValue(params, 'param.retroDirect', '0'),
      '指定方向'
    )
    const distance = Number(getParamValue(params, 'param.retroDistance', '0.05')) * 100
    return `同一目标${direction}超过画面高度的${Number.isFinite(distance) ? distance : 5}%时产生候选告警。`
  }
  if (type === AREA_RULE_UI_TYPES.DURATION) {
    const condition = getAreaRuleOptionLabel(
      'durationBreakAreaType',
      getParamValue(params, 'durationBreakAreaType', '0'),
      '位于区域内'
    )
    return `同一目标${condition}并${duration('param.areaDuration', 'param.areaDurationTimeType')}时产生候选告警。`
  }
  if (type === AREA_RULE_UI_TYPES.PRESENCE) {
    return '检测到任一上游有效目标位于判断区域内时产生候选告警；目标类别由上游模型和类别筛选决定。'
  }
  return '该配置包含当前界面无法识别的旧版规则值。为避免覆盖，建议使用兼容模式只读检查。'
}
