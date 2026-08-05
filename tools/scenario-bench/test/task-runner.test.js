import assert from 'node:assert/strict';
import test from 'node:test';

import { TaskRunner } from '../src/task-runner.js';

test('task runner aborts when a hold sample cannot be captured', async () => {
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch() {
      return { failedList: [] };
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
  });
  runner.setChannels(['channel-1']);

  await assert.rejects(
    runner.runStaircase(
      [{ channels: 1, holdSec: 0.001 }],
      { onSample: async () => { throw new Error('preview keepalive failed'); } },
      0.001,
    ),
    /sample tick failed: preview keepalive failed/,
  );
});

test('task runner stops and disables tasks when a hold fuse trips', async () => {
  const switches = [];
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch(tasks) {
      switches.push(tasks);
      return { failedList: [] };
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
  });
  runner.setChannels(['channel-1']);

  const result = await runner.runStaircase(
    [{ channels: 1, holdSec: 0.002 }],
    { onSample: async () => ({ stop: true, reason: 'disk 90% >= 90%' }) },
    0.001,
  );

  assert.equal(result.bottleneckPhase, 'hold');
  assert.equal(result.bottleneckReason, 'disk 90% >= 90%');
  assert.equal(switches.length, 1);
  assert.equal(switches[0][0].enable, 0);
});
