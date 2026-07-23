import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { ReportWriter } from '../src/report-writer.js';

test('partial reports are checkpointed at a bounded interval', async () => {
  const output = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-report-writer-'));
  let now = 1_000;
  try {
    const writer = new ReportWriter(output, { partialWriteIntervalMs: 30_000, now: () => now });
    const first = await writer.writePartial({ samples: [1] });
    assert.equal(first.skipped, false);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1]);

    now += 10_000;
    const skipped = await writer.writePartial({ samples: [1, 2] });
    assert.equal(skipped.skipped, true);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1]);

    now += 20_000;
    const checkpoint = await writer.writePartial({ samples: [1, 2, 3] });
    assert.equal(checkpoint.skipped, false);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1, 2, 3]);

    now += 1;
    const forced = await writer.writePartial({ samples: [1, 2, 3, 4] }, { force: true });
    assert.equal(forced.skipped, false);
    assert.deepEqual(JSON.parse(fs.readFileSync(first.jsonPath, 'utf8')).samples, [1, 2, 3, 4]);
  } finally {
    fs.rmSync(output, { recursive: true, force: true });
  }
});

test('capacity conclusion uses bottleneck step when an earlier step has a report failure', () => {
  const writer = new ReportWriter('.');
  const steps = [
    { step: { index: 0 }, channels: 1, pass: false, reasons: ['warmup window below target'] },
    { step: { index: 1 }, channels: 2, pass: true, reasons: [] },
    { step: { index: 2 }, channels: 3, pass: true, reasons: [] },
    { step: { index: 3 }, channels: 4, pass: true, reasons: [] },
    { step: { index: 4 }, channels: 5, pass: true, reasons: [] },
    { step: { index: 5 }, channels: 6, pass: true, reasons: [] },
    { step: { index: 6 }, channels: 7, pass: false, reasons: ['fpsRatio 0.661 < 0.8'] },
  ];

  const summary = writer._buildSummary({
    status: 'completed',
    profileMode: 'capacity',
    bottleneck: {
      stepIndex: 6,
      stepNumber: 7,
      channels: 7,
      reason: 'fpsRatio 0.661 < 0.8',
    },
  }, steps);

  assert.equal(summary.maxStableChannels, 6);
  assert.match(summary.conclusion, /容量上限：6 路/);
  assert.match(summary.conclusion, /第 7 阶段/);
  assert.doesNotMatch(summary.conclusion, /1 路开始/);
});

test('HTML rendering formats task percentage fields', () => {
  const writer = new ReportWriter('.');
  const html = writer._renderHtml({
    scenarioName: 'vlm',
    tasks: [{ id: 'vlm', type: 'vlm', algorithmId: '77175', targetFps: 0.1 }],
    videoMode: 'local',
    status: 'completed',
    thresholds: { pass: {} },
    samples: [{ ts: 0 }, { ts: 3000 }],
  }, [{
    step: { index: 0 },
    channels: 1,
    holdSec: 60,
    targetFps: 0.1,
    minFpsAcross: 0.1,
    criticalPathLatencyMs: 1200,
    detectorLatencyMs: 1200,
    avgDiscard: 0,
    maxDiscard: 0,
    maxNpu: 90,
    maxCpu: 3,
    maxMem: 55,
    pass: true,
    reasons: [],
    perThreshold: [],
    taskStats: [{
      taskKey: 'vlm',
      taskDisplayName: 'vlm',
      strategy: 'vlm',
      algorithmId: '77175',
      bindingCount: 1,
      minThroughputFps: 0.1,
      minFpsRatio: 1,
      maxMissingRate: 0,
      avgDiscardRate: 0,
      maxPrimaryLatencyMs: 1200,
      maxCriticalPathLatencyMs: 1200,
    }],
  }], {
    overallPass: true,
    hasBottleneck: false,
    maxStableChannelsExact: true,
    conclusion: 'ok',
  });

  assert.match(html, /100%/);
  assert.match(html, /0%/);
});

test('HTML rendering estimates sampling interval from stable in-step samples', () => {
  const writer = new ReportWriter('.');
  const html = writer._renderHtml({
    scenarioName: 'vlm',
    tasks: [{ id: 'vlm', type: 'vlm', algorithmId: '77175', targetFps: 0.1 }],
    videoMode: 'local',
    status: 'completed',
    thresholds: { pass: {} },
    samples: [
      { stepIndex: 0, ts: 0 },
      { stepIndex: 0, ts: 33000 },
      { stepIndex: 0, ts: 36000 },
      { stepIndex: 0, ts: 39000 },
      { stepIndex: 1, ts: 70000 },
      { stepIndex: 1, ts: 73000 },
    ],
  }, [], {
    overallPass: true,
    hasBottleneck: false,
    maxStableChannelsExact: true,
    conclusion: 'ok',
  });

  assert.match(html, /约每 3s 采样一次/);
});

test('HTML rendering splits ramp-only bottleneck samples by observed channels', () => {
  const writer = new ReportWriter('.');
  const samples = Array.from({ length: 7 }, (_, index) => {
    const activeChannels = index + 1;
    return {
      stepIndex: 0,
      phase: 'ramp',
      targetChannels: 16,
      activeChannels,
      ts: index * 3000,
      channels: Array.from({ length: activeChannels }, (_, channelIndex) => ({
        taskKey: 'helmet-7463',
        taskDisplayName: '安全帽检测 7463',
        taskType: 'cv',
        algorithmId: '7463',
        channelId: `ch-${channelIndex + 1}`,
        measuredFps: 5,
        pipelineMinFps: 5,
        discardRate: 0,
        nodeDurationInfos: [
          { name: 'detector', durationAvgUs: 20_000 },
        ],
      })),
      hardware: {
        npuUtilization: { usedPercent: activeChannels === 7 ? 94 : 50 },
        cpuUtilization: { usedPercent: 20 },
        generalMemoryUtilization: { usedPercent: 40 },
      },
    };
  });
  const runResult = {
    scenarioName: 'helmet',
    profileMode: 'configured',
    videoMode: 'local',
    status: 'completed',
    tasks: [{ id: 'helmet-7463', type: 'cv', algorithmId: '7463', targetFps: 5 }],
    thresholds: { pass: { avgDiscardRate: 0.05 } },
    steps: [{ index: 0, channels: 16, holdSec: 30 }],
    bottleneck: {
      stepIndex: 0,
      stepNumber: 1,
      channels: 16,
      reason: 'NPU >= 90%',
    },
    samples,
  };

  const stepSummaries = writer._summarizeSteps(runResult);
  assert.deepEqual(stepSummaries.map((s) => s.channels), [1, 2, 3, 4, 5, 6, 7]);

  const summary = writer._buildSummary(runResult, stepSummaries);
  assert.equal(summary.bottleneck.channels, 7);
  assert.equal(summary.bottleneck.targetChannels, 16);

  const html = writer._renderHtml(runResult, stepSummaries, summary);
  const routeTable = html.match(/<h2>路数结果<\/h2>[\s\S]*?<h2>媒体与预览分阶段指标<\/h2>/)[0];
  assert.equal((routeTable.match(/<tr>/g) ?? []).length - 1, 7);
  assert.match(routeTable, /<td>7<\/td>[\s\S]*<td class="warn">STOPPED<\/td>/);
  assert.doesNotMatch(routeTable, /<td>16<\/td>/);
});

test('HTML rendering expands single target step from observed channel samples', () => {
  const writer = new ReportWriter('.');
  const samples = [
    ...Array.from({ length: 16 }, (_, index) => sampleForChannels(index + 1, index * 3000, 'ramp')),
    ...Array.from({ length: 10 }, (_, index) => sampleForChannels(16, 48_000 + index * 3000, 'hold')),
  ];
  const runResult = {
    scenarioName: 'helmet',
    profileMode: 'configured',
    videoMode: 'local',
    status: 'completed',
    tasks: [{ id: 'helmet-99898', type: 'cv', algorithmId: '99898', targetFps: 5 }],
    thresholds: { pass: { avgDiscardRate: 0.05 } },
    steps: [{ index: 0, channels: 16, holdSec: 30 }],
    baselineFps: 5.45,
    samples,
  };

  const stepSummaries = writer._summarizeSteps(runResult);
  assert.deepEqual(stepSummaries.map((s) => s.channels), Array.from({ length: 16 }, (_, i) => i + 1));
  assert.equal(stepSummaries[15].taskStats[0].bindingCount, 16);

  const summary = writer._buildSummary(runResult, stepSummaries);
  assert.equal(summary.baselineFps, 5);
  assert.equal(summary.maxStableChannels, 16);

  const html = writer._renderHtml(runResult, stepSummaries, summary);
  assert.match(html, /<td>16<\/td>/);
  assert.doesNotMatch(html, /samplePhase/);
});

function sampleForChannels(activeChannels, ts, phase) {
  return {
    stepIndex: 0,
    phase,
    targetChannels: 16,
    activeChannels,
    ts,
    channels: Array.from({ length: activeChannels }, (_, channelIndex) => ({
      taskKey: 'helmet-99898',
      taskDisplayName: 'helmet 99898',
      taskType: 'cv',
      algorithmId: '99898',
      channelId: `ch-${channelIndex + 1}`,
      measuredFps: 5,
      pipelineMinFps: 5,
      discardRate: 0,
      nodeDurationInfos: [
        { name: 'detector', durationAvgUs: 20_000 },
      ],
    })),
    hardware: {
      npuUtilization: { usedPercent: 50 },
      cpuUtilization: { usedPercent: 20 },
      generalMemoryUtilization: { usedPercent: 40 },
    },
  };
}
