#!/usr/bin/env node
// scenario-bench entry point.

import fs from 'node:fs';
import path from 'node:path';
import { CosmoClient } from './cosmo-client.js';
import {
  DEFAULT_HOLD_SEC,
  DEFAULT_VLM_HOLD_SEC,
  ScenarioPackage,
  detectVlmMode,
} from './scenario-package.js';
import { ChannelManager } from './channel-manager.js';
import { TaskRunner } from './task-runner.js';
import { MetricsSampler } from './metrics-sampler.js';
import { ReportWriter } from './report-writer.js';
import { Logger } from './logger.js';
import { summarizeStep, runtimeStepDecision } from './step-evaluator.js';
import { strategyForTaskType } from './task-strategies.js';
import { PreviewLoad } from './preview-load.js';
import { runPreviewValidation } from './preview-validator.js';

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--help' || a === '-h' || a === 'help') {
      args.command = 'help';
    } else if (a.startsWith('--')) {
      const key = a.slice(2);
      if (['verbose', 'no-reuse', 'cleanup', 'skip-import'].includes(key)) {
        args[key] = true;
      } else {
        args[key] = argv[++i];
      }
    } else if (!args.command && ['run', 'doctor', 'preview', 'init-scenario'].includes(a)) {
      args.command = a;
    }
  }
  return args;
}

function printHelp() {
  console.log(`scenario-bench - CosmoEdge scenario load benchmark

Usage:
  scenario-bench run --device <url> (--user <u> --password <p> | --token-env <name>) --scenario <dir> --output <dir> [options]
  scenario-bench preview --device <url> (--user <u> --password <p> | --token-env <name>) --channel <id> --output <dir> [options]
  scenario-bench doctor --scenario <dir> [--device <url> --user <u> --password <p>] [--output <dir>]
  scenario-bench init-scenario --name <name> --template <algorithm-template.json> --video <file> [options]

run required:
  --device <url>       Device base URL, e.g. http://192.168.1.10:8080
  --user <account>     Login account (use together with --password)
  --password <plain>   Login password
  --token-env <name>   Read an existing device token from this environment variable
  --scenario <dir>     Scenario package directory
  --output <dir>       Report output directory

scenario package:
  scenario.yml          channels + tasks + loadProfile + optional bindings/thresholds

run options:
  --cleanup            Delete created channels after the run
  --skip-import        Skip algorithm/layout/save when the template already exists
  --no-reuse           Always create new bench channels
  --profile <mode>     capacity (default) expands to every channel count; configured keeps scenario.yml steps
  --ramp-batch-size <n>
  --ramp-batch-delay-sec <n>
  --preview <mode>      none (default) | raw | algorithm
  --preview-streams <n> Number of preview streams or all (default)
  --preview-clients <n> Media clients per preview stream, default 1
  --media-base <url>    HTTP-FLV base, e.g. http://device:18088
  --srs-api <url>       Optional SRS API base, e.g. http://device:1985
  --ffmpeg <path>       ffmpeg executable, default ffmpeg
  --lang <code>        Accept-Language, default zh-CN
  --verbose            Print per-sample debug logs

preview options:
  --channel <id>       Existing, running deterministic test channel
  --algorithm <id>     Running algorithm code; omit for raw-only validation
  --mode <mode>        raw | algorithm | both (default when --algorithm is set)
  --media-base <url>   HTTP-FLV base, e.g. http://device:18088
  --srs-api <url>      SRS API base, e.g. http://device:1985
  --duration-sec <n>   Decode duration per client, default 8
  --clients <n>        Concurrent clients on one stream, default 4
  --lifecycle-iterations <n> Repeated same-stream open/close count, default 0
  --min-overlay-pixel-delta <n> Algorithm-vs-raw pixel gate, default 0
  --ffmpeg <path>      ffmpeg executable, default ffmpeg
  --ffprobe <path>     ffprobe executable, default ffprobe
  --token-env <name>   Read an existing device token from this environment variable

init-scenario options:
  --output <dir>       New scenario directory, default scenarios/<name>
  --display-name <s>   Display name in the report
  --algorithm-id <id>  Defaults to algorithmCode/algorithmId from the template
  --schedule-id <id>   Defaults to default-schedule
  --target-fps <n>     Writes tasks[].targetFps when the template cannot expose it
`);
}

function requireArgs(args, names) {
  const missing = names.filter((name) => !args[name]);
  if (missing.length) {
    console.error(`Missing required argument(s): ${missing.map((name) => `--${name}`).join(', ')}`);
    printHelp();
    process.exit(2);
  }
}

function checkLine(ok, label, detail = '') {
  console.log(`[${ok ? 'OK ' : 'ERR'}] ${label}${detail ? ` - ${detail}` : ''}`);
}

function ensureWritableDir(dir) {
  const abs = path.resolve(dir);
  fs.mkdirSync(abs, { recursive: true });
  const probe = path.join(abs, `.scenario-bench-write-test-${process.pid}`);
  fs.writeFileSync(probe, 'ok', 'utf8');
  fs.unlinkSync(probe);
  return abs;
}

function deviceAuth(args) {
  const tokenEnv = args['token-env'];
  const token = tokenEnv ? process.env[tokenEnv] : null;
  if (tokenEnv && !token) {
    throw new Error(`environment variable ${tokenEnv} is empty or unset`);
  }
  if (!token && (!args.user || !args.password)) {
    throw new Error('provide --user and --password together, or use --token-env');
  }
  return { user: args.user, password: args.password, token };
}

async function runDoctor(args) {
  requireArgs(args, ['scenario']);
  let failures = 0;

  const nodeMajor = Number(process.versions.node.split('.')[0]);
  checkLine(nodeMajor >= 20, 'Node.js version', process.versions.node);
  if (nodeMajor < 20) failures++;

  let pkg = null;
  try {
    pkg = new ScenarioPackage(args.scenario).load();
    checkLine(true, 'scenario package loaded', path.resolve(args.scenario));
    checkLine(true, 'tasks', pkg.tasks.map((t) => `${t.id}:${t.algorithmId}(${t.type})`).join(', '));
    checkLine(true, 'task strategies', pkg.tasks.map((t) => `${t.id}=${strategyForTaskType(t.type).id}`).join(', '));
    checkLine(true, 'target FPS', pkg.tasks.map((t) => `${t.id}=${t.targetFps ?? 'N/A'}`).join(', '));
    checkLine(true, 'video mode', pkg.videoMode);
    checkLine(true, 'load profile', pkg.loadProfile.map((s) => `${s.channels}ch/${s.holdSec}s`).join(' -> '));
  } catch (err) {
    checkLine(false, 'scenario package loaded', err.message);
    failures++;
  }

  if (pkg) {
    try {
      for (const item of pkg.layoutSavePayloads) {
        checkLine(true, `layout/save payload ${item.taskId}`, `algorithmId=${item.payload.algorithmId}, category=${item.payload.algorithmCategory ?? '-'}`);
      }
    } catch (err) {
      checkLine(false, 'layout/save payload', err.message);
      failures++;
    }

    if (pkg.videoMode === 'local') {
      for (const src of pkg.videos.local ?? []) {
        const ok = Boolean(src.filePath || (src.file && fs.existsSync(src.file)));
        checkLine(ok, `local video ${src.name ?? src.file ?? src.filePath}`, src.filePath ?? src.file ?? '');
        if (!ok) failures++;
      }
    }
  }

  if (args.output) {
    try {
      checkLine(true, 'output directory writable', ensureWritableDir(args.output));
    } catch (err) {
      checkLine(false, 'output directory writable', err.message);
      failures++;
    }
  }

  if (args.device || args.user || args.password || args['token-env']) {
    if (!args.device) {
      checkLine(false, 'device login', 'provide --device with credentials');
      failures++;
    } else {
      try {
        const auth = deviceAuth(args);
        const client = new CosmoClient({
          base: args.device,
          ...auth,
          lang: args.lang ?? 'zh-CN',
        });
        await client.login();
        checkLine(true, 'device login', args.device);
        const info = await client.queryDeviceInfo();
        const flat = {};
        for (const it of info?.devInfoList ?? []) {
          if (it?.key) flat[it.key] = it.value;
        }
        checkLine(true, 'device info', `${flat.deviceType ?? flat.deviceModel ?? 'unknown'} / ${flat.deviceSn ?? flat.sn ?? 'unknown'} / ${flat.softwareVersion ?? 'unknown'}`);
      } catch (err) {
        checkLine(false, 'device login/info', err.message);
        failures++;
      }
    }
  } else {
    checkLine(true, 'device checks skipped', 'provide --device with login credentials or --token-env to enable');
  }

  if (failures) {
    console.error(`\ndoctor failed: ${failures} check(s) need attention.`);
    process.exit(1);
  }
  console.log('\ndoctor passed: this scenario is ready to run.');
}

function copyFileRequired(from, to) {
  if (!fs.existsSync(from)) throw new Error(`file not found: ${from}`);
  fs.copyFileSync(from, to);
}

function writeNewFile(file, content) {
  if (fs.existsSync(file)) throw new Error(`refuse to overwrite existing file: ${file}`);
  fs.writeFileSync(file, content, 'utf8');
}

function yamlString(value) {
  return JSON.stringify(String(value));
}

function buildEffectiveLoadProfile(loadProfile, mode = 'capacity') {
  const configured = [...(loadProfile ?? [])]
    .map((s) => ({ channels: Number(s.channels), holdSec: Number(s.holdSec) }))
    .filter((s) => Number.isInteger(s.channels) && s.channels > 0 && Number.isFinite(s.holdSec) && s.holdSec > 0)
    .sort((a, b) => a.channels - b.channels);
  if (!configured.length) return [];
  if (mode === 'configured') return configured;
  if (mode !== 'capacity') {
    throw new Error(`unsupported --profile "${mode}", expected capacity or configured`);
  }

  const maxChannels = configured[configured.length - 1].channels;
  const holdFor = (channels) => {
    const exact = configured.find((s) => s.channels === channels);
    if (exact) return exact.holdSec;
    const lower = [...configured].reverse().find((s) => s.channels < channels);
    const upper = configured.find((s) => s.channels > channels);
    return lower?.holdSec ?? upper?.holdSec ?? configured[0].holdSec;
  };
  const expanded = [];
  for (let channels = 1; channels <= maxChannels; channels++) {
    expanded.push({ channels, holdSec: holdFor(channels) });
  }
  return expanded;
}

async function initScenario(args) {
  requireArgs(args, ['name', 'template', 'video']);
  const name = args.name;
  const outDir = path.resolve(args.output ?? path.join('scenarios', name));
  if (fs.existsSync(outDir) && fs.readdirSync(outDir).length > 0) {
    throw new Error(`output directory already exists and is not empty: ${outDir}`);
  }
  fs.mkdirSync(outDir, { recursive: true });

  const templatePath = path.resolve(args.template);
  const template = JSON.parse(fs.readFileSync(templatePath, 'utf8'));
  const algorithmId = String(args['algorithm-id'] ?? template.algorithmCode ?? template.algorithmId ?? template.id ?? '');
  if (!algorithmId) throw new Error('cannot derive algorithm id; pass --algorithm-id');

  const isVlm = detectVlmMode(template).direct;
  const taskType = isVlm ? 'vlm' : 'cv';
  const defaultHoldSec = isVlm ? DEFAULT_VLM_HOLD_SEC : DEFAULT_HOLD_SEC;
  const displayName = args['display-name'] ?? `${name} (algorithm ${algorithmId}${args['target-fps'] ? `, fps${args['target-fps']}` : ''})`;
  const scheduleId = args['schedule-id'] ?? 'default-schedule';
  const videoPath = path.resolve(args.video);
  const videoName = path.basename(videoPath);
  const targetFps = args['target-fps'] == null ? null : Number(args['target-fps']);
  if (args['target-fps'] != null && (!Number.isFinite(targetFps) || targetFps <= 0)) {
    throw new Error('--target-fps must be a positive number');
  }

  copyFileRequired(templatePath, path.join(outDir, 'algorithm-template.json'));
  copyFileRequired(videoPath, path.join(outDir, videoName));

  const targetFpsLine = targetFps == null ? '' : `    targetFps: ${targetFps}\n`;

  writeNewFile(path.join(outDir, 'scenario.yml'), `name: ${yamlString(name)}
displayName: ${yamlString(displayName)}
sampleIntervalSec: 3

channels:
  mode: local
  repeatCount: 0
  sources:
    - name: ${yamlString(path.parse(videoName).name)}
      file: ${yamlString(videoName)}

tasks:
  - id: ${yamlString(name)}
    displayName: ${yamlString(displayName)}
    type: ${taskType}
    algorithmId: "${algorithmId}"
    scheduleId: ${yamlString(scheduleId)}
    template: algorithm-template.json
${targetFpsLine}
loadProfile:
  - channels: 1
    holdSec: ${defaultHoldSec}
  - channels: 4
    holdSec: ${defaultHoldSec}
  - channels: 8
    holdSec: ${defaultHoldSec}
  - channels: 16
    holdSec: ${defaultHoldSec}
  - channels: 24
    holdSec: ${defaultHoldSec}

thresholds:
  pass:
    maxCriticalPathLatencyMs: 200
    maxDetectorLatencyMs: 150
    avgDiscardRate: 0.02
    maxPacketDiscardRate: 0.01
`);

  console.log(`Scenario created: ${outDir}`);
  console.log(`Next: node src/cli.js doctor --scenario "${outDir}" --output "reports/${name}"`);
}

async function runBenchmark(args) {
  requireArgs(args, ['device', 'scenario', 'output']);
  const auth = deviceAuth(args);

  const log = new Logger({ verbose: args.verbose });
  const startedAt = new Date().toISOString();
  let pkg = null;
  let deviceInfo = {};
  let channelMgr = null;
  let previewLoad = null;
  const samples = [];
  let baselineFps = null;
  const baselineByTask = {};
  let bottleneckStep = null;
  let bottleneckChannels = null;
  let bottleneckPhase = null;
  let bottleneckReason = null;
  let runError = null;
  let currentChannels = 0;
  let currentStepIndex = -1;
  const writer = new ReportWriter(args.output);
  let effectiveLoadProfile = [];

  const buildResult = (status = runError ? 'aborted' : 'completed') => ({
    scenarioName: pkg?.scenario?.displayName ?? pkg?.scenario?.name,
    tasks: pkg?.tasks?.map((task) => ({
      id: task.id,
      displayName: task.displayName,
      type: task.type,
      algorithmId: task.algorithmId,
      algorithmCode: task.algorithmCode,
      scheduleId: task.scheduleId,
      targetFps: task.targetFps,
      templateFile: task.templateFile,
    })) ?? [],
    bindings: pkg?.bindings ?? [],
    algorithmId: pkg?.algorithmId,
    algorithmName: pkg?.template?.algorithmName,
    targetFps: pkg?.targetFps,
    videoMode: pkg?.videoMode,
    status,
    error: runError ? { message: runError.message, atChannels: currentChannels || null, atStepIndex: currentStepIndex } : null,
    device: {
      model: deviceInfo.deviceType ?? deviceInfo.deviceModel,
      sn: deviceInfo.deviceSn ?? deviceInfo.sn,
      softwareVersion: deviceInfo.softwareVersion,
      hardwareVersion: deviceInfo.hardwareVersion,
    },
    thresholds: pkg?.thresholds,
    loadProfile: effectiveLoadProfile.map((s, i) => ({ index: i, ...s })),
    configuredLoadProfile: pkg?.loadProfile?.map((s, i) => ({ index: i, ...s })) ?? [],
    profileMode: args.profile ?? 'capacity',
    previewProfile: previewLoad?.profile() ?? { mode: args.preview ?? 'none' },
    bottleneck: bottleneckStep != null
      ? {
          stepIndex: bottleneckStep,
          stepNumber: bottleneckStep + 1,
          channels: bottleneckChannels ?? effectiveLoadProfile?.[bottleneckStep]?.channels,
          targetChannels: effectiveLoadProfile?.[bottleneckStep]?.channels,
          phase: bottleneckPhase,
          reason: bottleneckReason,
        }
      : null,
    baselineFps,
    baselineByTask,
    startedAt,
    endedAt: new Date().toISOString(),
    samples,
    steps: effectiveLoadProfile.map((s, i) => ({ index: i, ...s })),
  });

  const writePartial = async () => {
    if (!pkg) return;
    try {
      await writer.writePartial(buildResult('running'));
    } catch (err) {
      log.warn(`write partial report failed: ${err.message}`);
    }
  };

  try {
    log.info(`Loading scenario package: ${args.scenario}`);
    pkg = new ScenarioPackage(args.scenario).load();
    log.info(`Scenario "${pkg.scenario.name}" | tasks=${pkg.tasks.map((t) => `${t.id}:${t.algorithmId}`).join(', ')} | mode=${pkg.videoMode}`);
    effectiveLoadProfile = buildEffectiveLoadProfile(pkg.loadProfile, args.profile ?? 'capacity');
    const maxChannels = Math.max(...effectiveLoadProfile.map((s) => s.channels));
    log.info(`Profile mode: ${args.profile ?? 'capacity'} | configured=${pkg.loadProfile.map((s) => s.channels).join(',')} | effective=${effectiveLoadProfile.map((s) => s.channels).join(',')}`);

    log.info(`Connecting to device ${args.device}...`);
    const client = new CosmoClient({
      base: args.device,
      ...auth,
      lang: args.lang ?? 'zh-CN',
    });
    await client.login();
    log.info('Login OK.');

    const deviceInfoRaw = await client.queryDeviceInfo().catch((e) => {
      log.warn(`QueryDeviceInfo failed: ${e.message}`);
      return {};
    });
    for (const it of deviceInfoRaw?.devInfoList ?? []) {
      if (it?.key) deviceInfo[it.key] = it.value;
    }

    if (args['skip-import']) {
      log.info('Skipping layout save (--skip-import).');
    } else {
      for (const item of pkg.layoutSavePayloads) {
        log.info(`Saving orchestration template for task "${item.taskId}" via /algorithm/layout/save...`);
        await client.layoutSave(item.payload);
      }
      log.info(`Layout saved for ${pkg.layoutSavePayloads.length} task(s).`);
    }

    channelMgr = new ChannelManager(client, {
      channelPrefix: `bench-${pkg.scenario.name}`,
      reuse: !args['no-reuse'],
      cleanup: args.cleanup,
      logger: log,
    });
    log.info(`Ensuring ${maxChannels} channels (mode=${pkg.videoMode})...`);
    const videoChannelIds = await channelMgr.ensureChannels(pkg.videos, maxChannels);
    log.info(`Channels ready: ${videoChannelIds.join(', ')}`);

    const runner = new TaskRunner(client, {
      tasks: pkg.tasks,
      bindings: pkg.bindings,
      rampBatchSize: Number(args['ramp-batch-size'] ?? 1),
      rampBatchDelaySec: Number(args['ramp-batch-delay-sec'] ?? 15),
    }, log);
    runner.setChannels(videoChannelIds);

    previewLoad = new PreviewLoad(client, {
      mode: args.preview ?? 'none',
      streamLimit: args['preview-streams'] ?? 'all',
      clientsPerStream: Number(args['preview-clients'] ?? 1),
      mediaBase: args['media-base'],
      srsApiBase: args['srs-api'],
      ffmpeg: args.ffmpeg ?? 'ffmpeg',
      logger: log,
    });

    const sampler = new MetricsSampler(client, log);
    const activeEntries = () => runner.expectedTaskEntries(runner.allChannelIds.slice(0, currentChannels));
    const FPS_HALVE_RATIO = 0.5;
    const DISCARD_BOTTLENECK = 0.05;

    const captureSample = async (phase = 'hold', targetChannels = currentChannels) => {
      previewLoad.assertHealthy();
      const sample = await sampler.sample(activeEntries());
      sample.preview = await previewLoad.snapshot();
      sample.stepIndex = currentStepIndex;
      sample.phase = phase;
      sample.targetChannels = targetChannels;
      samples.push(sample);
      await writePartial();
      const ch0 = sample.channels[0];
      log.debug(`sample step=${currentStepIndex} ch=${sample.activeChannels} bindings=${sample.activeTaskBindings ?? sample.channels.length} first=${ch0?.taskKey ?? '-'} fps=${ch0?.measuredFps ?? '-'} discard=${ch0?.discardRate ?? '-'} cpu=${sample.hardware?.cpuUtilization?.usedPercent ?? '-'}%`);
      return sample;
    };

    const lastConsecutive = (selector, predicate) => {
      let count = 0;
      for (let i = samples.length - 1; i >= 0; i--) {
        const value = selector(samples[i]);
        if (typeof value !== 'number' || !predicate(value)) break;
        count++;
      }
      return count;
    };

    const recentAverage = (selector, windowMs, minSamples = 3) => {
      const now = samples[samples.length - 1]?.ts ?? Date.now();
      const values = samples
        .filter((s) => now - s.ts <= windowMs)
        .map(selector)
        .filter((v) => typeof v === 'number' && Number.isFinite(v));
      return values.length >= minSamples ? values.reduce((a, b) => a + b, 0) / values.length : null;
    };

    const meanChannelDiscard = (s) => {
      const values = (s.channels ?? [])
        .filter((ch) => !ch.missing && typeof ch.discardRate === 'number')
        .map((ch) => ch.discardRate);
      return values.length ? values.reduce((a, b) => a + b, 0) / values.length : null;
    };

    const quickFuse = (sample) => {
      const reasons = [];
      if (sample.hardware?._error && sample.channels?.length && sample.channels.every((c) => c.missing)) {
        reasons.push('RunningDetail and HardwareResource unavailable');
      }
      const memAvg60s = recentAverage((s) => s.hardware?.generalMemoryUtilization?.usedPercent, 60_000);
      const mem98Count = lastConsecutive((s) => s.hardware?.generalMemoryUtilization?.usedPercent, (v) => v >= 98);
      const cpu98Count = lastConsecutive((s) => s.hardware?.cpuUtilization?.usedPercent, (v) => v >= 98);
      const npu98Count = lastConsecutive((s) => s.hardware?.npuUtilization?.usedPercent, (v) => v >= 98);
      const discardCount = lastConsecutive(meanChannelDiscard, (v) => v > DISCARD_BOTTLENECK);
      if (mem98Count >= 3) reasons.push(`memory >= 98% for ${mem98Count} consecutive samples`);
      if (memAvg60s != null && memAvg60s >= 95) reasons.push(`memory 60s average ${memAvg60s.toFixed(1)}% >= 95%`);
      if (cpu98Count >= 3) reasons.push(`CPU >= 98% for ${cpu98Count} consecutive samples`);
      // if (npu98Count >= 3) reasons.push(`NPU >= 98% for ${npu98Count} consecutive samples`);
      if (discardCount >= 2) reasons.push(`discardRate > ${DISCARD_BOTTLENECK} for ${discardCount} consecutive samples`);
      return reasons.length ? { stop: true, reason: reasons.join('; ') } : { stop: false };
    };

    const staircaseResult = await runner.runStaircase(effectiveLoadProfile, {
      onRampBatch: async (step, active) => {
        currentChannels = active.length;
        currentStepIndex = step.index;
        return quickFuse(await captureSample('ramp', step.channels));
      },
      onStepStart: async (step, active, entries) => {
        currentChannels = active.length;
        currentStepIndex = step.index;

        const hasVLM = activeEntries().some((e) => e.taskType === 'vlm');
        if (hasVLM && step.index === 0) {
          log.info(`[warmup] VLM detected in first step, waiting 30 seconds for model loading before sampling...`);
          await new Promise((resolve) => setTimeout(resolve, 30000));
        }
        await previewLoad.sync(entries);
      },
      onSample: async () => {
        await captureSample('hold', currentChannels);
      },
      onStepEnd: async (step) => {
        const summary = summarizeStep(step, samples, pkg.thresholds, pkg.videoMode);
        const minFps = summary.minFpsAcross;
        const meanDiscard = summary.avgDiscard;
        const maxNpu = summary.maxNpu;
        const maxCpu = summary.maxCpu;
        const taskStats = summary.taskStats ?? [];
        const sat = `npu=${maxNpu ?? '-'}% cpu=${maxCpu ?? '-'}%`;
        if (step.index === 0) {
          baselineFps = minFps ?? null;
          for (const stat of taskStats) {
            baselineByTask[stat.taskKey] = stat.minThroughputFps;
          }
          const baselineText = taskStats
            .map((stat) => `${stat.taskKey}=${stat.minThroughputFps == null ? '-' : stat.minThroughputFps.toFixed(2)}fps`)
            .join(', ');
          log.info(`[baseline] step 1 steady minFps=${baselineFps} fps (${sat}) tasks=[${baselineText}] -> strategy gates active`);
          return;
        }
        const decision = runtimeStepDecision(summary, {
          thresholds: pkg.thresholds,
          baselineByTask,
          fpsHalveRatio: FPS_HALVE_RATIO,
          discardBottleneck: DISCARD_BOTTLENECK,
        });
        const satNote = ((maxNpu ?? 0) >= 90 || (maxCpu ?? 0) >= 90) ? ' [resource near saturation]' : '';
        log.info(`[step ${step.index + 1}] steady minFps=${minFps?.toFixed(1) ?? '-'} meanDiscard=${meanDiscard?.toFixed(3) ?? '-'} ${sat}${satNote} ${decision.stop ? '-> BOTTLENECK (' + decision.reason + ')' : '-> ok, continuing'}`);
        return decision.stop ? { stop: true, reason: decision.reason } : { stop: false };
      },
    }, pkg.sampleIntervalSec);
    bottleneckStep = staircaseResult?.bottleneckStep ?? null;
    bottleneckChannels = staircaseResult?.bottleneckChannels ?? null;
    bottleneckPhase = staircaseResult?.bottleneckPhase ?? null;
    bottleneckReason = staircaseResult?.bottleneckReason ?? null;
  } catch (err) {
    runError = err;
    log.error(`Benchmark aborted; a partial report will be written: ${err.message}`);
  } finally {
    if (previewLoad) {
      try {
        await previewLoad.stop();
      } catch (e) {
        log.warn(`Preview cleanup failed: ${e.message}`);
      }
    }
    if (channelMgr) {
      try {
        await channelMgr.finish();
      } catch (e) {
        log.warn(`Channel cleanup failed: ${e.message}`);
      }
    }
  }

  if (!pkg) {
    console.error(`\nBenchmark failed: ${runError?.message ?? 'unknown error'}`);
    if (runError?.stack && process.env.BENCH_DEBUG) console.error(runError.stack);
    process.exit(1);
  }

  const result = buildResult();
  const { jsonPath, htmlPath } = await writer.write(result);
  if (runError) {
    log.warn(`Partial report written:\n  ${jsonPath}\n  ${htmlPath}`);
    console.error(`\nBenchmark aborted: ${runError.message}`);
    if (runError.stack && process.env.BENCH_DEBUG) console.error(runError.stack);
    process.exit(1);
  }
  log.info(`Report written:\n  ${jsonPath}\n  ${htmlPath}`);
}

async function runPreviewCommand(args) {
  requireArgs(args, ['device', 'channel', 'output', 'media-base', 'srs-api']);
  const auth = deviceAuth(args);
  const log = new Logger({ verbose: args.verbose });
  const client = new CosmoClient({
    base: args.device,
    ...auth,
    lang: args.lang ?? 'zh-CN',
  });
  await client.login();
  const report = await runPreviewValidation(client, {
    channelId: args.channel,
    algorithmId: args.algorithm ?? '',
    mode: args.mode,
    output: args.output,
    mediaBase: args['media-base'],
    srsApiBase: args['srs-api'],
    durationSec: args['duration-sec'],
    clients: args.clients,
    lifecycleIterations: args['lifecycle-iterations'],
    minOverlayPixelDelta: args['min-overlay-pixel-delta'],
    ffmpeg: args.ffmpeg,
    ffprobe: args.ffprobe,
    logger: log,
  });
  log.info(`Preview validation ${report.status}: ${path.join(path.resolve(args.output), 'preview-validation.json')}`);
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (!args.command || args.command === 'help') {
    printHelp();
    process.exit(args.command === 'help' ? 0 : 1);
  }
  if (args.command === 'doctor') {
    await runDoctor(args);
    return;
  }
  if (args.command === 'init-scenario') {
    await initScenario(args);
    return;
  }
  if (args.command === 'preview') {
    await runPreviewCommand(args);
    return;
  }
  await runBenchmark(args);
}

main().catch((err) => {
  console.error(`\nscenario-bench failed: ${err.message}`);
  if (err.stack && process.env.BENCH_DEBUG) console.error(err.stack);
  process.exit(1);
});
