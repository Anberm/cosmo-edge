import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  auditLongRun,
  auditLongRunFile,
  writeLongRunAudit,
} from '../src/longrun-auditor.js';

const START = Date.parse('2026-08-04T00:00:00.000Z');

function accelerator(index) {
  const frames = 1000 + index * 100;
  return {
    blobConvertFrames: frames,
    colorConvertFrames: frames,
    graphForwardFrames: frames,
    graphForwardFailures: 0,
    mppCopyOutFailures: 0,
    mppCopyOutFrames: frames * 2,
    mppDecodeFailures: 0,
    mppDecodeFallbacks: 0,
    mppDecodedFrames: frames * 4,
    mppEarlyDroppedFrames: frames * 2,
    mppEncodeFailures: 0,
    mppEncodedFrames: frames * 3,
    osdFrames: frames * 3,
    previewStreamFailures: 0,
    publishedFrames: frames * 3,
    resultParseFailures: 0,
    resultParseFrames: frames,
    rgaFailures: 0,
    rgaFrames: frames,
    rknnForwardFailures: 0,
    rknnRgaFailures: 0,
    rknnRgaBoundInputFrames: frames * 3,
    rknnRgaBoundInputImportFailures: 0,
    rknnRgaBoundRequantizeFailures: 0,
    rknnForwards: frames * 3,
    videoDecoderBackend: 'rockchip-copy-out',
    videoEncoderBackend: 'rockchip-copy-first',
  };
}

function sample(index, overrides = {}) {
  return {
    ts: START + index * 60_000,
    iso: new Date(START + index * 60_000).toISOString(),
    phase: 'hold',
    stepIndex: 0,
    activeChannels: 4,
    targetChannels: 4,
    channels: Array.from({ length: 4 }, (_, channelIndex) => ({
      taskKey: 'helmet',
      channelId: `ch${channelIndex + 1}`,
      targetFps: 5,
      measuredFps: 5.2,
      discardRate: 0,
      missing: false,
      telemetryMissing: false,
    })),
    hardware: {
      accelerator: accelerator(index),
      cpuUtilization: { usedPercent: 50 },
      generalMemoryUtilization: { usedPercent: 25 },
      eMMCUtilization: { usedPercent: 70 },
      memoryPool: {
        totalAllocatedBytes: 400 * 1024 * 1024 + index * 1024,
        totalInUseBytes: 30 * 1024 * 1024,
        utilizationPercent: 7.5,
      },
    },
    preview: {
      mode: 'algorithm',
      requestedStreams: 4,
      srsStreams: 4,
      srsPublishingStreams: 4,
      srsClients: 4,
      mediaClients: 0,
      errors: [],
    },
    ...overrides,
  };
}

function runResult(count = 5) {
  return {
    scenarioName: 'RK3576 4ch 5fps 12h',
    status: 'running',
    startedAt: new Date(START).toISOString(),
    endedAt: new Date(START + count * 60_000).toISOString(),
    previewProfile: { mode: 'algorithm' },
    thresholds: { pass: { avgDiscardRate: 0.05, maxDiskUsedPercent: 90 } },
    steps: [{ index: 0, channels: 4, holdSec: 259200 }],
    samples: Array.from({ length: count }, (_, index) => sample(index + 1)),
  };
}

const options = {
  gateHours: 4 / 60,
  nowMs: START + 5 * 60_000 + 10_000,
  maxGapSec: 120,
  maxFreshnessSec: 120,
  minFpsRatio: 0.9,
  expectedPreviewStreams: 4,
  expectedDecoderBackend: 'rockchip-copy-out',
  expectedEncoderBackend: 'rockchip-copy-first',
};

test('long-run audit passes only after duration and all native gates pass', () => {
  const result = auditLongRun(runResult(), options);
  assert.equal(result.verdict, 'PASS');
  assert.equal(result.gate.reached, true);
  assert.deepEqual(result.failures, []);
  assert.equal(result.nativeMedia.copyAccountingError, 0);
  assert.equal(result.workload.bindings.length, 4);
});

test('long-run audit reports IN_PROGRESS before the wall-clock gate', () => {
  const result = auditLongRun(runResult(), { ...options, gateHours: 1 });
  assert.equal(result.verdict, 'IN_PROGRESS');
  assert.deepEqual(result.pending, ['gate.duration']);
  assert.deepEqual(result.failures, []);
});

test('completed long-run uses completedAt with a bounded final sampling delay', () => {
  const input = runResult();
  input.status = 'completed';
  input.endedAt = new Date(START + 5.5 * 60_000).toISOString();

  const result = auditLongRun(input, { ...options, gateHours: 5.5 / 60 });
  assert.equal(result.verdict, 'PASS');
  assert.equal(result.gate.finalSampleDelaySec, 30);
  assert.equal(result.checks.find((item) => item.id === 'samples.finalDelay')?.status, 'PASS');
});

test('long-run audit fails sampling gaps, FPS regression, and counter reset', () => {
  const input = runResult();
  input.samples[2].ts += 180_000;
  input.samples[2].iso = new Date(input.samples[2].ts).toISOString();
  input.samples[3].channels[0].measuredFps = 4;
  input.samples[3].hardware.accelerator.rknnForwards = 1;

  const result = auditLongRun(input, options);
  assert.equal(result.verdict, 'FAIL');
  assert.ok(result.failures.includes('samples.timestamps'));
  assert.ok(result.failures.includes('samples.maxGap'));
  assert.ok(result.failures.includes('workload.fps'));
  assert.ok(result.failures.includes('native.counterContinuity'));
});

test('long-run audit fails native failure increments and unhealthy preview', () => {
  const input = runResult();
  input.samples.at(-1).hardware.accelerator.mppEncodeFailures = 1;
  input.samples.at(-1).preview.srsPublishingStreams = 3;

  const result = auditLongRun(input, options);
  assert.equal(result.verdict, 'FAIL');
  assert.ok(result.failures.includes('native.failures'));
  assert.ok(result.failures.includes('preview.health'));
});

test('long-run audit fails RGA bound-input import and requantize errors', () => {
  const input = runResult();
  input.samples.at(-1).hardware.accelerator.rknnRgaBoundInputImportFailures = 1;
  input.samples.at(-1).hardware.accelerator.rknnRgaBoundRequantizeFailures = 1;

  const result = auditLongRun(input, options);
  assert.equal(result.verdict, 'FAIL');
  assert.ok(result.failures.includes('native.failures'));
  const nativeFailures = result.checks.find((item) => item.id === 'native.failures');
  assert.equal(nativeFailures.actual.rknnRgaBoundInputImportFailures, 1);
  assert.equal(nativeFailures.actual.rknnRgaBoundRequantizeFailures, 1);
});

test('file audit binds source and allowlisted candidate identity by SHA-256', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-longrun-audit-'));
  try {
    const input = path.join(dir, 'metrics.partial.json');
    const identity = path.join(dir, 'identity.txt');
    const output = path.join(dir, 'checkpoint.json');
    fs.writeFileSync(input, JSON.stringify(runResult()), 'utf8');
    fs.writeFileSync(identity, [
      'engineSourceCommit=88e556a1',
      'engineSha256=abc123',
      'password=must-not-be-copied',
    ].join('\n'), 'utf8');

    const result = auditLongRunFile(input, { ...options, identityPath: identity });
    const written = writeLongRunAudit(output, result);
    const saved = JSON.parse(fs.readFileSync(written, 'utf8'));

    assert.match(saved.source.sha256, /^[a-f0-9]{64}$/);
    assert.match(saved.identity.sha256, /^[a-f0-9]{64}$/);
    assert.equal(saved.identity.properties.engineSourceCommit, '88e556a1');
    assert.equal(saved.identity.properties.password, undefined);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
