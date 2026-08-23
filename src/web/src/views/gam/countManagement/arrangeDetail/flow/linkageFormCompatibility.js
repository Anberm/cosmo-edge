const ALARM_ALGORITHM_KEYS = new Set(['algs', 'strageAlgorithms'])
const ALARM_ACTION_IDS = new Set(['LA_AlarmData_Code', 'EVT_00001'])

export const isAlarmAlgorithmsKey = (key) =>
  ALARM_ALGORITHM_KEYS.has(String(key || ''))

export const isAlarmDataAction = (actionId) =>
  ALARM_ACTION_IDS.has(String(actionId || ''))

export const collectAlarmAlgorithms = (selectedChannels = []) => {
  const result = []
  const walk = (nodes, channelId = '') => {
    ;(nodes || []).forEach((node) => {
      const currentChannelId = node?.channelId || channelId || node?.id || ''
      if (Array.isArray(node?.children) && node.children.length) {
        walk(node.children, currentChannelId)
      } else if (currentChannelId && node?.algorithmId) {
        result.push({
          channelId: String(currentChannelId),
          algorithmId: String(node.algorithmId)
        })
      }
    })
  }
  walk(selectedChannels)
  return result
}
