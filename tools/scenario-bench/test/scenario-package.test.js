import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { ScenarioPackage } from '../src/scenario-package.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

test('loads a single-task scenario through the canonical workload schema', () => {
  const pkg = new ScenarioPackage(
    path.join(root, 'scenarios/no-helmet-99898-fps5-20260630'),
  ).load();

  assert.equal(pkg.scenario.name, 'no-helmet-99898-fps5-20260630');
  assert.equal(pkg.videoMode, 'local');
  assert.equal(pkg.videoRepeatCount, 0);
  assert.equal(pkg.tasks.length, 1);
  assert.equal(pkg.tasks[0].id, 'no-helmet');
  assert.equal(pkg.tasks[0].algorithmId, '99898');
  assert.deepEqual(pkg.bindings, [{ taskId: 'no-helmet', channels: 'all' }]);
  assert.equal(pkg.thresholds.pass.avgDiscardRate, 0.05);
  assert.match(pkg.videos.local[0].file, /LX0000000007\.mp4$/);

  const repeatParam = pkg.taskConfig.params.find((param) => param.key === 'param.videoRepeatCount');
  assert.equal(repeatParam?.value, '0');
});

test('loads a multi-task scenario without a package version flag', () => {
  const pkg = new ScenarioPackage(path.join(root, 'scenarios/multi-task-example')).load();

  assert.equal(pkg.scenario.version, undefined);
  assert.equal(pkg.tasks.length, 2);
  assert.deepEqual(
    pkg.tasks.map((task) => task.id),
    ['no-helmet', 'play-phone'],
  );
  assert.deepEqual(pkg.bindings, [
    { taskId: 'no-helmet', channels: 'all' },
    { taskId: 'play-phone', channels: 'all' },
  ]);
  assert.equal(pkg.thresholds.pass.avgDiscardRate, 0.05);
  assert.match(pkg.videos.local[0].file, /LX0000000007\.mp4$/);

  const readFpsParam = pkg.taskConfig.params.find((param) => param.key === 'param.videoReadFps');
  assert.equal(readFpsParam, undefined);
});

test('injects local video read FPS for direct VLM tasks', (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-bench-vlm-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));

  fs.writeFileSync(path.join(dir, 'scenario.yml'), `name: vlm-smoke
sampleIntervalSec: 5
channels:
  mode: local
  repeatCount: 0
  sources:
    - name: vlm
      file: /device/vlm.mp4
tasks:
  - id: vlm
    displayName: VLM
    algorithmId: "55009"
    scheduleId: schedule
    template: algorithm-template.json
    targetFps: 0.1
loadProfile:
  - channels: 1
    holdSec: 120
`, 'utf8');
  fs.writeFileSync(path.join(dir, 'algorithm-template.json'), JSON.stringify({
    algorithmId: '55009',
    algorithmCode: '55009',
    algorithmName: 'VLM',
    taskConfig: { params: [], areas: [] },
    algorithmProcessdata: JSON.stringify([{
      actionId: 'DA_00003',
      name: 'Qwen3VLWorker',
      configObject: { params: [{ key: 'fps', value: '0.1' }] },
    }]),
  }), 'utf8');

  const pkg = new ScenarioPackage(dir).load();
  const params = pkg.taskConfig.params;
  assert.equal(params.find((param) => param.key === 'param.videoRepeatCount')?.value, '0');
  assert.equal(params.find((param) => param.key === 'param.videoReadFps')?.value, '0.1');
  assert.equal(pkg.loadProfile[0].holdSec, 120);
});

test('defaults omitted VLM hold seconds to 60', (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-bench-vlm-default-hold-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));

  fs.writeFileSync(path.join(dir, 'scenario.yml'), `name: vlm-default-hold
sampleIntervalSec: 5
channels:
  mode: local
  repeatCount: 0
  sources:
    - name: vlm
      file: /device/vlm.mp4
tasks:
  - id: vlm
    displayName: VLM
    algorithmId: "55009"
    scheduleId: schedule
    template: algorithm-template.json
    targetFps: 0.1
loadProfile:
  - channels: 1
  - channels: 2
`, 'utf8');
  writeTemplate(path.join(dir, 'algorithm-template.json'), {
    actionId: 'DA_00003',
    name: 'Qwen3VLWorker',
    configObject: { params: [{ key: 'fps', value: '0.1' }] },
  });

  const pkg = new ScenarioPackage(dir).load();
  assert.deepEqual(pkg.loadProfile.map((step) => step.holdSec), [60, 60]);
});

test('defaults omitted CV hold seconds to 30', (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-bench-cv-default-hold-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));

  fs.writeFileSync(path.join(dir, 'scenario.yml'), `name: cv-default-hold
sampleIntervalSec: 5
channels:
  mode: local
  repeatCount: 0
  sources:
    - name: cv
      file: /device/cv.mp4
tasks:
  - id: cv
    displayName: CV
    type: cv
    algorithmId: "99898"
    scheduleId: schedule
    template: algorithm-template.json
loadProfile:
  - channels: 1
  - channels: 4
`, 'utf8');
  writeTemplate(path.join(dir, 'algorithm-template.json'), {
    actionId: 'AA_00001',
    name: 'AIDetector',
    configObject: { params: [{ key: 'fps', value: '5' }] },
  });

  const pkg = new ScenarioPackage(dir).load();
  assert.deepEqual(pkg.loadProfile.map((step) => step.holdSec), [30, 30]);
});

function writeTemplate(file, processNode) {
  fs.writeFileSync(file, JSON.stringify({
    algorithmId: '55009',
    algorithmCode: '55009',
    algorithmName: 'Template',
    taskConfig: { params: [], areas: [] },
    algorithmProcessdata: JSON.stringify([processNode]),
  }), 'utf8');
}
