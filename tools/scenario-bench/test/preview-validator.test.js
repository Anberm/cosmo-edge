import assert from 'node:assert/strict';
import test from 'node:test';

import { _previewValidatorTest } from '../src/preview-validator.js';

test('preview validator counts saturated overlay-like pixels', () => {
  const rgb = Buffer.from([
    255, 0, 0,
    128, 128, 128,
    0, 240, 10,
  ]);
  assert.equal(_previewValidatorTest.countOverlayLikePixels(rgb), 2);
});

test('preview validator analyzes bounded raw RGB frames and progress timestamps', () => {
  const first = Buffer.from([255, 0, 0, 10, 10, 10]);
  const second = Buffer.from([0, 255, 0, 0, 0, 255]);
  const result = _previewValidatorTest.analyzeFrames(
    Buffer.concat([first, second]),
    'frame=2\nout_time_us=2000000\nprogress=end\n',
    { sampleWidth: 2, sampleHeight: 1, key: 'test' },
  );

  assert.equal(result.frameCount, 2);
  assert.equal(result.maxOverlayPixels, 2);
  assert.deepEqual(result.bestFrame, second);
  assert.equal(result.outTimeUs, 2_000_000);
});

test('preview validator accepts the legacy out_time_ms field as microseconds', () => {
  const result = _previewValidatorTest.analyzeFrames(
    Buffer.from([1, 2, 3]),
    'frame=1\nout_time_ms=1500000\nprogress=continue\n',
    { sampleWidth: 1, sampleHeight: 1, key: 'legacy-progress' },
  );

  assert.equal(result.outTimeUs, 1_500_000);
});

test('preview validator derives stream names from HTTP-FLV URLs', () => {
  assert.equal(_previewValidatorTest.streamNameFromFlvUrl('/live/LX1_7463.flv'), 'LX1_7463');
});

test('preview capture terminates by sampled frame count instead of live-stream time base', () => {
  const args = _previewValidatorTest.captureArgs({
    url: 'http://device/live/LX1.flv',
    durationSec: 6,
    sampleFps: 5,
    sampleWidth: 320,
    sampleHeight: 180,
  });

  assert.deepEqual(args.slice(args.indexOf('-frames:v'), args.indexOf('-frames:v') + 2), [
    '-frames:v',
    '30',
  ]);
  assert.equal(args.includes('-t'), false);
});

test('preview capture has a bounded wall-clock timeout', () => {
  assert.equal(_previewValidatorTest.captureTimeoutMs(1), 15_000);
  assert.equal(_previewValidatorTest.captureTimeoutMs(8), 34_000);
});

test('preview lifecycle waits for lagging cumulative metrics', () => {
  const before = { previewStreamStarts: 10, previewStreamStops: 10 };
  const released = {
    activePreviewStreams: 0,
    activePreviewPublishers: 0,
  };

  assert.equal(_previewValidatorTest.lifecycleMetricsReady({
    ...released,
    previewStreamStarts: 997,
    previewStreamStops: 997,
  }, before, 1000), false);
  assert.equal(_previewValidatorTest.lifecycleMetricsReady({
    ...released,
    previewStreamStarts: 1010,
    previewStreamStops: 1010,
  }, before, 1000), true);
});
