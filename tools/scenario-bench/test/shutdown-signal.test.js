import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import test from 'node:test';

import {
  ShutdownSignalError,
  installShutdownSignalHandlers,
  sleepWithSignal,
} from '../src/shutdown-signal.js';

test('SIGTERM aborts once with the conventional exit code and handlers are disposable', () => {
  const processRef = new EventEmitter();
  const controller = new AbortController();
  const received = [];
  const dispose = installShutdownSignalHandlers(controller, {
    processRef,
    onSignal: (error) => received.push(error),
  });

  processRef.emit('SIGTERM');
  processRef.emit('SIGINT');

  assert.equal(received.length, 1);
  assert.equal(received[0].signalName, 'SIGTERM');
  assert.equal(received[0].exitCode, 143);
  assert.equal(controller.signal.reason, received[0]);

  dispose();
  assert.equal(processRef.listenerCount('SIGTERM'), 0);
  assert.equal(processRef.listenerCount('SIGINT'), 0);
});

test('signal-aware sleep rejects immediately when the run is aborted', async () => {
  const controller = new AbortController();
  const sleeping = sleepWithSignal(60_000, controller.signal);

  controller.abort(new ShutdownSignalError('SIGINT'));

  await assert.rejects(sleeping, (error) => error.signalName === 'SIGINT' && error.exitCode === 130);
});
