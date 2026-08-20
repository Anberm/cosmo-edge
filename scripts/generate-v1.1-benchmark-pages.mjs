import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(here, '..');
const defaultSourceRoot = path.join(repositoryRoot, 'docs', 'benchmarks', 'scenario-bench', 'v1.1');
const defaultOutputRoot = path.join(repositoryRoot, 'docs', '.vitepress', 'dist', 'benchmarks', 'scenario-bench', 'v1.1');

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const options = parseArgs(process.argv.slice(2));
  if (options.writeSourceChecksums) {
    const sourceRoot = path.resolve(options.sourceRoot ?? defaultSourceRoot);
    writeChecksums(sourceRoot);
    console.log(`Updated canonical source checksums in ${path.relative(repositoryRoot, sourceRoot)}.`);
    process.exit(0);
  }
  const result = generateBenchmarkPages(options);
  console.log(
    `Generated ${result.reportCount} benchmark reports for ${result.platformCount} platforms and ` +
    `${result.caseCount} canonical cases in ${path.relative(repositoryRoot, result.outputRoot) || '.'}.`,
  );
}

export function generateBenchmarkPages({ sourceRoot = defaultSourceRoot, outputRoot = defaultOutputRoot } = {}) {
  sourceRoot = path.resolve(sourceRoot);
  outputRoot = path.resolve(outputRoot);
  if (sourceRoot === outputRoot) throw new Error('benchmark pages must be generated outside the canonical source directory');
  if (!fs.existsSync(sourceRoot)) throw new Error(`canonical benchmark source is missing: ${sourceRoot}`);

  const manifest = readJson(path.join(sourceRoot, 'release-manifest.json'));
  const platformDefs = Array.isArray(manifest.platforms) ? manifest.platforms : [];
  const platformIds = platformDefs.map((definition) => definition.id);
  fs.mkdirSync(outputRoot, { recursive: true });
  copyCanonicalAssets(sourceRoot, outputRoot, platformIds);

  const platforms = platformDefs.map((definition) => loadPlatform(sourceRoot, definition));
  const vlm = readJson(path.join(sourceRoot, 'results', 'vlm-observations.json'));
  const vlmByPlatform = new Map(vlm.observations.map((item) => [item.platformId, item]));
  const caseCount = platforms.reduce((count, item) => count + item.cases.length, 0);
  let reportCount = 0;

  removeGeneratedPages(outputRoot, platforms.map((item) => item.id));
  writeDerivedIndexes(outputRoot, manifest, platforms, vlm);

  for (const locale of ['en', 'zh-CN']) {
    const suffix = locale === 'zh-CN' ? '.zh-CN.html' : '.html';
    writeReport(outputRoot, `report${suffix}`, renderRootReport(locale, manifest, platforms, vlmByPlatform));
    reportCount += 1;

    for (const platform of platforms) {
      writeReport(outputRoot, `results/${platform.id}/report${suffix}`, renderPlatformReport(locale, manifest, platform, vlmByPlatform.get(platform.id)));
      writeReport(outputRoot, `results/${platform.id}/cases/report${suffix}`, renderCaseIndex(locale, platform));
      writeReport(outputRoot, `results/${platform.id}/single-detector/report${suffix}`, renderSingleDetectorReport(locale, manifest, platform));
      writeReport(outputRoot, `results/${platform.id}/dual-detector/report${suffix}`, renderDualDetectorReport(locale, platform));
      reportCount += 4;

      const observation = vlmByPlatform.get(platform.id);
      if (observation) {
        writeReport(outputRoot, `results/${platform.id}/vlm-observation/report${suffix}`, renderVlmReport(locale, platform, observation));
        reportCount += 1;
      }

      for (const benchmarkCase of platform.cases) {
        writeReport(
          outputRoot,
          `results/${platform.id}/cases/${benchmarkCase.caseId}/report${suffix}`,
          renderCaseReport(locale, platform, benchmarkCase),
        );
        reportCount += 1;
      }
    }
  }

  writeChecksums(outputRoot);
  return { outputRoot, reportCount, platformCount: platforms.length, caseCount };
}

function loadPlatform(sourceRoot, definition) {
  const id = definition.id;
  const canonical = readJson(path.join(sourceRoot, 'results', id, 'cases.json'));
  const environment = readJson(path.join(sourceRoot, 'environments', `${id}.json`));
  return {
    id,
    name: canonical.platform,
    scope: canonical.scope,
    cases: canonical.cases,
    gates: canonical.gates,
    environment,
  };
}

function writeDerivedIndexes(outputRoot, manifest, platforms, vlm) {
  const resultsRoot = path.join(outputRoot, 'results');
  const vlmPlatformIds = new Set(vlm.observations.map((item) => item.platformId));
  writeJson(path.join(resultsRoot, 'index.json'), {
    schemaVersion: 3,
    benchmark: 'CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark',
    publicationStatus: manifest.release.publicationState,
    manifest: '../release-manifest.json',
    generatedAt: manifest.release.frozenAt,
    primaryClaim: 'observed short-run local-loop capacity boundary',
    caseCount: platforms.reduce((count, platform) => count + platform.cases.length, 0),
    platforms: platforms.map((platform) => ({
      platformId: platform.id,
      platform: platform.name,
      scope: platform.scope,
      environment: `../environments/${platform.id}.json`,
      models: `../models/${platform.id}.json`,
      cases: `${platform.id}/cases.json`,
      report: `${platform.id}/report.html`,
      reportZhCn: `${platform.id}/report.zh-CN.html`,
      vlmObservation: vlmPlatformIds.has(platform.id) ? 'vlm-observations.json' : null,
    })),
  });

  const globalCases = [];
  for (const platform of platforms) {
    const cases = platform.cases.map((item) => ({
      platformId: platform.id,
      platform: platform.name,
      caseId: item.caseId,
      workload: item.workload,
      targetFps: item.targetFps,
      configuredChannels: item.configuredChannels,
      outcome: item.outcome,
      boundaryKind: item.boundaryKind,
      lastPassingChannels: item.lastPassingChannels,
      canonical: `${platform.id}/cases.json`,
      report: `${platform.id}/cases/${item.caseId}/report.html`,
      reportZhCn: `${platform.id}/cases/${item.caseId}/report.zh-CN.html`,
    }));
    globalCases.push(...cases);
    writeJson(path.join(resultsRoot, platform.id, 'cases', 'index.json'), {
      schemaVersion: 2,
      platformId: platform.id,
      caseCount: cases.length,
      canonical: '../cases.json',
      cases: cases.map(({
        platformId: unusedId,
        platform: unusedName,
        canonical: unusedCanonical,
        report: unusedReport,
        reportZhCn: unusedReportZhCn,
        ...item
      }) => ({
        ...item,
        report: `${item.caseId}/report.html`,
        reportZhCn: `${item.caseId}/report.zh-CN.html`,
      })),
    });
  }
  writeJson(path.join(resultsRoot, 'cases.json'), {
    schemaVersion: 2,
    benchmark: 'CosmoEdge 1.1 small-model capacity benchmark',
    caseCount: globalCases.length,
    cases: globalCases,
  });

  writeJson(path.join(resultsRoot, 'workload-matrix.json'), {
    schemaVersion: 3,
    publicationStatus: manifest.release.publicationState,
    interpretation: {
      singleDetector: 'last passing short-run channel count under enabled CV gates',
      dualDetector: 'highest configured short-run point passed; observed lower bound only',
      vlm: 'Experimental runtime observation; analysis FPS is not a gate',
      bindingBlocked: 'the next configured channel was blocked during task binding before measurement',
      storageBlocked: 'the expansion run was blocked before measurement by its storage precondition',
    },
    platforms: platforms.map((platform) => ({
      platformId: platform.id,
      platform: platform.name,
      scope: platform.scope,
      singleDetector: platform.cases.filter((item) => item.workload !== 'dual-detector').map(matrixEntry),
      dualDetector: platform.cases.filter((item) => item.workload === 'dual-detector').map(matrixEntry),
    })),
    vlmEvidenceStatus: vlm.evidenceStatus,
  });
}

function renderRootReport(locale, manifest, platforms, vlmByPlatform) {
  const zh = locale === 'zh-CN';
  const targetFpsValues = manifest.controls.smallModelTargetFps;
  const official = platforms.filter((item) => item.scope === 'release-platform');
  const experimental = platforms.filter((item) => item.scope !== 'release-platform');
  const dualRows = platforms.map((platform) => {
    const item = findWorkloadCase(platform, 'dual-detector');
    return [platformLabel(platform, locale), displayBoundary(item), `${item.configuredChannels * 2}/${item.configuredChannels * 2}`, '30 s'];
  });
  const singleRows = official.flatMap((platform) => ['person-detector', 'safety-helmet-detector'].map((workload) => [
    platform.name,
    workloadLabel(workload, locale),
    ...targetFpsValues.map((fps) => displayBoundary(findCase(platform, workload, fps))),
  ]));
  const experimentalRows = experimental.flatMap((platform) => ['person-detector', 'safety-helmet-detector'].map((workload) => [
    platform.name,
    workloadLabel(workload, locale),
    ...targetFpsValues.map((fps) => displayBoundary(findCase(platform, workload, fps))),
  ]));
  const vlmRows = official.flatMap((platform) => {
    const item = vlmByPlatform.get(platform.id);
    if (!item) return [];
    const lastPassingStep = item.steps.find(
      (step) => step.channels === item.observedBoundary.highestNonFpsPassingChannels,
    );
    return [[
      platform.name,
      '0.1',
      value(item.observedBoundary.highestNonFpsPassingChannels),
      value(lastPassingStep?.observedEquivalentPerChannelFps),
      value(item.observedBoundary.firstNonFpsStopChannels),
    ]];
  });
  const environmentRows = official.map((platform) => [
    platform.name,
    platform.environment.deviceDescription,
    platform.environment.os,
    `${platform.environment.runtime.inference}; ${platform.environment.runtime.media}`,
    platform.environment.cosmoEdgeInstalledVersion,
  ]);
  const linksRows = platforms.map((platform) => [
    platformLabel(platform, locale),
    link(`results/${platform.id}/report${zh ? '.zh-CN' : ''}.html`, zh ? '平台汇总' : 'Platform overview'),
    link(`results/${platform.id}/cases/report${zh ? '.zh-CN' : ''}.html`, `${platform.cases.length} ${zh ? '个用例' : 'cases'}`),
    link(`results/${platform.id}/single-detector/report${zh ? '.zh-CN' : ''}.html`, zh ? '单任务' : 'Single-task'),
    link(`results/${platform.id}/dual-detector/report${zh ? '.zh-CN' : ''}.html`, zh ? '并发任务' : 'Concurrent'),
    platform.id === 'rv1126b' ? '—' : link(`results/${platform.id}/vlm-observation/report${zh ? '.zh-CN' : ''}.html`, 'VLM'),
  ]);

  const body = [
    `<h1>${zh ? 'CosmoEdge 1.1 多平台视频分析容量基准' : 'CosmoEdge 1.1 Multi-Platform Video Analytics Benchmark'}</h1>`,
    `<p class="lead">BM1688 · CV186X · RK3576${experimental.length ? ' · RV1126B Experimental' : ''}</p>`,
    notice(zh
      ? '本报告记录30秒本地循环输入下的短时容量边界，不是长稳、RTSP 韧性或生产推荐配置结论。'
      : 'This report records 30-second local-loop capacity boundaries. It is not long-run, RTSP-resilience, or recommended production-profile qualification.'),
    `<h2>${zh ? '双任务并发观测' : 'Concurrent dual-detector observations'}</h2>`,
    table(
      zh ? ['平台', '通过路数', '任务绑定', '单级时长'] : ['Platform', 'Passing channels', 'Task bindings', 'Hold / step'],
      dualRows,
    ),
    `<h2>${zh ? '单任务容量矩阵' : 'Single-task capacity matrix'}</h2>`,
    `<p>${boundaryLegend(locale)}</p>`,
    table(
      [zh ? '平台' : 'Platform', zh ? '任务' : 'Workload', ...targetFpsValues.map((fps) => `${fps} FPS`)],
      singleRows,
    ),
    `<img src="assets/capacity-overview${zh ? '.zh-CN' : ''}.svg" alt="${zh ? '容量概览图' : 'Capacity overview'}">`,
    `<img src="assets/throughput-curves${zh ? '.zh-CN' : ''}.svg" alt="${zh ? '吞吐与延时曲线' : 'Throughput and latency curves'}">`,
    `<img src="assets/resource-peaks${zh ? '.zh-CN' : ''}.svg" alt="${zh ? '资源峰值图' : 'Resource peaks'}">`,
  ];

  if (experimentalRows.length) {
    body.push(
      `<h2>${zh ? '附加实验平台' : 'Additional experimental platform'}</h2>`,
      table(
        [zh ? '平台' : 'Platform', zh ? '任务' : 'Workload', ...targetFpsValues.map((fps) => `${fps} FPS`)],
        experimentalRows,
      ),
    );
  }

  body.push(
    `<h2>${zh ? 'VLM 实验运行观测' : 'Experimental VLM runtime observations'}</h2>`,
    notice(zh
      ? 'VLM 保留的是上一批独立证据；FPS 只记录、不参与 PASS/FAIL，不能作为容量结论。'
      : 'The retained VLM observations are preceding evidence. FPS is recorded but excluded from PASS/FAIL and cannot support a capacity claim.', 'experimental'),
    table(zh ? ['平台', '目标 FPS/路', '最后非 FPS 通过路数', '最后通过级等效 FPS/路', '非 FPS 停止路数'] : ['Platform', 'Target FPS/ch', 'Last non-FPS pass', 'Last-passing equivalent FPS/ch', 'Non-FPS stop'], vlmRows),
    `<h2>${zh ? '证据入口' : 'Evidence entry points'}</h2>`,
    table(zh ? ['平台', '平台报告', '用例', '单任务', '并发任务', 'VLM'] : ['Platform', 'Overview', 'Cases', 'Single-task', 'Concurrent', 'VLM'], linksRows),
    `<h2>${zh ? '测试环境' : 'Test environment'}</h2>`,
    table(zh ? ['平台', '设备', '操作系统', '运行时 / 媒体', 'CosmoEdge'] : ['Platform', 'Device', 'OS', 'Runtime / media', 'CosmoEdge'], environmentRows),
    `<h2>${zh ? '方法与复现' : 'Method and reproduction'}</h2>`,
    `<ul><li>${zh ? '源码' : 'Source'}: <code>${escapeHtml(manifest.sourceBaseline.commit)}</code></li>` +
      `<li>${zh ? '受控输入 SHA-256' : 'Controlled input SHA-256'}: <code>${escapeHtml(manifest.dataset.sha256)}</code></li>` +
      `<li>${zh ? '四份 canonical case 数据是唯一机器可读事实源；HTML、索引和矩阵由构建生成。' : 'Four canonical case datasets are the only machine-readable source of truth; HTML, indexes, and matrices are generated at build time.'}</li></ul>`,
  );

  return page(locale, zh ? 'CosmoEdge 1.1 多平台容量基准' : 'CosmoEdge 1.1 Multi-Platform Benchmark', rootNav(locale), body.join(''));
}

function renderPlatformReport(locale, manifest, platform, observation) {
  const zh = locale === 'zh-CN';
  const targetFpsValues = manifest.controls.smallModelTargetFps;
  const singleRows = ['person-detector', 'safety-helmet-detector'].map((workload) => [
    workloadLabel(workload, locale),
    ...targetFpsValues.map((fps) => displayBoundary(findCase(platform, workload, fps))),
  ]);
  const dual = findWorkloadCase(platform, 'dual-detector');
  const body = [
    `<h1>${escapeHtml(platform.name)} · ${zh ? '短时容量概览' : 'Short-run capacity overview'}</h1>`,
    notice(zh
      ? '所有数值均绑定本报告的受控本地循环输入、30秒单级窗口和禁用预览条件。'
      : 'All values are bound to the controlled local-loop input, 30-second step window, and preview-disabled conditions in this report.'),
    `<h2>${zh ? '并发任务' : 'Concurrent workload'}</h2>`,
    table(zh ? ['工作负载', '目标 FPS/任务', '通过边界', '设定上限'] : ['Workload', 'Target FPS/task', 'Observed boundary', 'Configured maximum'], [[workloadLabel(dual.workload, locale), dual.targetFps, displayBoundary(dual), dual.configuredChannels]]),
    `<h2>${zh ? '单任务' : 'Single-task workloads'}</h2>`,
    `<p>${boundaryLegend(locale)}</p>`,
    table([zh ? '任务' : 'Workload', ...targetFpsValues.map((fps) => `${fps} FPS`)], singleRows),
    `<h2>${zh ? '环境' : 'Environment'}</h2>`,
    table(zh ? ['设备', '架构', '操作系统', '推理运行时', '媒体链路', 'CosmoEdge'] : ['Device', 'Architecture', 'OS', 'Inference runtime', 'Media path', 'CosmoEdge'], [[
      platform.environment.deviceDescription,
      platform.environment.architecture,
      platform.environment.os,
      platform.environment.runtime.inference,
      platform.environment.runtime.media,
      platform.environment.cosmoEdgeInstalledVersion,
    ]]),
    `<h2>${zh ? '详细结果' : 'Detailed results'}</h2>`,
    `<ul><li>${anchor(`cases/report${zh ? '.zh-CN' : ''}.html`, `${platform.cases.length} ${zh ? '个用例' : 'cases'}`)}</li>` +
      `<li>${anchor(`single-detector/report${zh ? '.zh-CN' : ''}.html`, zh ? '单任务汇总' : 'Single-task summary')}</li>` +
      `<li>${anchor(`dual-detector/report${zh ? '.zh-CN' : ''}.html`, zh ? '并发任务汇总' : 'Concurrent summary')}</li>` +
      (observation ? `<li>${anchor(`vlm-observation/report${zh ? '.zh-CN' : ''}.html`, zh ? 'VLM 实验观测' : 'Experimental VLM observation')}</li>` : '') +
      `</ul>`,
    `<p>${zh ? '测试源码' : 'Test source'}: <code>${escapeHtml(manifest.sourceBaseline.commit)}</code></p>`,
  ];
  return page(locale, `${platform.name} ${zh ? '容量概览' : 'capacity overview'}`, platformNav(locale), body.join(''));
}

function renderCaseIndex(locale, platform) {
  const zh = locale === 'zh-CN';
  const rows = platform.cases.map((item) => [
    link(`${item.caseId}/report${zh ? '.zh-CN' : ''}.html`, caseLabel(item, locale)),
    workloadLabel(item.workload, locale),
    item.targetFps,
    item.configuredChannels,
    displayBoundary(item),
    boundaryKindLabel(item.boundaryKind, locale),
  ]);
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '受控用例' : 'Controlled cases'}</h1>` +
    notice(zh
      ? '这些页面由单一 canonical JSON 确定性生成，不是额外的数据副本。'
      : 'These pages are generated deterministically from one canonical JSON file; they are not additional data copies.') +
    table(zh ? ['用例', '任务', '目标 FPS', '设定路数', '观测边界', '边界类型'] : ['Case', 'Workload', 'Target FPS', 'Configured channels', 'Observed boundary', 'Boundary type'], rows);
  return page(locale, `${platform.name} ${zh ? '用例' : 'cases'}`, caseIndexNav(locale), body);
}

function renderSingleDetectorReport(locale, manifest, platform) {
  const zh = locale === 'zh-CN';
  const sections = [];
  for (const workload of ['person-detector', 'safety-helmet-detector']) {
    sections.push(`<h2>${escapeHtml(workloadLabel(workload, locale))}</h2>`);
    for (const fps of manifest.controls.smallModelTargetFps) {
      const item = findCase(platform, workload, fps);
      if (!item) continue;
      sections.push(`<h3>${fps} FPS · ${escapeHtml(displayBoundary(item))}</h3>`, renderStepTable(locale, item));
    }
  }
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '单任务短时容量' : 'Single-task short-run capacity'}</h1>` +
    notice(zh ? '结果是短时容量边界，不是生产推荐路数。' : 'Results are short-run capacity boundaries, not recommended production channel counts.') +
    sections.join('');
  return page(locale, `${platform.name} ${zh ? '单任务容量' : 'single-task capacity'}`, workloadNav(locale), body);
}

function renderDualDetectorReport(locale, platform) {
  const zh = locale === 'zh-CN';
  const item = findWorkloadCase(platform, 'dual-detector');
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? '双任务并发观测' : 'Concurrent dual-detector observation'}</h1>` +
    notice(zh
      ? `每路同时运行人员检测与未佩戴安全帽分析，每任务 ${item.targetFps} FPS。`
      : `Each channel runs person detection and no-safety-helmet analysis concurrently at ${item.targetFps} FPS per task.`) +
    renderStepTable(locale, item);
  return page(locale, `${platform.name} ${zh ? '并发任务' : 'concurrent workload'}`, workloadNav(locale), body);
}

function renderCaseReport(locale, platform, item) {
  const zh = locale === 'zh-CN';
  const body = `<h1>${escapeHtml(platform.name)} · ${escapeHtml(caseLabel(item, locale))}</h1>` +
    `<p class="lead">${item.configuredChannels} ${zh ? '路设定' : 'configured channels'} · ${item.targetFps} FPS · 30 s/${zh ? '级' : 'step'}</p>` +
    notice(caseNotice(item, locale), item.boundaryKind === 'lower-bound' ? '' : 'experimental') +
    renderStepTable(locale, item) +
    `<p>${anchor('../../cases.json', zh ? '查看 canonical JSON' : 'Open canonical JSON')}</p>`;
  return page(locale, `${platform.name} ${caseLabel(item, locale)}`, individualCaseNav(locale), body);
}

function renderVlmReport(locale, platform, observation) {
  const zh = locale === 'zh-CN';
  const rows = observation.steps.map((step) => [
    step.channels,
    `${step.holdSeconds} s`,
    step.targetFpsPerChannel,
    step.observedEquivalentPerChannelFps,
    percent(step.averageDiscardRate),
    percentWhole(step.acceleratorPeakPercent),
    percentWhole(step.cpuPeakPercent),
    percentWhole(step.memoryPeakPercent),
    status(step.nonFpsGateResult),
    value(step.stopReason),
  ]);
  const body = `<h1>${escapeHtml(platform.name)} · ${zh ? 'VLM 实验运行观测' : 'Experimental VLM runtime observation'}</h1>` +
    notice(zh
      ? '这是本轮小模型刷新之前的保留证据。FPS 门禁已禁用，因此不支持容量结论。'
      : 'This is retained evidence from before the small-model refresh. The FPS gate was disabled, so it does not support a capacity claim.', 'experimental') +
    table(zh ? ['路数', '时长', '目标 FPS/路', '等效 FPS/路', '平均丢弃', '加速器', 'CPU', '内存', '非 FPS 门禁', '停止原因'] : ['Channels', 'Hold', 'Target FPS/ch', 'Equivalent FPS/ch', 'Avg discard', 'Accelerator', 'CPU', 'Memory', 'Non-FPS gate', 'Stop reason'], rows);
  return page(locale, `${platform.name} VLM`, workloadNav(locale), body);
}

function renderStepTable(locale, item) {
  const zh = locale === 'zh-CN';
  const dual = item.workload === 'dual-detector';
  const rows = item.steps.map((step) => {
    const base = [step.channels, `${step.holdSeconds} s`];
    if (dual) {
      base.push(value(step.tasks.find((task) => task.name === 'person-detector')?.minimumProcessingFps));
      base.push(value(step.tasks.find((task) => task.name === 'safety-helmet-detector')?.minimumProcessingFps));
    } else {
      base.push(value(step.minimumProcessingFps));
    }
    base.push(
      value(step.maximumCriticalPathLatencyMs),
      percent(step.averageDiscardRate),
      percentWhole(step.acceleratorPeakPercent),
      percentWhole(step.cpuPeakPercent),
      percentWhole(step.memoryPeakPercent),
      status(step.result),
      value(step.failureReason),
    );
    return base;
  });
  const headers = dual
    ? (zh ? ['路数', '时长', '人员检测最低 FPS', '安全帽最低 FPS', '关键路径 ms', '平均丢弃', '加速器', 'CPU', '内存', '结果', '原因'] : ['Channels', 'Hold', 'Person min FPS', 'Helmet min FPS', 'Critical path ms', 'Avg discard', 'Accelerator', 'CPU', 'Memory', 'Result', 'Reason'])
    : (zh ? ['路数', '时长', '最低 FPS', '关键路径 ms', '平均丢弃', '加速器', 'CPU', '内存', '结果', '原因'] : ['Channels', 'Hold', 'Minimum FPS', 'Critical path ms', 'Avg discard', 'Accelerator', 'CPU', 'Memory', 'Result', 'Reason']);
  return table(headers, rows);
}

function page(locale, title, nav, body) {
  return `<!doctype html>
<html lang="${locale}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>${escapeHtml(title)}</title>
  <style>${styles()}</style>
</head>
<body><main>${nav}${body}</main></body>
</html>
`;
}

function rootNav(locale) {
  return `<nav class="report-nav" aria-label="Report navigation"><span>CosmoEdge 1.1</span>${anchor(locale === 'zh-CN' ? 'report.html' : 'report.zh-CN.html', locale === 'zh-CN' ? 'English' : '中文')}</nav>`;
}

function platformNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function caseIndexNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function workloadNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function individualCaseNav(locale) {
  const zh = locale === 'zh-CN';
  return `<nav class="report-nav" aria-label="Report navigation">${anchor(`../../../../report${zh ? '.zh-CN' : ''}.html`, zh ? '多平台报告' : 'Multi-platform report')}${anchor(`../../report${zh ? '.zh-CN' : ''}.html`, zh ? '平台概览' : 'Platform overview')}${anchor(`../report${zh ? '.zh-CN' : ''}.html`, zh ? '用例索引' : 'Case index')}${anchor(`report${zh ? '' : '.zh-CN'}.html`, zh ? 'English' : '中文')}</nav>`;
}

function table(headers, rows) {
  return `<div class="table" tabindex="0" role="region" aria-label="Scrollable data table"><table><thead><tr>${headers.map((item) => `<th>${escapeHtml(item)}</th>`).join('')}</tr></thead><tbody>${rows.map((row) => `<tr>${row.map((item) => `<td${statusAttribute(item)}>${renderCell(item)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
}

function statusAttribute(item) {
  return item === 'PASS' || item === '通过' ? ' data-status="PASS"' : item === 'STOP' || item === 'FAIL' || item === '停止' || item === '失败' ? ' data-status="FAIL"' : '';
}

function renderCell(item) {
  if (item && typeof item === 'object' && item.__html) return item.__html;
  return escapeHtml(value(item));
}

function link(href, text) {
  return { __html: anchor(href, text) };
}

function anchor(href, text) {
  return `<a href="${escapeHtml(href)}">${escapeHtml(text)}</a>`;
}

function notice(text, extraClass = '') {
  return `<p class="notice${extraClass ? ` ${extraClass}` : ''}">${escapeHtml(text)}</p>`;
}

function findCase(platform, workload, fps) {
  return selectPreferredCase(platform.cases.filter((item) => item.workload === workload && item.targetFps === fps));
}

function findWorkloadCase(platform, workload) {
  return selectPreferredCase(platform.cases.filter((item) => item.workload === workload));
}

function selectPreferredCase(cases) {
  return cases.reduce((selected, item) => (
      !selected || item.configuredChannels > selected.configuredChannels ? item : selected
  ), null);
}

function matrixEntry(value) {
  return {
    caseId: value.caseId,
    workload: value.workload,
    targetFps: value.targetFps,
    configuredChannels: value.configuredChannels,
    outcome: value.outcome,
    boundaryKind: value.boundaryKind,
    lastPassingChannels: value.lastPassingChannels,
    firstBlockedChannels: value.firstBlockedChannels,
    display: displayBoundary(value),
    blockedReason: value.blockedReason,
  };
}

function displayBoundary(item) {
  if (!item) return '—';
  if (item.boundaryKind === 'lower-bound') return `≥${item.lastPassingChannels}`;
  if (item.boundaryKind === 'binding-blocked') return `≥${item.lastPassingChannels}*`;
  if (item.boundaryKind === 'storage-blocked') return `≥${item.lastPassingChannels}†`;
  return String(item.lastPassingChannels);
}

function boundaryLegend(locale) {
  return locale === 'zh-CN'
    ? '数字是最后通过路数；≥ 表示设定上限仍通过，* 表示下一路绑定阻断，† 表示扩容在测量前被存储前置条件阻断。'
    : 'Values are last-passing channels; ≥ means the configured maximum still passed, * means the next binding was blocked, and † means expansion was blocked before measurement by storage preconditions.';
}

function platformLabel(platform, locale) {
  return platform.scope === 'release-platform' ? platform.name : `${platform.name} (${locale === 'zh-CN' ? '实验' : 'experimental'})`;
}

function workloadLabel(workload, locale) {
  const zh = locale === 'zh-CN';
  if (workload === 'person-detector') return zh ? '人员检测' : 'Person detector';
  if (workload === 'safety-helmet-detector') return zh ? '未佩戴安全帽分析' : 'No-safety-helmet analysis';
  return zh ? '人员检测 + 安全帽分析' : 'Person + no-safety-helmet';
}

function caseLabel(item, locale) {
  return `${workloadLabel(item.workload, locale)} ${item.targetFps} FPS × ${item.configuredChannels}`;
}

function boundaryKindLabel(kind, locale) {
  const zh = locale === 'zh-CN';
  const labels = {
    'lower-bound': zh ? '下界' : 'lower bound',
    'binding-blocked': zh ? '绑定阻断' : 'binding blocked',
    'storage-blocked': zh ? '存储前置阻断' : 'storage blocked',
    'performance-stop': zh ? '性能停止' : 'performance stop',
  };
  return labels[kind] ?? kind;
}

function caseNotice(item, locale) {
  const zh = locale === 'zh-CN';
  if (item.boundaryKind === 'lower-bound') return zh ? `设定的 ${item.configuredChannels} 路全部通过；这是实测下界，不是极限或推荐值。` : `All ${item.configuredChannels} configured channels passed; this is an observed lower bound, not a maximum or recommendation.`;
  if (item.boundaryKind === 'binding-blocked') return zh ? `${item.lastPassingChannels} 路完成测量；第 ${item.firstBlockedChannels} 路在绑定时被阻断。` : `${item.lastPassingChannels} channels completed measurement; channel ${item.firstBlockedChannels} was blocked during binding.`;
  if (item.boundaryKind === 'storage-blocked') return zh ? `${item.lastPassingChannels} 路完成测量；后续扩容在测量前被存储前置条件阻断。` : `${item.lastPassingChannels} channels completed measurement; further expansion was blocked before measurement by storage preconditions.`;
  return zh ? `最后通过 ${item.lastPassingChannels} 路；${item.blockedReason ?? '后续步骤触发性能停止'}。` : `Last passing point: ${item.lastPassingChannels} channels; ${item.blockedReason ?? 'the following step triggered a performance stop'}.`;
}

function value(input) {
  return input === null || input === undefined || input === '' ? '—' : String(input);
}

function percent(input) {
  return input === null || input === undefined ? '—' : `${Number((input * 100).toFixed(2))}%`;
}

function percentWhole(input) {
  return input === null || input === undefined ? '—' : `${input}%`;
}

function status(input) {
  if (input === null || input === undefined) return '—';
  return String(input);
}

function escapeHtml(input) {
  return value(input).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
}

function styles() {
  return ':root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#f7f9fc;color:#172033;font:15px/1.65,Inter,"Segoe UI",Arial,sans-serif}main{max-width:1120px;margin:auto;padding:42px 24px 70px;min-width:0}h1{font-size:32px;line-height:1.25;margin:0 0 8px;letter-spacing:-.02em}h2{margin-top:38px;line-height:1.35}h3{margin-top:28px}.lead{color:#526071}.notice{background:#eef4ff;border-left:4px solid #2563eb;padding:14px 16px;border-radius:0 8px 8px 0}.experimental{background:#fff7e8;border-left-color:#d97706}.report-nav{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:0 0 22px}.report-nav a,.report-nav span{display:inline-flex;align-items:center;min-height:34px;padding:5px 11px;border:1px solid #cbd5e1;border-radius:999px;background:#fff;color:#1d4ed8;text-decoration:none}.report-nav span{color:#475569;background:#f8fafc}.table{max-width:100%;overflow-x:auto;overscroll-behavior-inline:contain;-webkit-overflow-scrolling:touch;border:1px solid #dce3ed;border-radius:8px;margin:12px 0 24px;background:#fff}.table:focus{outline:2px solid #93c5fd;outline-offset:2px}table{border-collapse:collapse;width:100%;background:#fff}th,td{padding:10px 12px;border-bottom:1px solid #dce3ed;text-align:left;vertical-align:top}th{background:#f1f5f9;white-space:nowrap}tr:last-child td{border-bottom:0}td[data-status="PASS"]{color:#047857;font-weight:700}td[data-status="FAIL"]{color:#b91c1c;font-weight:700}img{display:block;max-width:100%;height:auto;margin:24px auto;background:#fff;border:1px solid #e2e8f0;border-radius:10px}code{background:#eef2f7;padding:2px 5px;border-radius:4px}a{color:#1d4ed8}@media(max-width:600px){main{padding:26px 15px 52px}h1{font-size:27px}h2{font-size:22px;margin-top:32px}.table{margin-right:0}.table table{width:max-content;min-width:100%;max-width:none}th,td{padding:9px 11px;min-width:76px}img{margin:18px auto}.report-nav{gap:7px}.report-nav a,.report-nav span{font-size:13px;min-height:32px;padding:4px 9px}}';
}

function copyCanonicalAssets(sourceRoot, outputRoot, platformIds) {
  const canonicalCaseFiles = new Set(platformIds.map((id) => `results/${id}/cases.json`));
  for (const file of walk(sourceRoot)) {
    const relative = path.relative(sourceRoot, file).replaceAll('\\', '/');
    if (!canonicalStaticAsset(relative, canonicalCaseFiles)) continue;
    const target = path.join(outputRoot, ...relative.split('/'));
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(file, target);
  }
}

function canonicalStaticAsset(relative, canonicalCaseFiles) {
  if (relative === 'SHA256SUMS' || /^report(?:\.zh-CN)?\.html$/.test(relative)) return false;
  if (!relative.startsWith('results/')) return true;
  return canonicalCaseFiles.has(relative) || relative === 'results/cases.schema.json' || relative === 'results/vlm-observations.json';
}

function removeGeneratedPages(outputRoot, platformIds) {
  for (const relative of ['report.html', 'report.zh-CN.html', 'results/cases.json', 'results/index.json', 'results/workload-matrix.json']) {
    removePath(path.join(outputRoot, ...relative.split('/')));
  }
  for (const platform of platformIds) {
    for (const relative of [
      `results/${platform}/report.html`,
      `results/${platform}/report.zh-CN.html`,
      `results/${platform}/command.txt`,
      `results/${platform}/environment.json`,
      `results/${platform}/metrics.json`,
      `results/${platform}/summary.json`,
      `results/${platform}/test.log`,
      `results/${platform}/cases`,
      `results/${platform}/single-detector`,
      `results/${platform}/dual-detector`,
      `results/${platform}/vlm-observation`,
    ]) removePath(path.join(outputRoot, ...relative.split('/')));
  }
  removePath(path.join(outputRoot, 'SHA256SUMS'));
}

function removePath(target) {
  if (!fs.existsSync(target)) return;
  fs.rmSync(target, { recursive: true, force: false });
}

function writeReport(outputRoot, relative, html) {
  const file = path.join(outputRoot, ...relative.split('/'));
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, html, 'utf8');
}

export function writeChecksums(outputRoot) {
  const checksumPath = path.join(outputRoot, 'SHA256SUMS');
  const lines = walk(outputRoot)
    .filter((file) => path.resolve(file) !== path.resolve(checksumPath))
    .map((file) => {
      const relative = path.relative(outputRoot, file).replaceAll('\\', '/');
      const digest = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
      return `${digest}  ${relative}`;
    })
    .sort();
  fs.writeFileSync(checksumPath, `${lines.join('\n')}\n`, 'utf8');
}

function writeJson(file, value) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`, 'utf8');
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function walk(directory) {
  if (!fs.existsSync(directory)) return [];
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}

function parseArgs(args) {
  const options = {};
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--source') options.sourceRoot = requireValue(args, ++index, argument);
    else if (argument === '--output') options.outputRoot = requireValue(args, ++index, argument);
    else if (argument === '--write-source-checksums') options.writeSourceChecksums = true;
    else throw new Error(`unknown argument: ${argument}`);
  }
  return options;
}

function requireValue(args, index, option) {
  if (!args[index]) throw new Error(`${option} requires a value`);
  return args[index];
}
