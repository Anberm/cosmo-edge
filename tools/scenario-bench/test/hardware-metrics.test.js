import assert from 'node:assert/strict';
import test from 'node:test';

import { MetricsSampler } from '../src/metrics-sampler.js';
import { summarizeStep } from '../src/step-evaluator.js';

test('hardware sampler preserves platform-neutral accelerator metrics', async () => {
  const sampler = new MetricsSampler({
    queryHardwareResource: async () => ({
      customScore: 91.5,
      itemList: [
        { key: 'npuUtilization', usedPercent: 42, usedSize: '42%', unusedSize: '58%' },
        { key: 'specialMemoryUtilization', usedPercent: 37, usedSize: '6.00 GB', unusedSize: '10.00 GB' },
      ],
      accelerator: {
        activePreviewStreams: 1,
        activePreviewPublishers: 1,
        osdFrames: 12,
      },
    }),
  });

  const sample = await sampler.sample([]);

  assert.equal(sample.hardware.npuUtilization.usedPercent, 42);
  assert.equal(sample.hardware.specialMemoryUtilization.usedPercent, 37);
  assert.equal(sample.hardware.customScore, 91.5);
  assert.equal(sample.hardware.accelerator.activePreviewStreams, 1);
  assert.equal(sample.hardware.accelerator.activePreviewPublishers, 1);
  assert.equal(sample.hardware.accelerator.osdFrames, 12);
});

test('step summary derives platform-neutral preview timings and lifecycle deltas', () => {
  const samples = [0, 1, 2, 3].map((index) => ({
    stepIndex: 0,
    phase: 'hold',
    activeChannels: 1,
    ts: index * 3_000,
    channels: [{
      taskKey: 'helmet',
      taskDisplayName: 'helmet',
      taskType: 'cv',
      algorithmId: '7463',
      channelId: 'LX1',
      measuredFps: 10,
      pipelineMinFps: 10,
      discardRate: 0,
      nodeDurationInfos: [
        { name: 'resize preprocess', durationAvgUs: 2_000 },
        { name: 'tracker postprocess', durationAvgUs: 3_000 },
        { name: 'detector', durationAvgUs: 8_000 },
      ],
    }],
    hardware: {
      accelerator: {
        osdFrames: index * 10,
        osdMs: index * 40,
        publishedFrames: index * 10,
        publishMs: index * 50,
        firstFrames: index,
        firstFrameMs: index * 100,
        firstFrameMaxMs: index * 90,
        activePreviewStreams: 1,
        activePreviewPublishers: 1,
        activeRawPreviewStreams: 0,
        activeAlgorithmPreviewStreams: 1,
        previewStreamStarts: index,
        previewStreamStops: index,
        previewStreamFailures: 0,
      },
    },
    preview: { srsStreams: 1, srsClients: 2 },
  }));

  const summary = summarizeStep({ index: 0, channels: 1, holdSec: 12 }, samples);
  assert.equal(summary.mediaStages.preprocessAvgMs, 2);
  assert.equal(summary.mediaStages.postprocessAvgMs, 3);
  assert.equal(summary.mediaStages.osdAvgMs, 4);
  assert.equal(summary.mediaStages.publishAvgMs, 5);
  assert.equal(summary.mediaStages.firstFrameAvgMs, 100);
  assert.equal(summary.mediaStages.firstFrameMaxMs, 270);
  assert.equal(summary.mediaStages.activePreviewStreamsPeak, 1);
  assert.equal(summary.mediaStages.activePreviewPublishersPeak, 1);
  assert.equal(summary.mediaStages.activeAlgorithmPreviewStreamsPeak, 1);
  assert.equal(summary.mediaStages.srsClientsPeak, 2);
  assert.equal(summary.mediaStages.previewStartsDelta, 1);
  assert.equal(summary.mediaStages.previewStopsDelta, 1);
  assert.equal(summary.mediaStages.previewFailuresDelta, 0);
});
