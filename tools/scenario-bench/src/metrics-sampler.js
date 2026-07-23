// metrics-sampler.js — Collect RunningDetail + HardwareResource each tick.
import { isPrimaryThroughputAction, normalizeTaskType } from './task-strategies.js';
//
// Response shapes (verified against source DTOs):
//   RunningDetail: resData.status[] where each item has
//     { taskId, channelId, actionStatus[], nodeDurationInfos[] }
//     actionStatus[]: { actionId, name, statusCode, holdCount, alarmCount,
//       insertCount, processCount, discardCount, periodMs,
//       insertCountPeriod, processCountPeriod, discardCountPeriod }
//   HardwareResource: resData.itemList[] where each item is
//     { key, name, usedPercent, usedSize, unusedSize, available }
//     key in { cpuUtilization, generalMemoryUtilization, npuUtilization,
//       modelMemoryUtilization, pictureMemoryUtilization, TPPMemoryUtilization,
//       specialMemoryUtilization, eMMCUtilization, packetDiscardUtilization }
//
// NOTE: RunningDetail silently drops tasks whose action count <= 2. We detect
// such "sampling-missing" channels by diffing expected taskIds vs returned ones.

const HW_KEYS = [
  'cpuUtilization',
  'generalMemoryUtilization',
  'npuUtilization',
  'modelMemoryUtilization',
  'pictureMemoryUtilization',
  'TPPMemoryUtilization',
  'specialMemoryUtilization',
  'eMMCUtilization',
  'packetDiscardUtilization',
];

export class MetricsSampler {
  /**
   * @param {import('./cosmo-client.js').CosmoClient} client
   * @param {import('./logger.js').Logger} [logger]
   */
  constructor(client, logger) {
    this.client = client;
    this.log = logger;
    this.counterSnapshots = new Map();
  }

  /**
   * Take one sample for the currently active task/channel bindings.
   * @param {Array<object>|Map<string,string>} expectedBindings active task bindings.
   *   The array form is [{ taskKey, taskId, channelId, targetFps, ... }].
   *   A legacy Map channelId -> taskId is still accepted.
   * @param {number} [legacyTargetFps] orchestration fps baseline for legacy callers
   * @returns {Promise<object>} a sample record
   */
  async sample(expectedBindings, legacyTargetFps = null) {
    const ts = Date.now();
    const expected = normalizeExpectedBindings(expectedBindings, legacyTargetFps);
    const activeChannelIds = [...new Set(expected.map((entry) => entry.channelId))];
    const activeTaskIds = [...new Set(expected.map((entry) => entry.taskId))];

    // Sample both endpoints in parallel; either may fail independently.
    const [taskDetail, hwRes] = await Promise.allSettled([
      activeTaskIds.length ? this.client.taskRunningDetail(activeTaskIds) : Promise.resolve({ status: [] }),
      this.client.queryHardwareResource(),
    ]);

    const perBinding = this._parseRunningDetail(taskDetail, expected, ts);
    const hw = this._parseHardware(hwRes);

    return {
      ts,
      iso: new Date(ts).toISOString(),
      activeChannels: activeChannelIds.length,
      activeTaskBindings: expected.length,
      channels: perBinding,
      hardware: hw,
    };
  }

  // ── RunningDetail parsing ──────────────────────────────────────────────

  _parseRunningDetail(detailResult, expectedBindings, ts) {
    const out = [];
    if (detailResult.status !== 'fulfilled') {
      // Whole RunningDetail call failed - mark every expected binding as errored.
      for (const entry of expectedBindings) {
        out.push({ ...entry, error: String(detailResult.reason?.message ?? detailResult.reason), missing: true });
      }
      return out;
    }

    const statusList = detailResult.value?.status ?? [];
    const returnedByTaskId = new Map(statusList.map((s) => [s.taskId, s]));

    for (const entry of expectedBindings) {
      const st = returnedByTaskId.get(entry.taskId);
      if (!st) {
        // Not returned -> either action count <= 2 (silent filter) or task not started yet.
        out.push({ ...entry, missing: true });
        continue;
      }
      out.push({
        ...entry,
        ...this._summarizeTask(st, entry.targetFps, entry.taskType, ts),
        taskKey: entry.taskKey,
        taskDisplayName: entry.taskDisplayName,
        taskType: entry.taskType,
        algorithmId: entry.algorithmId,
        algorithmCode: entry.algorithmCode,
      });
    }
    return out;
  }

  /**
   * Aggregate a task's actions into per-channel throughput.
   * FPS uses the slowest effective action rate instead of summing pipeline nodes,
   * otherwise multi-node graphs overcount the channel throughput.
   */
  _summarizeTask(st, targetFps, taskType, ts) {
    const actions = Array.isArray(st.actionStatus) ? st.actionStatus : [];
    let insertPeriod = 0, processPeriod = 0, discardPeriod = 0;
    let periodMs = 0, holdCount = 0, alarmCount = 0;
    let insertTotal = 0, processTotal = 0, discardTotal = 0;
    let pipelineMinFps = Infinity;
    let primaryFps = null;
    let primaryAction = null;
    let primaryProcessTotal = null;
    let maxDiscardRate = 0;
    const actionSummaries = [];

    for (const a of actions) {
      const actionInsertPeriod = num(a.insertCountPeriod);
      const actionProcessPeriod = num(a.processCountPeriod);
      const actionDiscardPeriod = num(a.discardCountPeriod);
      const actionPeriodMs = num(a.periodMs);

      insertPeriod += actionInsertPeriod;
      processPeriod += actionProcessPeriod;
      discardPeriod += actionDiscardPeriod;
      periodMs = Math.max(periodMs, actionPeriodMs);  // pick the longest window
      holdCount += num(a.holdCount);
      alarmCount += num(a.alarmCount);
      insertTotal += num(a.insertCount);
      processTotal += num(a.processCount);
      discardTotal += num(a.discardCount);

      const actionFps = actionPeriodMs > 0 ? (actionProcessPeriod * 1000) / actionPeriodMs : null;
      const actionName = String(a.name ?? '');
      const actionId = String(a.actionId ?? '');
      const primaryThroughputAction = isPrimaryThroughputAction(actionName, actionId, taskType);
      if (primaryAction == null && primaryThroughputAction) {
        primaryAction = a;
        primaryProcessTotal = num(a.processCount);
      }
      actionSummaries.push({
        actionId,
        name: actionName,
        fps: actionFps != null ? round(actionFps, 2) : null,
        insertPeriod: actionInsertPeriod,
        processPeriod: actionProcessPeriod,
        discardPeriod: actionDiscardPeriod,
        periodMs: actionPeriodMs,
      });

      if (actionFps != null && actionProcessPeriod > 0) {
        pipelineMinFps = Math.min(pipelineMinFps, actionFps);
        if (primaryFps == null && primaryThroughputAction) {
          primaryFps = actionFps;
        }
      }
      const actionDiscardDen = actionInsertPeriod + actionDiscardPeriod;
      if (actionDiscardDen > 0) {
        maxDiscardRate = Math.max(maxDiscardRate, actionDiscardPeriod / actionDiscardDen);
      }
    }

    const isVlm = normalizeTaskType(taskType) === 'vlm';
    if (isVlm && primaryAction) {
      const deltaFps = this._counterFps(
        `${st.taskId}:${primaryAction.actionId ?? primaryAction.name ?? 'vlm'}`,
        num(primaryAction.processCount),
        ts,
      );
      if (deltaFps != null) primaryFps = deltaFps;
    }
    if (primaryFps == null && !isVlm) {
      const firstEffective = actionSummaries.find((a) => a.fps != null && a.processPeriod > 0);
      primaryFps = firstEffective?.fps ?? null;
    }
    const measuredFps = primaryFps ?? (isVlm ? null : 0);
    const minPipelineFps = pipelineMinFps !== Infinity ? pipelineMinFps : 0;
    const discardRate = maxDiscardRate;
    const fpsRatio = targetFps && targetFps > 0 && measuredFps != null ? measuredFps / targetFps : null;

    return {
      channelId: st.channelId,
      taskId: st.taskId,
      algorithmName: st.algorithmName,
      algorithmVersion: st.algorithmVersion,
      actionCount: actions.length,
      measuredFps: measuredFps != null ? round(measuredFps, 2) : null,
      throughputFps: measuredFps != null ? round(measuredFps, 2) : null,
      telemetryMissing: isVlm && primaryAction == null,
      primaryProcessTotal,
      pipelineMinFps: round(minPipelineFps, 2),
      targetFps,
      fpsRatio: fpsRatio != null ? round(fpsRatio, 3) : null,
      discardRate: round(discardRate, 4),
      insertPeriod, processPeriod, discardPeriod, periodMs,
      insertTotal, processTotal, discardTotal,
      holdCount, alarmCount,
      actionSummaries,
      nodeDurationInfos: Array.isArray(st.nodeDurationInfos) ? st.nodeDurationInfos : [],
    };
  }

  _counterFps(key, count, ts) {
    const previous = this.counterSnapshots.get(key);
    this.counterSnapshots.set(key, { count, ts });
    if (!previous || ts <= previous.ts || count < previous.count) return null;
    return (count - previous.count) * 1000 / (ts - previous.ts);
  }

  // ── HardwareResource parsing ───────────────────────────────────────────

  _parseHardware(hwResult) {
    const hw = {};
    if (hwResult.status !== 'fulfilled') {
      hw._error = String(hwResult.reason?.message ?? hwResult.reason);
      return hw;
    }
    const itemList = hwResult.value?.itemList ?? [];
    const byKey = new Map(itemList.map((it) => [it.key, it]));
    for (const key of HW_KEYS) {
      const it = byKey.get(key);
      if (it) {
        hw[key] = { usedPercent: num(it.usedPercent), usedSize: it.usedSize, unusedSize: it.unusedSize };
      }
    }
    hw.customScore = hwResult.value?.customScore ?? null;
    hw.accelerator = normalizeAccelerator(hwResult.value?.accelerator);
    return hw;
  }
}

function normalizeAccelerator(value) {
  if (!value || typeof value !== 'object') return null;
  return Object.fromEntries(Object.entries(value).map(([key, item]) => {
    if (typeof item === 'number') return [key, Number.isFinite(item) ? item : null];
    return [key, item];
  }));
}

function num(v) {
  const n = Number(v);
  return Number.isFinite(n) ? n : 0;
}

function round(v, digits) {
  const f = 10 ** digits;
  return Math.round(v * f) / f;
}

function normalizeExpectedBindings(expectedBindings, legacyTargetFps) {
  if (expectedBindings instanceof Map) {
    return [...expectedBindings.entries()].map(([channelId, taskId]) => ({
      taskKey: 'default',
      taskDisplayName: 'default',
      taskType: 'cv',
      algorithmId: null,
      algorithmCode: null,
      targetFps: legacyTargetFps,
      channelId,
      taskId,
    }));
  }

  if (!Array.isArray(expectedBindings)) return [];
  return expectedBindings.map((entry) => ({
    taskKey: entry.taskKey ?? entry.taskId,
    taskDisplayName: entry.taskDisplayName ?? entry.taskKey ?? entry.taskId,
    taskType: entry.taskType ?? 'cv',
    algorithmId: entry.algorithmId ?? null,
    algorithmCode: entry.algorithmCode ?? null,
    targetFps: entry.targetFps ?? null,
    channelId: entry.channelId,
    taskId: entry.taskId,
  }));
}
