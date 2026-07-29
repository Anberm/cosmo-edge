import assert from 'node:assert/strict'
import {
  AREA_RULE_UI_TYPES,
  applyAreaRuleUiType,
  buildAreaRuleSummary,
  inferAreaRuleUiType,
  mergeParamsPreservingUnknown
} from '../src/views/gam/countManagement/arrangeDetail/flow/areaRuleCompatibility.js'

const legacy = [
  { key: 'areaAlarmType', value: '6' },
  { key: 'areaLimitTargetCount', value: '2' },
  { key: 'vendor.extension', value: 'keep-me' }
]
assert.equal(inferAreaRuleUiType(legacy), AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT)
assert.match(
  buildAreaRuleSummary(legacy, AREA_RULE_UI_TYPES.LEGACY_TARGET_LIMIT),
  /有效目标数 < 2/
)

const compareCases = [
  ['0', '<'],
  ['1', '>'],
  ['2', '≤'],
  ['3', '≥'],
  ['4', '=']
]
compareCases.forEach(([compare, symbol]) => {
  const summary = buildAreaRuleSummary(
    [
      { key: 'param.areaLimitTargetCount', value: '3' },
      { key: 'param.areaLimitTargetType', value: compare }
    ],
    AREA_RULE_UI_TYPES.TARGET_LIMIT
  )
  assert.match(summary, new RegExp(`有效目标数 ${symbol} 3`))
})

const areaCount = applyAreaRuleUiType(
  [
    { key: 'areaAlarmType', value: '1' },
    { key: 'countBreakAreaType', value: '103' },
    { key: 'vendor.extension', value: 'keep-me' }
  ],
  AREA_RULE_UI_TYPES.AREA_COUNT
)
assert.equal(inferAreaRuleUiType(areaCount), AREA_RULE_UI_TYPES.AREA_COUNT)
assert.equal(areaCount.find((item) => item.key === 'vendor.extension').value, 'keep-me')

const passFlow = applyAreaRuleUiType(areaCount, AREA_RULE_UI_TYPES.PASS_FLOW)
assert.equal(inferAreaRuleUiType(passFlow), AREA_RULE_UI_TYPES.PASS_FLOW)

const merged = mergeParamsPreservingUnknown(
  [
    { key: 'known', value: 'old' },
    { key: 'unknown', value: 'preserved' }
  ],
  [{ key: 'known', value: 'new' }]
)
assert.deepEqual(merged, [
  { key: 'known', value: 'new' },
  { key: 'unknown', value: 'preserved' }
])

const unknown = [{ key: 'areaAlarmType', value: '99' }]
assert.equal(inferAreaRuleUiType(unknown), AREA_RULE_UI_TYPES.UNKNOWN)
assert.deepEqual(applyAreaRuleUiType(unknown, AREA_RULE_UI_TYPES.UNKNOWN), unknown)

console.log('area rule compatibility checks passed')
