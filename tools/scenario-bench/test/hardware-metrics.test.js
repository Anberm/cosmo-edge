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

test('hardware sampler omits explicitly unavailable NPU utilization', async () => {
  const sampler = new MetricsSampler({
    queryHardwareResource: async () => ({
      itemList: [
        { key: 'cpuUtilization', usedPercent: 12, available: 1 },
        { key: 'npuUtilization', usedPercent: 100, available: 0 },
      ],
    }),
  });

  const sample = await sampler.sample([]);

  assert.equal(sample.hardware.cpuUtilization.usedPercent, 12);
  assert.equal(sample.hardware.npuUtilization, undefined);
});

test('CV sampler reports per-channel pipeline FPS when a detector is shared', async () => {
  const sampler = new MetricsSampler({
    taskRunningDetail: async () => ({
      status: [{
        taskId: 'ch1_alg',
        channelId: 'ch1',
        actionStatus: [
          {
            actionId: 'AA_00001',
            name: '9275710 AiDetector',
            processCount: 2400,
            processCountPeriod: 2400,
            periodMs: 60_000,
          },
          {
            actionId: 'AA_00003',
            name: 'ch1 tracker',
            processCount: 300,
            processCountPeriod: 300,
            periodMs: 60_000,
          },
        ],
        nodeDurationInfos: [],
      }],
    }),
    queryHardwareResource: async () => ({ itemList: [] }),
  });

  const sample = await sampler.sample([{
    taskKey: 'helmet',
    taskType: 'cv',
    channelId: 'ch1',
    taskId: 'ch1_alg',
    targetFps: 5,
  }]);

  assert.equal(sample.channels[0].actionSummaries[0].fps, 40);
  assert.equal(sample.channels[0].measuredFps, 5);
  assert.equal(sample.channels[0].pipelineMinFps, 5);
  assert.equal(sample.channels[0].fpsRatio, 1);
});

test('CV sampler excludes terminal event rate from frame throughput', async () => {
  const sampler = new MetricsSampler({
    taskRunningDetail: async () => ({
      status: [{
        taskId: 'ch1_alg',
        channelId: 'ch1',
        actionStatus: [
          {
            actionId: 'AA_00001',
            name: '9275710 AiDetector',
            processCount: 2400,
            processCountPeriod: 2400,
            periodMs: 60_000,
          },
          {
            actionId: 'AA_00003',
            name: 'ch1 tracker',
            processCount: 300,
            processCountPeriod: 300,
            periodMs: 60_000,
          },
          {
            actionId: 'BA_00004',
            name: '事件上报',
            processCount: 9,
            processCountPeriod: 9,
            periodMs: 60_000,
          },
        ],
        nodeDurationInfos: [],
      }],
    }),
    queryHardwareResource: async () => ({ itemList: [] }),
  });

  const sample = await sampler.sample([{
    taskKey: 'helmet',
    taskType: 'cv',
    channelId: 'ch1',
    taskId: 'ch1_alg',
    targetFps: 5,
  }]);

  assert.equal(sample.channels[0].actionSummaries[0].fps, 40);
  assert.equal(sample.channels[0].actionSummaries[1].fps, 5);
  assert.equal(sample.channels[0].actionSummaries[2].fps, 0.15);
  assert.equal(sample.channels[0].measuredFps, 5);
  assert.equal(sample.channels[0].pipelineMinFps, 5);
  assert.equal(sample.channels[0].fpsRatio, 1);
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
        rknnOutputsReleaseCalls: index * 10,
        rknnOutputsReleaseMs: index * 5,
        rknnOutputTransformCalls: index * 10,
        rknnOutputTransformMs: index * 50,
        rknnForwards: index * 10,
        rknnForwardMs: index * 420,
        rknnForwardFailures: 0,
        rknnMutexWaitCalls: index * 10,
        rknnMutexWaitMs: index * 6,
        rknnDetectorPrepareCalls: index * 4,
        rknnDetectorPrepareMs: index * 4,
        rknnDetectorInputsSetCalls: index * 4,
        rknnDetectorInputsSetMs: index * 8,
        rknnDetectorRunCalls: index * 4,
        rknnDetectorRunMs: index * 120,
        rknnDetectorOutputsGetCalls: index * 4,
        rknnDetectorOutputsGetMs: index * 16,
        rknnDetectorOutputsReleaseCalls: index * 4,
        rknnDetectorOutputsReleaseMs: index * 2,
        rknnDetectorOutputTransformCalls: index * 4,
        rknnDetectorOutputTransformMs: index * 20,
        rknnDetectorForwards: index * 4,
        rknnDetectorForwardMs: index * 168,
        rknnDetectorForwardFailures: 0,
        rknnDetectorMutexWaitCalls: index * 4,
        rknnDetectorMutexWaitMs: index * 2,
        rknnPreprocessFastHits: index * 4,
        rknnRgaFillCalls: index * 4,
        rknnRgaFillMs: index * 2,
        rknnRgaResizeColorCalls: index * 4,
        rknnRgaResizeColorMs: index * 8,
        rknnRgaCropResizeCalls: index * 6,
        rknnRgaCropResizeMs: index * 3,
        rknnRgaCropResizeFailures: 0,
        rknnRgaCropDmaBufFrames: index * 6,
        rknnRgaCropHostFallbacks: 0,
        rknnRgaFailures: 0,
        rknnCpuResizeFallbackCalls: 0,
        rknnCpuResizeFallbackMs: 0,
        rknnCpuCropResizeFallbackCalls: 0,
        rknnCpuCropResizeFallbackMs: 0,
        rknnCpuNormalizeFallbackCalls: 0,
        rknnCpuNormalizeFallbackMs: 0,
        rknnNativeInputMapCalls: index * 4,
        rknnNativeInputMapMs: index,
        rknnNativeInt8Inputs: index * 4,
        rknnFloatInputs: index * 6,
        rknnInputCompatibilityFallbacks: 0,
        rknnBoundInputBindAttempts: index,
        rknnBoundInputBindFailures: 0,
        rknnBoundInputCopyCalls: index * 4,
        rknnBoundInputCopyMs: index * 2,
        rknnBoundInputCopyBytes: index * 4 * 1_228_800,
        rknnBoundInputCopyFailures: 0,
        rknnBoundInputSyncCalls: index * 4,
        rknnBoundInputSyncMs: index,
        rknnBoundInputSyncFailures: 0,
        rknnBoundInputFrames: index * 4,
        rknnRgaBoundInputBindAttempts: index,
        rknnRgaBoundInputBindFailures: 0,
        rknnRgaBoundInputImportCalls: index,
        rknnRgaBoundInputImportMs: index * 0.25,
        rknnRgaBoundInputImportFailures: 0,
        rknnRgaBoundInputFrames: index * 4,
        rknnRgaBoundUint8Frames: index * 4,
        rknnRgaBoundNativeInt8Frames: 0,
        rknnRgaBoundRequantizeCalls: 0,
        rknnRgaBoundRequantizeMs: 0,
        rknnRgaBoundRequantizeFailures: 0,
        rknnRgaBoundInputNormalizeBypasses: index * 4,
        rknnMppDmaBufImportCalls: index * 4,
        rknnMppDmaBufImportMs: index,
        rknnMppDmaBufImportFailures: 0,
        rknnMppDmaBufFrames: index * 4,
        rknnMppDmaBufFallbacks: 0,
        rknnMppDmaBufSourceBytes: index * 4 * 3_133_440,
        rknnNativeInt8Outputs: index * 4,
        rknnFloatOutputs: index * 6,
        rknnOutputCompatibilityFallbacks: index,
        rknnNativeOutputBytes: index * 4 * 1_225_600,
        rknnFloatOutputBytes: index * 6 * 4_902_400,
        rknnYolov8DflCalls: index * 4,
        rknnYolov8DflMs: index * 8,
        rknnYolov8ClassCalls: index * 4,
        rknnYolov8ClassMs: index * 4,
        rknnYolov8DirectCandidateCalls: index * 4,
        rknnYolov8DirectCandidateFailures: 0,
        rknnYolov8DirectPointsScanned: index * 4 * 8_400,
        rknnYolov8DirectPointsDecoded: index * 4 * 17,
        rknnYolov8ScoreSumPointsRejected: index * 4 * 8_000,
        rknnYolov8LogicalFloatBytesAvoided: index * 4 * 2_822_400,
        yolov8PostprocessCalls: index * 4,
        yolov8PostprocessMs: index * 12,
        yolov8NmsCalls: index * 4,
        yolov8NmsMs: index * 4,
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
        mppRgaCopyOutFrames: index * 6,
        mppRgaCopyOutFailures: 0,
        mppCpuCopyOutFallbacks: 0,
        mppRgaCopyInFrames: index * 10,
        mppRgaCopyInFailures: 0,
        mppCpuCopyInFallbacks: 0,
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
  assert.equal(summary.mediaStages.rknnOutputsReleaseAvgMs, 0.5);
  assert.equal(summary.mediaStages.rknnOutputTransformAvgMs, 5);
  assert.equal(summary.mediaStages.rknnForwardAvgMs, 42);
  assert.equal(summary.mediaStages.rknnForwardFailures, 0);
  assert.equal(summary.mediaStages.rknnMutexWaitAvgMs, 0.6);
  assert.equal(summary.mediaStages.rknnDetectorForwardAvgMs, 42);
  assert.equal(summary.mediaStages.rknnDetectorOutputsReleaseAvgMs, 0.5);
  assert.equal(summary.mediaStages.rknnDetectorMutexWaitAvgMs, 0.5);
  assert.equal(summary.mediaStages.rknnPreprocessFastHits, 4);
  assert.equal(summary.mediaStages.rknnRgaFillAvgMs, 0.5);
  assert.equal(summary.mediaStages.rknnRgaResizeColorAvgMs, 2);
  assert.equal(summary.mediaStages.rknnNativeInputMapAvgMs, 0.25);
  assert.equal(summary.mediaStages.rknnNativeInt8Inputs, 4);
  assert.equal(summary.mediaStages.rknnFloatInputs, 6);
  assert.equal(summary.mediaStages.rknnInputCompatibilityFallbacks, 0);
  assert.equal(summary.mediaStages.rknnBoundInputBindAttempts, 1);
  assert.equal(summary.mediaStages.rknnBoundInputBindFailures, 0);
  assert.equal(summary.mediaStages.rknnBoundInputCopyAvgMs, 0.5);
  assert.equal(summary.mediaStages.rknnBoundInputCopyAvgBytes, 1_228_800);
  assert.equal(summary.mediaStages.rknnBoundInputCopyFailures, 0);
  assert.equal(summary.mediaStages.rknnBoundInputSyncAvgMs, 0.25);
  assert.equal(summary.mediaStages.rknnBoundInputSyncFailures, 0);
  assert.equal(summary.mediaStages.rknnBoundInputFrames, 4);
  assert.equal(summary.mediaStages.rknnRgaBoundInputBindAttempts, 1);
  assert.equal(summary.mediaStages.rknnRgaBoundInputBindFailures, 0);
  assert.equal(summary.mediaStages.rknnRgaBoundInputImportCalls, 1);
  assert.equal(summary.mediaStages.rknnRgaBoundInputImportAvgMs, 0.25);
  assert.equal(summary.mediaStages.rknnRgaBoundInputImportFailures, 0);
  assert.equal(summary.mediaStages.rknnRgaBoundInputFrames, 4);
  assert.equal(summary.mediaStages.rknnRgaBoundUint8Frames, 4);
  assert.equal(summary.mediaStages.rknnRgaBoundNativeInt8Frames, 0);
  assert.equal(summary.mediaStages.rknnRgaBoundRequantizeCalls, 0);
  assert.equal(summary.mediaStages.rknnRgaBoundRequantizeAvgMs, null);
  assert.equal(summary.mediaStages.rknnRgaBoundRequantizeFailures, 0);
  assert.equal(summary.mediaStages.rknnRgaBoundInputNormalizeBypasses, 4);
  assert.equal(summary.mediaStages.rknnMppDmaBufImportCalls, 4);
  assert.equal(summary.mediaStages.rknnMppDmaBufImportAvgMs, 0.25);
  assert.equal(summary.mediaStages.rknnMppDmaBufImportFailures, 0);
  assert.equal(summary.mediaStages.rknnMppDmaBufFrames, 4);
  assert.equal(summary.mediaStages.rknnMppDmaBufFallbacks, 0);
  assert.equal(summary.mediaStages.rknnMppDmaBufSourceAvgBytes, 3_133_440);
  assert.equal(summary.mediaStages.rknnNativeInt8Outputs, 4);
  assert.equal(summary.mediaStages.rknnFloatOutputs, 6);
  assert.equal(summary.mediaStages.rknnOutputCompatibilityFallbacks, 1);
  assert.equal(summary.mediaStages.rknnNativeOutputAvgBytes, 1_225_600);
  assert.equal(summary.mediaStages.rknnFloatOutputAvgBytes, 4_902_400);
  assert.equal(summary.mediaStages.rknnYolov8DflAvgMs, 2);
  assert.equal(summary.mediaStages.rknnYolov8ClassAvgMs, 1);
  assert.equal(summary.mediaStages.rknnYolov8DirectCandidateCalls, 4);
  assert.equal(summary.mediaStages.rknnYolov8DirectCandidateFailures, 0);
  assert.equal(summary.mediaStages.rknnYolov8DirectAvgPointsScanned, 8_400);
  assert.equal(summary.mediaStages.rknnYolov8DirectAvgPointsDecoded, 17);
  assert.equal(summary.mediaStages.rknnYolov8ScoreSumAvgPointsRejected, 8_000);
  assert.equal(summary.mediaStages.rknnYolov8LogicalFloatBytesAvoided, 11_289_600);
  assert.equal(summary.mediaStages.yolov8PostprocessAvgMs, 3);
  assert.equal(summary.mediaStages.yolov8NmsAvgMs, 1);
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
