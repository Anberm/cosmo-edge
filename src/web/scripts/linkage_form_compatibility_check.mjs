import assert from 'node:assert/strict'
import {
  collectAlarmAlgorithms,
  isAlarmAlgorithmsKey
} from '../src/views/gam/countManagement/arrangeDetail/flow/linkageFormCompatibility.js'

assert.equal(isAlarmAlgorithmsKey('algs'), true)
assert.equal(isAlarmAlgorithmsKey('strageAlgorithms'), true)
assert.equal(isAlarmAlgorithmsKey('audioDeviceId'), false)

assert.deepEqual(
  collectAlarmAlgorithms([
    {
      id: 'channel-1',
      children: [{ algorithmId: 'algorithm-1' }, { algorithmId: 'algorithm-2' }]
    }
  ]),
  [
    { channelId: 'channel-1', algorithmId: 'algorithm-1' },
    { channelId: 'channel-1', algorithmId: 'algorithm-2' }
  ]
)

console.log('linkage form compatibility checks passed')
