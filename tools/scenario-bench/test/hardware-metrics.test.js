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
    queryDeviceMemoryPool: async () => ({
      totalMalloc: 1024,
      totalInUsing: 256,
      status: [{ poolSize: 128, mallocCnt: 2, freeCnt: 6 }],
    }),
  });

  const sample = await sampler.sample([]);

  assert.equal(sample.hardware.npuUtilization.usedPercent, 42);
  assert.equal(sample.hardware.specialMemoryUtilization.usedPercent, 37);
  assert.equal(sample.hardware.customScore, 91.5);
  assert.equal(sample.hardware.accelerator.activePreviewStreams, 1);
  assert.equal(sample.hardware.accelerator.activePreviewPublishers, 1);
  assert.equal(sample.hardware.accelerator.osdFrames, 12);
  assert.equal(sample.hardware.memoryPool.totalAllocatedBytes, 1024);
  assert.equal(sample.hardware.memoryPool.totalInUseBytes, 256);
  assert.equal(sample.hardware.memoryPool.utilizationPercent, 25);
  assert.deepEqual(sample.hardware.memoryPool.pools, [
    { blockSize: 128, usedBlocks: 2, freeBlocks: 6 },
  ]);
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
      memoryPool: {
        totalAllocatedBytes: index * 1024,
        totalInUseBytes: index * 256,
        utilizationPercent: index * 5,
      },
      accelerator: {
        osdFrames: index * 10,
        osdMs: index * 40,
        publishedFrames: index * 10,
        publishMs: index * 50,
        firstFrames: index,
        firstFrameMs: index * 100,
        firstFrameMaxMs: index * 90,
        colorConvertFrames: index * 10,
        colorConvertMs: index * 20,
        blobConvertFrames: index * 10,
        blobConvertMs: index * 30,
        graphForwardFrames: index * 10,
        graphForwardMs: index * 80,
        graphForwardFailures: index,
        resultParseFrames: index * 10,
        resultParseMs: index * 10,
        resultParseFailures: 0,
        rknnPrepareCalls: index * 10,
        rknnPrepareMs: index * 10,
        rknnInputsSetCalls: index * 10,
        rknnInputsSetMs: index * 20,
        rknnRunCalls: index * 10,
        rknnRunMs: index * 300,
        rknnOutputsGetCalls: index * 10,
        rknnOutputsGetMs: index * 40,
        rknnOutputTransformCalls: index * 10,
        rknnOutputTransformMs: index * 50,
        rknnForwards: index * 10,
        rknnForwardMs: index * 420,
        rknnForwardFailures: 0,
        rgaFrames: index * 10,
        rgaMs: index * 15,
        rgaFailures: 0,
        mppEncodedFrames: index * 10,
        mppEncodeMs: index * 25,
        mppEncodeFailures: 0,
        mppDecodedFrames: index * 10,
        mppDecodeMs: index * 18,
        mppDecodeFailures: 0,
        mppDecodeFallbacks: 0,
        mppCopyOutFrames: index * 6,
        mppCopyOutMs: index * 24,
        mppCopyOutFailures: 0,
        mppEarlyDroppedFrames: index * 4,
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
  assert.equal(summary.mediaStages.colorConvertAvgMs, 2);
  assert.equal(summary.mediaStages.blobConvertAvgMs, 3);
  assert.equal(summary.mediaStages.graphForwardAvgMs, 8);
  assert.equal(summary.mediaStages.resultParseAvgMs, 1);
  assert.equal(summary.mediaStages.graphForwardFailures, 1);
  assert.equal(summary.mediaStages.resultParseFailures, 0);
  assert.equal(summary.mediaStages.rknnPrepareAvgMs, 1);
  assert.equal(summary.mediaStages.rknnInputsSetAvgMs, 2);
  assert.equal(summary.mediaStages.rknnRunAvgMs, 30);
  assert.equal(summary.mediaStages.rknnOutputsGetAvgMs, 4);
  assert.equal(summary.mediaStages.rknnOutputTransformAvgMs, 5);
  assert.equal(summary.mediaStages.rknnForwardAvgMs, 42);
  assert.equal(summary.mediaStages.rknnForwardFailures, 0);
  assert.equal(summary.mediaStages.rgaAvgMs, 1.5);
  assert.equal(summary.mediaStages.rgaFailures, 0);
  assert.equal(summary.mediaStages.mppEncodeAvgMs, 2.5);
  assert.equal(summary.mediaStages.mppEncodeFailures, 0);
  assert.equal(summary.mediaStages.mppDecodeAvgMs, 1.8);
  assert.equal(summary.mediaStages.mppDecodedFrames, 10);
  assert.equal(summary.mediaStages.mppDecodeFailures, 0);
  assert.equal(summary.mediaStages.mppDecodeFallbacks, 0);
  assert.equal(summary.mediaStages.mppCopyOutAvgMs, 4);
  assert.equal(summary.mediaStages.mppCopyOutFrames, 6);
  assert.equal(summary.mediaStages.mppCopyOutFailures, 0);
  assert.equal(summary.mediaStages.mppEarlyDroppedFrames, 4);
  assert.equal(summary.mediaStages.osdAvgMs, 4);
  assert.equal(summary.mediaStages.publishAvgMs, 5);
  assert.equal(summary.mediaStages.firstFrameAvgMs, 100);
  assert.equal(summary.mediaStages.firstFrameMaxMs, 270);
  assert.equal(summary.maxPoolAllocatedBytes, 3072);
  assert.equal(summary.maxPoolInUseBytes, 768);
  assert.equal(summary.maxPoolUtilizationPercent, 15);
  assert.equal(summary.mediaStages.activePreviewStreamsPeak, 1);
  assert.equal(summary.mediaStages.activePreviewPublishersPeak, 1);
  assert.equal(summary.mediaStages.activeAlgorithmPreviewStreamsPeak, 1);
  assert.equal(summary.mediaStages.srsClientsPeak, 2);
  assert.equal(summary.mediaStages.previewStartsDelta, 1);
  assert.equal(summary.mediaStages.previewStopsDelta, 1);
  assert.equal(summary.mediaStages.previewFailuresDelta, 0);
});
