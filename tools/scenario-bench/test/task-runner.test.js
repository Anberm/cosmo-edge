import assert from 'node:assert/strict';
import test from 'node:test';

import { ShutdownSignalError } from '../src/shutdown-signal.js';
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

test('task runner interrupts a hold and disables active tasks on SIGTERM', async () => {
  const controller = new AbortController();
  const switches = [];
  let cleanupStarted = false;
  const client = {
    async taskApplyParamsBatch() {
      return { failedList: [] };
    },
    async taskBatchSwitch(tasks) {
      assert.equal(cleanupStarted, true);
      switches.push(tasks);
      return { failedList: [] };
    },
    beginCleanup() {
      cleanupStarted = true;
    },
  };
  const runner = new TaskRunner(client, {
    algorithmId: '7463',
    scheduleId: 'always',
    rampBatchDelaySec: 0,
    signal: controller.signal,
  });
  runner.setChannels(['channel-1']);

  const run = runner.runStaircase([{ channels: 1, holdSec: 60 }], {}, 60);
  await new Promise((resolve) => setImmediate(resolve));
  controller.abort(new ShutdownSignalError('SIGTERM'));

  await assert.rejects(run, (error) => error.signalName === 'SIGTERM' && error.exitCode === 143);
  assert.equal(cleanupStarted, true);
  assert.equal(switches.length, 1);
  assert.equal(switches[0][0].enable, 0);
});
