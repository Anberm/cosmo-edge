// Emit metrics.json, summary.json, and a self-contained HTML report.

import fs from 'node:fs';
import path from 'node:path';
import { summarizeStep } from './step-evaluator.js';
import {
  strategyForTask,
  strategyForTaskType,
  thresholdLabel,
} from './task-strategies.js';

export class ReportWriter {
  constructor(outputDir, { partialWriteIntervalMs = 30_000, now = () => Date.now() } = {}) {
    this.outputDir = path.resolve(outputDir);
    this.partialWriteIntervalMs = partialWriteIntervalMs;
    this.now = now;
    this.lastPartialWriteAt = null;
  }

  async write(runResult) {
    fs.mkdirSync(this.outputDir, { recursive: true });

    const jsonPath = path.join(this.outputDir, 'metrics.json');
    fs.writeFileSync(jsonPath, JSON.stringify(runResult, null, 2), 'utf8');

    const stepSummaries = this._summarizeSteps(runResult);
    const summary = this._buildSummary(runResult, stepSummaries);
    const summaryPath = path.join(this.outputDir, 'summary.json');
    fs.writeFileSync(summaryPath, JSON.stringify(summary, null, 2), 'utf8');

    const html = this._renderHtml(runResult, stepSummaries, summary);
    const htmlPath = path.join(this.outputDir, 'report.html');
    fs.writeFileSync(htmlPath, html, 'utf8');

    return { jsonPath, summaryPath, htmlPath };
  }

  async writePartial(runResult, { force = false } = {}) {
    const now = this.now();
    if (!force && this.lastPartialWriteAt != null
        && now - this.lastPartialWriteAt < this.partialWriteIntervalMs) {
      return { jsonPath: null, skipped: true };
    }
    fs.mkdirSync(this.outputDir, { recursive: true });
    const jsonPath = path.join(this.outputDir, 'metrics.partial.json');
    const temporaryPath = `${jsonPath}.tmp`;
    fs.writeFileSync(temporaryPath, JSON.stringify(runResult, null, 2), 'utf8');
    fs.renameSync(temporaryPath, jsonPath);
    this.lastPartialWriteAt = now;
    return { jsonPath, skipped: false };
  }

  _summarizeSteps(r) {
    const samples = r.samples ?? [];
    const thresholds = r.thresholds ?? {};
    const videoMode = r.videoMode ?? 'local';
    return buildReportSteps(r).map((st) =>
      this._summarizeStep(st, samples, thresholds, videoMode),
    );
  }

  _summarizeStep(step, samples, thresholds, videoMode) {
    return summarizeStep(step, samples, thresholds, videoMode);
  }

  _buildSummary(r, stepSummaries) {
    const ran = stepSummaries.filter((s) => !s.skipped);
    const firstFailed = ran.find((s) => s.pass === false) ?? null;
    const bottleneck = normalizeBottleneck(r.bottleneck ?? (firstFailed
      ? {
          stepIndex: firstFailed.step.index,
          stepNumber: firstFailed.step.index + 1,
          channels: firstFailed.channels,
          reason: firstFailed.reasons.join('; '),
        }
      : null), stepSummaries);
    const hasBottleneck = Boolean(bottleneck);
    const verifiedPassed = ran.filter((s) =>
      s.pass && (!hasBottleneck || s.step.index < bottleneck.stepIndex),
    );
    const maxVerifiedPassedChannels = verifiedPassed.length
      ? Math.max(...verifiedPassed.map((s) => s.channels))
      : null;
    const continuousProfile = r.profileMode === 'capacity' || isContinuousChannelProfile(stepSummaries);
    const maxStableChannels = continuousProfile ? maxVerifiedPassedChannels : null;
    const allRanStepsPass = ran.length > 0 && ran.every((s) => s.pass);
    const capacityMeasured = r.status !== 'aborted'
      && continuousProfile
      && maxVerifiedPassedChannels != null
      && (hasBottleneck || allRanStepsPass);
    const overallPass = r.status !== 'aborted' && allRanStepsPass && !hasBottleneck;

    let conclusion;
    if (r.status === 'aborted') {
      conclusion = `压测中断：运行到 ${r.error?.atChannels ?? '?'} 路时停止，原因：${r.error?.message ?? '未知错误'}`;
    } else if (bottleneck) {
      if (continuousProfile) {
        conclusion = `容量上限：${maxStableChannels ?? 0} 路；第 ${bottleneck.stepNumber} 阶段（${bottleneck.channels} 路）触发失败/停止，原因：${bottleneck.reason}`;
      } else {
        const upper = bottleneck.channels;
        conclusion = `已验证通过阶梯：${maxVerifiedPassedChannels ?? 0} 路；连续最大稳定路数未精确测定，已知 >= ${maxVerifiedPassedChannels ?? 0} 路且 < ${upper} 路；第 ${bottleneck.stepNumber} 阶段 ${bottleneck.channels} 路触发失败/停止，原因：${bottleneck.reason}`;
      }
    } else if (overallPass) {
      conclusion = continuousProfile
        ? `全部测试路数通过；容量上限至少为 ${maxStableChannels ?? '-'} 路`
        : `全部配置阶梯通过；已验证通过阶梯：${maxVerifiedPassedChannels ?? '-'} 路；连续最大稳定路数未精确测定`;
    } else {
      conclusion = '没有足够的有效采样点形成结论';
    }

    return {
      scenarioName: r.scenarioName,
      algorithmId: r.algorithmId,
      algorithmName: r.algorithmName,
      tasks: r.tasks ?? [],
      targetFps: r.targetFps,
      videoMode: r.videoMode,
      previewProfile: r.previewProfile ?? { mode: 'none' },
      profileMode: r.profileMode ?? 'configured',
      status: r.status,
      overallPass,
      allRanStepsPass,
      hasBottleneck,
      capacityMeasured,
      conclusion,
      maxStableChannels,
      maxStableChannelsExact: continuousProfile,
      maxVerifiedPassedChannels,
      firstFailedStep: firstFailed ? {
        stepIndex: firstFailed.step.index,
        stepNumber: firstFailed.step.index + 1,
        channels: firstFailed.channels,
        reasons: firstFailed.reasons,
      } : null,
      capacityBound: !continuousProfile && bottleneck ? {
        lowerInclusive: maxVerifiedPassedChannels,
        upperExclusive: bottleneck.channels,
      } : null,
      bottleneck,
      baselineFps: reportBaselineFps(r, stepSummaries),
      baselineByTask: r.baselineByTask ?? {},
      device: r.device,
      startedAt: r.startedAt,
      endedAt: r.endedAt,
      sampleCount: (r.samples ?? []).length,
      mediaStages: stepSummaries.map((step) => ({ channels: step.channels, ...step.mediaStages })),
    };
  }

  _renderHtml(r, stepSummaries, summary) {
    const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const pass = r.thresholds?.pass ?? {};
    const sampleInterval = estimateSampleIntervalSec(r.samples ?? []);
    const samplingText = Number.isFinite(sampleInterval)
      ? `约每 ${Math.round(sampleInterval)}s 采样一次；阶梯汇总使用该阶梯后半段采样点作为稳定窗口。`
      : '阶梯汇总使用该阶梯后半段采样点作为稳定窗口。';
    const profileText = summary.maxStableChannelsExact
      ? '当前按连续路数扫描，可直接给出容量上限。容量上限是最后一个完整执行且通过报告阈值的路数。'
      : `当前阶梯不是连续通道数，只能给出已验证通过阶梯；连续最大稳定路数需在相邻区间内补测。${summary.capacityBound ? `本次已知 >= ${summary.capacityBound.lowerInclusive ?? 0} 路且 < ${summary.capacityBound.upperExclusive} 路。` : ''}`;
    const interpretationRows = [
      ['容量结论', profileText],
      ['路数 PASS/FAIL', `每个任务按 task type 选择判定策略。CV 默认使用关键链路、检测节点和丢弃率；VLM 默认使用分析 FPS 达标率和采样缺失率，并可配置端到端延时。全局平均丢弃率阈值为 ${pass.avgDiscardRate ?? pass.maxDiscardRate ?? '-'}。`],
      ['瓶颈停止', '运行期保护熔断，用于避免设备继续加压。CV 使用稳定窗口 FPS 相对基线折半作为保护；VLM 使用目标 FPS 达标率，避免低帧率任务被短窗口误判。丢弃率、CPU、内存等保护仍然通用。'],
      ['采样窗口', samplingText],
      ['失败原因', '表格中的失败原因来自未通过的阈值项；若同时存在瓶颈停止，顶部横幅展示触发提前停止的运行期原因。'],
    ];
    const baseRows = [
      ['场景', r.scenarioName],
      ['任务', formatTaskList(r.tasks, r.algorithmId, r.algorithmName)],
      ['任务策略', formatTaskStrategies(r.tasks)],
      ['编排设定 FPS（参考）', formatTargetFps(r.tasks, r.targetFps)],
      ['视频模式', r.videoMode],
      ['预览负载', formatPreviewProfile(r.previewProfile)],
      ['设备', `${r.device?.model ?? ''} / ${r.device?.sn ?? ''}`],
      ['软件版本', r.device?.softwareVersion ?? ''],
      ['开始时间', r.startedAt],
      ['结束时间', r.endedAt],
      ['总采样点', (r.samples ?? []).length],
      ['基线 FPS（参考）', summary.baselineFps != null ? `${summary.baselineFps} (step 1, 1ch)` : '-'],
    ];

    const bottleneckBanner = summary.bottleneck
      ? `<div class="banner warn"><strong>检测到瓶颈</strong><br>第 ${summary.bottleneck.stepNumber} 阶段（${summary.bottleneck.channels} 路${summary.bottleneck.targetChannels && summary.bottleneck.targetChannels !== summary.bottleneck.channels ? `，目标 ${summary.bottleneck.targetChannels} 路` : ''}）触发停止。原因：${esc(summary.bottleneck.reason)}</div>`
      : '';

    const abortedBanner = r.status === 'aborted'
      ? `<div class="banner error"><strong>压测中断（部分报告）</strong><br>运行到 ${r.error?.atChannels ?? '?'} 路 / 第 ${r.error?.atStepIndex != null ? r.error.atStepIndex + 1 : '?'} 阶段时中断。原因：${esc(r.error?.message ?? '未知错误')}</div>`
      : '';

    const runBadge = summary.hasBottleneck
      ? { className: 'warn', label: 'STOPPED' }
      : (summary.overallPass ? { className: 'pass', label: 'PASS' } : { className: 'fail', label: 'FAIL' });
    const stepStatus = (s) => {
      if (s.skipped) return { className: 'na', label: 'SKIP' };
      if (summary.bottleneck?.stepIndex === s.step.index
          && (summary.bottleneck.channels == null || summary.bottleneck.channels === s.channels)) {
        return { className: 'warn', label: 'STOPPED' };
      }
      return s.pass ? { className: 'pass', label: 'PASS' } : { className: 'fail', label: 'FAIL' };
    };

    const stepRows = stepSummaries.map((s, rowIndex) => {
      const status = stepStatus(s);
      return `
      <tr>
        <td>${rowIndex + 1}</td>
        <td>${s.channels}</td>
        <td>${s.holdSec}s</td>
        <td>${s.targetFps ?? '-'}</td>
        <td>${s.minFpsAcross ?? '-'}</td>
        <td>${s.criticalPathLatencyMs ?? '-'}</td>
        <td>${s.detectorLatencyMs ?? '-'}</td>
        <td>${s.avgDiscard != null ? s.avgDiscard : '-'}</td>
        <td>${s.maxDiscard != null ? s.maxDiscard : '-'}</td>
        <td class="${s.maxNpu >= 90 ? 'fail' : ''}">${s.maxNpu != null ? s.maxNpu + '%' : '-'}</td>
        <td class="${s.maxAcceleratorMem >= 90 ? 'fail' : ''}">${s.maxAcceleratorMem != null ? s.maxAcceleratorMem + '%' : '-'}</td>
        <td class="${s.maxCpu >= 90 ? 'fail' : ''}">${s.maxCpu != null ? s.maxCpu + '%' : '-'}</td>
        <td class="${s.maxMem >= 90 ? 'fail' : ''}">${s.maxMem != null ? s.maxMem + '%' : '-'}</td>
        <td class="${status.className}">${status.label}</td>
        <td>${esc((s.reasons ?? []).join('; '))}</td>
      </tr>`;
    }).join('');

    const verdictRows = stepSummaries.flatMap((s) =>
      s.perThreshold.map((t) => `
        <tr>
          <td>${s.channels}ch</td>
          <td>${esc(t.taskDisplayName ?? t.taskKey ?? '-')}</td>
          <td>${esc(t.strategy ?? '-')}</td>
          <td>${esc(thresholdLabel(t.name, strategyForTask(t)))}</td>
          <td>${t.threshold ?? '-'}</td>
          <td>${t.actual ?? '-'}</td>
          <td class="${t.result === 'PASS' ? 'pass' : (t.result === 'FAIL' ? 'fail' : 'na')}">${t.result}</td>
        </tr>`),
    ).join('');

    const taskRows = stepSummaries.flatMap((s) =>
      (s.taskStats ?? []).map((t) => `
        <tr>
          <td>${s.channels}ch</td>
          <td>${esc(t.taskDisplayName ?? t.taskKey)}</td>
          <td>${esc(t.strategy)}</td>
          <td>${t.algorithmId ?? '-'}</td>
          <td>${t.bindingCount}</td>
          <td>${t.minThroughputFps ?? '-'}</td>
          <td>${t.minFpsRatio != null ? formatPercent(t.minFpsRatio) : '-'}</td>
          <td>${t.maxMissingRate != null ? formatPercent(t.maxMissingRate) : '-'}</td>
          <td>${t.avgDiscardRate ?? '-'}</td>
          <td>${t.maxPrimaryLatencyMs ?? '-'}</td>
          <td>${t.maxCriticalPathLatencyMs ?? '-'}</td>
        </tr>`),
    ).join('');

    const mediaRows = stepSummaries.map((s) => {
      const m = s.mediaStages ?? {};
      return `<tr>
        <td>${s.channels}ch</td>
        <td>${metric(m.preprocessAvgMs)}</td>
        <td>${metric(m.inferAvgMs)}</td>
        <td>${metric(m.postprocessAvgMs)}</td>
        <td>${metric(m.osdAvgMs)}</td>
        <td>${metric(m.publishAvgMs)}</td>
        <td>${metric(m.firstFrameAvgMs)}/${metric(m.firstFrameMaxMs)}</td>
        <td>${m.activePreviewStreamsPeak ?? '-'}/${m.activePreviewPublishersPeak ?? '-'}</td>
        <td>${m.activeRawPreviewStreamsPeak ?? '-'}/${m.activeAlgorithmPreviewStreamsPeak ?? '-'}</td>
        <td>${m.srsStreamsPeak ?? '-'}/${m.srsClientsPeak ?? '-'}</td>
        <td>${m.previewStartsDelta ?? '-'}/${m.previewStopsDelta ?? '-'}/${m.previewFailuresDelta ?? '-'}</td>
      </tr>`;
    }).join('');

    return `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>压测报告 - ${esc(r.scenarioName)}</title>
<style>
  body{font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;margin:24px;color:#1a1a1a}
  h1{font-size:20px} h2{font-size:16px;margin-top:28px;border-bottom:1px solid #ddd;padding-bottom:4px}
  table{border-collapse:collapse;width:100%;margin:8px 0}
  th,td{border:1px solid #ddd;padding:6px 10px;text-align:left;vertical-align:top}
  th{background:#f5f5f5}.pass{color:#16a34a;font-weight:600}
  .fail{color:#dc2626;font-weight:600}.na{color:#888}
  .badge{display:inline-block;padding:4px 12px;border-radius:4px;color:#fff;font-weight:600}
  .badge.pass{background:#16a34a}.badge.fail{background:#dc2626}.badge.warn{background:#f59e0b}
  .summary{background:#f8fafc;border:1px solid #cbd5e1;padding:12px 14px;border-radius:4px;margin:12px 0}
  .banner{padding:10px 14px;border-radius:4px;margin:12px 0}.warn{background:#fef3c7;border:1px solid #f59e0b}.error{background:#fee2e2;border:1px solid #dc2626}
  .note-table th{width:140px}
</style></head><body>
<h1>压测报告</h1>
<p>总体结果: <span class="badge ${runBadge.className}">${runBadge.label}</span>${r.status === 'aborted' ? ' <span class="badge fail">ABORTED</span>' : ''}</p>
<div class="summary"><strong>结论</strong><br>${esc(summary.conclusion)}</div>
${abortedBanner}
${bottleneckBanner}
<h2>判定口径</h2>
<table class="note-table">${interpretationRows.map(([k, v]) => `<tr><th>${esc(k)}</th><td>${esc(v)}</td></tr>`).join('')}</table>
<h2>基础信息</h2>
<table>${baseRows.map(([k, v]) => `<tr><th>${esc(k)}</th><td>${esc(v)}</td></tr>`).join('')}</table>
<h2>路数结果</h2>
<table>
  <tr><th>序号</th><th>路数</th><th>保持</th><th>目标FPS(参考)</th><th>处理FPS(参考)</th><th>关键/端到端延时ms</th><th>主节点延时ms</th><th>平均丢弃率</th><th>最差通道丢弃率</th><th>加速器峰值</th><th>加速器内存峰值</th><th>CPU峰值</th><th>内存峰值</th><th>结果</th><th>失败原因</th></tr>
  ${stepRows}
</table>
<h2>媒体与预览分阶段指标</h2>
<table>
  <tr><th>路数</th><th>Preprocess ms</th><th>Infer ms</th><th>Postprocess ms</th><th>OSD ms</th><th>Publish ms</th><th>首帧平均/进程最大ms</th><th>预览流/发布器峰值</th><th>原始/算法预览峰值</th><th>SRS流/客户端峰值</th><th>启动/停止/失败增量</th></tr>
  ${mediaRows}
</table>
<h2>分任务汇总</h2>
<table>
  <tr><th>路数</th><th>任务</th><th>策略</th><th>算法ID</th><th>绑定数</th><th>最低处理FPS</th><th>最低FPS达标率</th><th>最大缺失率</th><th>平均丢弃率</th><th>最大主节点ms</th><th>最大关键/端到端ms</th></tr>
  ${taskRows}
</table>
<h2>阈值判定明细</h2>
<table>
  <tr><th>路数</th><th>任务</th><th>策略</th><th>指标</th><th>阈值</th><th>实测</th><th>结果</th></tr>
  ${verdictRows}
</table>
</body></html>`;
  }
}

function formatTaskList(tasks, legacyAlgorithmId, legacyAlgorithmName) {
  if (!Array.isArray(tasks) || !tasks.length) {
    return `${legacyAlgorithmId ?? '-'} (${legacyAlgorithmName ?? '-'})`;
  }
  return tasks
    .map((task) => `${task.id}: ${task.algorithmId}${task.displayName ? ` (${task.displayName})` : ''}`)
    .join('; ');
}

function formatTargetFps(tasks, legacyTargetFps) {
  if (!Array.isArray(tasks) || !tasks.length) {
    return legacyTargetFps ?? '未提取到';
  }
  return tasks.map((task) => `${task.id}=${task.targetFps ?? 'N/A'}`).join('; ');
}

function formatTaskStrategies(tasks) {
  if (!Array.isArray(tasks) || !tasks.length) return strategyForTaskType('cv').id;
  return tasks
    .map((task) => `${task.id}=${strategyForTaskType(task.type).id}`)
    .join('; ');
}

function formatPreviewProfile(profile) {
  if (!profile || profile.mode === 'none') return 'none（后台推理）';
  return `${profile.mode}; streams=${profile.streamLimit ?? 'all'}; clients/stream=${profile.clientsPerStream ?? 1}`;
}

function metric(value, suffix = '') {
  return typeof value === 'number' && Number.isFinite(value) ? `${round(value, 3)}${suffix}` : '-';
}

function isContinuousChannelProfile(stepSummaries) {
  const channels = stepSummaries
    .map((s) => s.channels)
    .filter((v) => Number.isInteger(v))
    .sort((a, b) => a - b);
  if (!channels.length || channels[0] !== 1) return false;
  for (let i = 1; i < channels.length; i++) {
    if (channels[i] !== channels[i - 1] + 1) return false;
  }
  return true;
}

function buildReportSteps(runResult) {
  const samples = runResult.samples ?? [];
  const steps = runResult.steps ?? [];
  const reportSteps = [];
  const seenChannels = new Set();

  for (const step of steps) {
    const observedChannels = uniqueObservedChannels(
      samples.filter((sample) => sample.stepIndex === step.index),
    );

    const shouldExpand = observedChannels.length > 1
      && observedChannels.some((channels) => channels < step.channels);
    const channelsToReport = shouldExpand ? observedChannels : [step.channels];

    for (const channels of channelsToReport) {
      if (seenChannels.has(channels)) continue;
      seenChannels.add(channels);
      reportSteps.push({
        ...step,
        index: channels - 1,
        sourceIndex: step.index,
        channels,
        targetChannels: step.channels,
        sampleStepIndex: step.index,
        sampleChannels: channels,
      });
    }
  }

  return reportSteps.sort((a, b) => a.channels - b.channels);
}

function uniqueObservedChannels(samples) {
  return [...new Set(
    samples
      .map((s) => Number(s.activeChannels))
      .filter((v) => Number.isInteger(v) && v > 0),
  )].sort((a, b) => a - b);
}

function normalizeBottleneck(bottleneck, stepSummaries) {
  if (!bottleneck) return null;
  const stepRows = stepSummaries.filter((s) => !s.skipped);
  if (!stepRows.length) return bottleneck;

  const channels = stepRows.map((s) => s.channels).filter((v) => Number.isInteger(v));
  if (!channels.length) return bottleneck;

  const matchingRow = stepRows.find((s) => s.channels === bottleneck.channels);
  if (matchingRow) {
    return {
      ...bottleneck,
      stepIndex: matchingRow.step.index,
      stepNumber: matchingRow.step.index + 1,
    };
  }

  const maxObserved = Math.max(...channels);
  if (Number.isInteger(bottleneck.channels) && maxObserved < bottleneck.channels) {
    const observedRow = stepRows.find((s) => s.channels === maxObserved);
    return {
      ...bottleneck,
      stepIndex: observedRow?.step.index ?? bottleneck.stepIndex,
      stepNumber: observedRow ? observedRow.step.index + 1 : bottleneck.stepNumber,
      targetChannels: bottleneck.targetChannels ?? bottleneck.channels,
      channels: maxObserved,
    };
  }

  return bottleneck;
}

function reportBaselineFps(runResult, stepSummaries) {
  const oneChannel = stepSummaries.find((s) => s.channels === 1 && !s.skipped);
  return oneChannel?.minFpsAcross ?? runResult.baselineFps;
}

function formatPercent(v) {
  return `${round(Number(v) * 100, 2)}%`;
}

function round(v, digits) {
  const f = 10 ** digits;
  return Math.round(v * f) / f;
}

function estimateSampleIntervalSec(samples) {
  const deltas = [];
  for (let i = 1; i < samples.length; i++) {
    const prev = samples[i - 1];
    const curr = samples[i];
    if (prev?.stepIndex !== curr?.stepIndex) continue;
    const delta = Number(curr?.ts) - Number(prev?.ts);
    if (Number.isFinite(delta) && delta > 0) deltas.push(delta / 1000);
  }
  if (!deltas.length) return NaN;

  deltas.sort((a, b) => a - b);
  const mid = Math.floor(deltas.length / 2);
  return deltas.length % 2 ? deltas[mid] : (deltas[mid - 1] + deltas[mid]) / 2;
}
