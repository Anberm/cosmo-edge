import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { generateBenchmarkPages } from './generate-v1.1-benchmark-pages.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const workspace = path.resolve(here, '..');
const root = path.join(workspace, 'docs', 'benchmarks', 'scenario-bench', 'v1.1');
const errors = [];
const archivePath = parseArchiveArgument(process.argv.slice(2));

const requiredSourceFiles = [
  'README.md',
  'README.zh-CN.md',
  'RELEASE-CHECKLIST.md',
  'LICENSES.md',
  'methodology.md',
  'release-manifest.json',
  'SHA256SUMS',
  'results/cases.schema.json',
  'results/vlm-observations.json',
];

if (!fs.existsSync(root)) {
  fail(`benchmark source root is missing: ${root}`);
} else {
  for (const relativePath of requiredSourceFiles) requireFile(root, relativePath);
}

const manifest = readJsonIfPresent(root, 'release-manifest.json');
const platformDefinitions = validateManifest(manifest);
const canonicalPlatforms = validateCanonicalCases(manifest, platformDefinitions);
const vlm = validateVlmEvidence(manifest, platformDefinitions);
if (archivePath) validateArchivedEvidence(archivePath, manifest, canonicalPlatforms);
validateCanonicalSourceLayout(platformDefinitions);
validateSchemaDocument();

const sourceFiles = fs.existsSync(root) ? walk(root) : [];
for (const file of sourceFiles.filter((entry) => entry.toLowerCase().endsWith('.json'))) {
  const value = readJsonWithError(file);
  if (value !== null) validateJsonReferences(root, file, value, null);
}

let generatedRoot = null;
let generation = null;
try {
  generatedRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'cosmoedge-v1.1-validate-'));
  generation = generateBenchmarkPages({ sourceRoot: root, outputRoot: generatedRoot });
  validateGeneratedPack(generatedRoot, manifest, canonicalPlatforms, vlm, generation);
} catch (error) {
  fail(`deterministic report generation failed: ${error.message}`);
}

validatePublicScrub(sourceFiles, root, 'source');
validateChecksums(root, 'source SHA256SUMS');
validateLinksAndLanguages(root, sourceFiles, generatedRoot);

if (generatedRoot && fs.existsSync(generatedRoot)) {
  const generatedFiles = walk(generatedRoot);
  for (const file of generatedFiles.filter((entry) => entry.toLowerCase().endsWith('.json'))) {
    const value = readJsonWithError(file);
    if (value !== null) validateJsonReferences(generatedRoot, file, value, null);
  }
  validatePublicScrub(generatedFiles, generatedRoot, 'generated pack');
  validateChecksums(generatedRoot, 'generated SHA256SUMS');
  validateLinksAndLanguages(generatedRoot, generatedFiles, null);
  fs.rmSync(generatedRoot, { recursive: true, force: true });
}

if (errors.length) {
  console.error(`Validation failed (${errors.length}):`);
  for (const error of errors) console.error(`- ${error}`);
  process.exit(1);
}

const totalCases = canonicalPlatforms.reduce((count, item) => count + item.cases.length, 0);
console.log(
  `Validation passed: ${platformDefinitions.length} manifest-defined platforms, ${totalCases} canonical cases, ` +
  `${generation.reportCount} generated bilingual reports, refreshed VLM observations, relative links, public scrub, and source/generated checksums verified.`,
);

function validateManifest(value) {
  if (!value) return [];
  if (value.schemaVersion !== 3) fail('manifest schemaVersion must be 3');
  if (value.release?.version !== '1.1.0' || value.release?.tag !== 'v1.1.0') {
    fail('manifest release identity must be version 1.1.0 and tag v1.1.0');
  }
  if (value.release?.publicationState !== 'prepared-not-published') {
    fail('manifest release.publicationState must be prepared-not-published');
  }
  if (!sha1(value.sourceBaseline?.commit)) fail('manifest sourceBaseline.commit must be a frozen 40-character Git commit');
  if (!sha1(value.sourceBaseline?.tree)) fail('manifest sourceBaseline.tree must be a frozen 40-character Git tree');
  if (!sha256(value.dataset?.sha256)) fail('manifest dataset.sha256 must be a 64-character SHA-256');
  const targetFpsValues = value.controls?.smallModelTargetFps;
  if (!Array.isArray(targetFpsValues) || !targetFpsValues.length || targetFpsValues.some((fps) => !positiveNumber(fps))) {
    fail('manifest controls.smallModelTargetFps must be a non-empty array of positive numbers');
  } else if (new Set(targetFpsValues).size !== targetFpsValues.length) {
    fail('manifest controls.smallModelTargetFps must not contain duplicates');
  }
  if (!positiveInteger(value.evidence?.smallModelCaseCount)) fail('manifest evidence.smallModelCaseCount must be a positive integer');
  if (value.evidence?.caseSchema !== 'results/cases.schema.json') fail('manifest evidence.caseSchema must point to the canonical schema');
  if (value.evidence?.vlmObservations !== 'results/vlm-observations.json') fail('manifest evidence.vlmObservations must point to the canonical VLM file');
  if (typeof value.benchmarkTool?.role !== 'string' || !value.benchmarkTool.role.includes('current publication')) {
    fail('manifest benchmarkTool.role must distinguish the current publication tool from execution provenance');
  }

  const benchmarkToolFiles = {
    cliSha256: 'tools/scenario-bench/src/cli.js',
    evaluatorSha256: 'tools/scenario-bench/src/step-evaluator.js',
    metricsSamplerSha256: 'tools/scenario-bench/src/metrics-sampler.js',
    reportWriterSha256: 'tools/scenario-bench/src/report-writer.js',
    vlmReadinessSha256: 'tools/scenario-bench/src/vlm-readiness.js',
    scenarioPackageSha256: 'tools/scenario-bench/src/scenario-package.js',
    taskRunnerSha256: 'tools/scenario-bench/src/task-runner.js',
    lockfileSha256: 'tools/scenario-bench/package-lock.json',
  };
  for (const [field, relative] of Object.entries(benchmarkToolFiles)) {
    const expected = value.benchmarkTool?.[field];
    if (!sha256(expected)) {
      fail(`manifest benchmarkTool.${field} must be a 64-character SHA-256`);
      continue;
    }
    const file = path.join(workspace, ...relative.split('/'));
    if (!fs.existsSync(file)) {
      fail(`manifest benchmark tool file is missing: ${relative}`);
      continue;
    }
    const actual = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    if (actual !== expected) fail(`manifest benchmarkTool.${field} differs from ${relative}`);
  }

  const archive = value.evidence?.fullEvidenceArchive;
  if (!archive || typeof archive !== 'object') {
    fail('manifest evidence.fullEvidenceArchive is required');
  } else {
    if (!sha256(archive.sha256)) fail('full evidence archive SHA-256 is invalid');
    if (!sha1(archive.sourceCommit) || !sha1(archive.sourceTree)) fail('full evidence archive source commit/tree is invalid');
    if (archive.scope !== '49 canonical small-model cases only') fail('full evidence archive scope must remain limited to the 49 small-model cases');
    if (archive.publicationState !== 'prepared-not-published') fail('full evidence archive must remain prepared-not-published');
    if (archive.includedInRepository !== false) fail('full evidence archive must not be included in the repository');
  }

  if (value.qualification?.canonicalBenchmarkDatasetComplete !== true) {
    fail('manifest qualification.canonicalBenchmarkDatasetComplete must be true');
  }
  if (value.qualification?.generatedReportContractVerified !== true) {
    fail('manifest qualification.generatedReportContractVerified must be true');
  }
  if (value.qualification?.productReleaseQualified !== false) {
    fail('benchmark evidence must not claim product release qualification');
  }

  const definitions = Array.isArray(value.platforms) ? value.platforms : [];
  if (!definitions.length) fail('manifest must declare at least one platform');
  const ids = definitions.map((item) => item?.id);
  if (ids.some((id) => typeof id !== 'string' || !/^[a-z0-9-]+$/.test(id))) fail('manifest contains an invalid platform id');
  if (new Set(ids).size !== ids.length) fail('manifest contains duplicate platform ids');
  if (!definitions.some((item) => item?.scope === 'release-platform')) fail('manifest must declare at least one release platform');
  const rk3576 = definitions.find((item) => item?.id === 'rk3576');
  const rv1126b = definitions.find((item) => item?.id === 'rv1126b');
  if (rk3576?.scope !== 'release-platform' || rv1126b?.scope !== 'release-platform') {
    fail('RK3576 and RV1126B must share the release-platform scope');
  }

  const allowedScopes = new Set(['release-platform', 'additional-experimental-platform']);
  for (const definition of definitions) {
    const label = `manifest platform ${definition?.id ?? '<missing>'}`;
    if (!allowedScopes.has(definition?.scope)) fail(`${label} has an unsupported scope`);
    if (definition?.environment !== `environments/${definition.id}.json`) fail(`${label} environment reference is not canonical`);
    if (definition?.models !== `models/${definition.id}.json`) fail(`${label} model reference is not canonical`);
    if (definition?.results !== `results/${definition.id}/cases.json`) fail(`${label} results reference must point to its canonical case file`);
  }

  const expectedCaseFiles = definitions.map((item) => `results/${item.id}/cases.json`).sort();
  const declaredCaseFiles = Array.isArray(value.evidence?.canonicalSmallModelCases)
    ? [...value.evidence.canonicalSmallModelCases].sort()
    : [];
  if (!sameArray(expectedCaseFiles, declaredCaseFiles)) {
    fail('manifest evidence.canonicalSmallModelCases must exactly match the manifest platform inventory');
  }
  return definitions;
}

function validateCanonicalCases(manifestValue, definitions) {
  const result = [];
  const globalCaseKeys = new Set();
  const sourceSummaryHashes = new Set();
  for (const definition of definitions) {
    const relativePath = `results/${definition.id}/cases.json`;
    requireFile(root, relativePath);
    const value = readJsonIfPresent(root, relativePath);
    if (!value) continue;
    result.push(value);
    const label = `${definition.id} canonical cases`;
    expectObjectKeys(value, [
      '$schema', 'schemaVersion', 'benchmark', 'platformId', 'platform', 'scope', 'evidenceDate', 'caseCount', 'gates', 'cases',
    ], label);
    if (value.$schema !== '../cases.schema.json') fail(`${label} has an incorrect schema reference`);
    if (value.schemaVersion !== 3) fail(`${label} schemaVersion must be 3`);
    if (value.platformId !== definition.id) fail(`${label} platformId does not match the manifest`);
    if (value.scope !== definition.scope) fail(`${label} scope does not match the manifest`);
    if (!/^\d{4}-\d{2}-\d{2}$/.test(value.evidenceDate ?? '')) fail(`${label} evidenceDate is invalid`);
    if (!deepEqual(value.gates, manifestValue?.controls?.cvGates)) fail(`${label} gates differ from the manifest controls`);

    const cases = Array.isArray(value.cases) ? value.cases : [];
    if (!Array.isArray(value.cases)) fail(`${label} cases must be an array`);
    if (value.caseCount !== cases.length) fail(`${label} caseCount does not match its case array`);
    const localIds = new Set();
    for (const benchmarkCase of cases) {
      validateCase(definition, value.gates ?? {}, benchmarkCase);
      const caseKey = `${definition.id}/${benchmarkCase?.caseId}`;
      if (localIds.has(benchmarkCase?.caseId)) fail(`${label} contains duplicate case id ${benchmarkCase?.caseId}`);
      if (globalCaseKeys.has(caseKey)) fail(`duplicate global case key ${caseKey}`);
      localIds.add(benchmarkCase?.caseId);
      globalCaseKeys.add(caseKey);
      if (sha256(benchmarkCase?.sourceSummarySha256)) {
        if (sourceSummaryHashes.has(benchmarkCase.sourceSummarySha256)) fail(`duplicate source summary hash in ${caseKey}`);
        sourceSummaryHashes.add(benchmarkCase.sourceSummarySha256);
      }
    }
    for (const workload of ['person-detector', 'no-safety-helmet-analysis']) {
      for (const fps of manifestValue?.controls?.smallModelTargetFps ?? []) {
        if (!cases.some((item) => item.workload === workload && item.targetFps === fps)) {
          fail(`${label} is missing ${workload} at ${fps} FPS`);
        }
      }
    }
    if (!cases.some((item) => item.workload === 'concurrent-mixed')) fail(`${label} is missing a concurrent-mixed case`);
  }

  const actualCount = result.reduce((count, item) => count + (item.cases?.length ?? 0), 0);
  if (manifestValue && actualCount !== manifestValue.evidence?.smallModelCaseCount) {
    fail(`canonical case total ${actualCount} does not match manifest evidence.smallModelCaseCount ${manifestValue.evidence?.smallModelCaseCount}`);
  }
  return result;
}

function validateCase(definition, gates, value) {
  const label = `${definition.id}/${value?.caseId ?? '<missing-case-id>'}`;
  expectObjectKeys(value, [
    'caseId', 'sourceCaseId', 'workload', 'targetFps', 'configuredChannels', 'outcome', 'boundaryKind', 'lastPassingChannels',
    'firstBlockedChannels', 'blockedReason', 'sourceSummarySha256', 'steps',
  ], label);
  const identity = parseCaseId(value?.caseId);
  const sourceIdentity = parseSourceCaseId(value?.sourceCaseId);
  if (!identity) fail(`${label} caseId does not encode workload, target FPS, and configured channels`);
  if (!sourceIdentity) fail(`${label} sourceCaseId does not encode the archived workload, target FPS, and configured channels`);
  if (identity && value.workload !== identity.workload) fail(`${label} workload differs from its caseId`);
  if (identity && value.targetFps !== identity.targetFps) fail(`${label} targetFps differs from its caseId`);
  if (identity && value.configuredChannels !== identity.configuredChannels) fail(`${label} configuredChannels differs from its caseId`);
  if (sourceIdentity && value.workload !== sourceIdentity.workload) fail(`${label} workload differs from its archived sourceCaseId`);
  if (sourceIdentity && value.targetFps !== sourceIdentity.targetFps) fail(`${label} targetFps differs from its archived sourceCaseId`);
  if (sourceIdentity && value.configuredChannels !== sourceIdentity.configuredChannels) fail(`${label} configuredChannels differs from its archived sourceCaseId`);
  if (!sha256(value?.sourceSummarySha256)) fail(`${label} sourceSummarySha256 is invalid`);
  if (!positiveInteger(value?.configuredChannels) || !positiveInteger(value?.lastPassingChannels)) fail(`${label} channel boundaries must be positive integers`);
  if (value?.lastPassingChannels > value?.configuredChannels) fail(`${label} lastPassingChannels exceeds configuredChannels`);

  const allowedOutcomes = new Set(['completed', 'stopped', 'setup-blocked']);
  const allowedBoundaries = new Set(['lower-bound', 'performance-stop', 'binding-blocked', 'storage-blocked']);
  if (!allowedOutcomes.has(value?.outcome)) fail(`${label} outcome is invalid`);
  if (!allowedBoundaries.has(value?.boundaryKind)) fail(`${label} boundaryKind is invalid`);

  const steps = Array.isArray(value?.steps) ? value.steps : [];
  if (!steps.length) fail(`${label} must contain at least one measured step`);
  let previousChannel = 0;
  for (const [index, step] of steps.entries()) {
    validateStep(label, value, gates, step, index);
    if (positiveInteger(step?.channels) && step.channels <= previousChannel) fail(`${label} step channels must be strictly increasing`);
    previousChannel = step?.channels ?? previousChannel;
  }

  const passingChannels = steps.filter((step) => step?.result === 'PASS').map((step) => step.channels);
  const nonPassingChannels = steps.filter((step) => step?.result !== 'PASS').map((step) => step.channels);
  const measuredLastPass = passingChannels.length ? Math.max(...passingChannels) : null;
  if (measuredLastPass !== value?.lastPassingChannels) fail(`${label} lastPassingChannels does not match measured PASS steps`);

  if (value?.boundaryKind === 'lower-bound') {
    if (value.outcome !== 'completed' || value.firstBlockedChannels !== null || value.blockedReason !== null) {
      fail(`${label} lower-bound metadata is inconsistent`);
    }
    if (value.lastPassingChannels !== value.configuredChannels || nonPassingChannels.length) {
      fail(`${label} lower-bound must pass every configured step`);
    }
  } else if (value?.boundaryKind === 'performance-stop') {
    const firstNonPass = nonPassingChannels.length ? Math.min(...nonPassingChannels) : null;
    if (value.outcome !== 'stopped' || value.firstBlockedChannels !== firstNonPass || typeof value.blockedReason !== 'string') {
      fail(`${label} performance-stop metadata is inconsistent`);
    }
  } else if (value) {
    if (value.outcome !== 'setup-blocked' || value.firstBlockedChannels !== value.lastPassingChannels + 1) {
      fail(`${label} setup-blocked boundary metadata is inconsistent`);
    }
    if (nonPassingChannels.length || typeof value.blockedReason !== 'string') {
      fail(`${label} setup-blocked case must contain only completed PASS steps and a reason`);
    }
  }
}

function validateStep(caseLabel, benchmarkCase, gates, step, index) {
  const label = `${caseLabel} step ${index + 1}`;
  expectObjectKeys(step, [
    'channels', 'holdSeconds', 'result', 'tasks', 'minimumProcessingFps', 'maximumDetectorLatencyMs',
    'maximumCriticalPathLatencyMs', 'averageDiscardRate', 'maximumChannelDiscardRate', 'acceleratorPeakPercent',
    'acceleratorMemoryPeakPercent', 'cpuPeakPercent', 'memoryPeakPercent', 'failureReason',
  ], label);
  if (!positiveInteger(step?.channels) || step.channels > benchmarkCase.configuredChannels) fail(`${label} channels are invalid`);
  if (!positiveNumber(step?.holdSeconds)) fail(`${label} holdSeconds must be positive`);
  if (!new Set(['PASS', 'FAIL', 'STOP']).has(step?.result)) fail(`${label} result is invalid`);
  if (step?.result === 'PASS' && step.failureReason !== null) fail(`${label} PASS must not contain a failure reason`);
  if (step?.result !== 'PASS' && typeof step?.failureReason !== 'string') fail(`${label} non-PASS result must contain a failure reason`);

  const tasks = Array.isArray(step?.tasks) ? step.tasks : [];
  const expectedTaskNames = benchmarkCase.workload === 'concurrent-mixed'
    ? ['person-detector', 'no-safety-helmet-analysis']
    : [benchmarkCase.workload];
  if (!sameArray(tasks.map((task) => task?.name).sort(), [...expectedTaskNames].sort())) fail(`${label} task inventory differs from its workload`);
  for (const task of tasks) {
    expectObjectKeys(task, [
      'name', 'targetFps', 'minimumProcessingFps', 'minimumFpsRatio', 'missingRate', 'averageDiscardRate',
      'maximumDetectorLatencyMs', 'maximumCriticalPathLatencyMs',
    ], `${label} task ${task?.name ?? '<missing>'}`);
    if (task?.targetFps !== benchmarkCase.targetFps) fail(`${label} task ${task?.name} targetFps differs from its case`);
    for (const key of ['minimumProcessingFps', 'minimumFpsRatio', 'missingRate', 'averageDiscardRate', 'maximumDetectorLatencyMs', 'maximumCriticalPathLatencyMs']) {
      if (!nullableNonNegativeNumber(task?.[key])) fail(`${label} task ${task?.name} ${key} must be a non-negative number or null`);
    }
  }
  for (const key of [
    'minimumProcessingFps', 'maximumDetectorLatencyMs', 'maximumCriticalPathLatencyMs', 'averageDiscardRate',
    'maximumChannelDiscardRate', 'acceleratorPeakPercent', 'acceleratorMemoryPeakPercent', 'cpuPeakPercent', 'memoryPeakPercent',
  ]) {
    if (!nullableNonNegativeNumber(step?.[key])) fail(`${label} ${key} must be a non-negative number or null`);
  }

  if (step?.result === 'PASS') {
    for (const task of tasks) {
      if (!(task.minimumFpsRatio >= gates.minFpsRatio)) fail(`${label} PASS violates the task FPS-ratio gate`);
      if (!(task.missingRate <= gates.maxMissingRate)) fail(`${label} PASS violates the task missing-rate gate`);
      if (!(task.averageDiscardRate <= gates.maxAverageDiscardRate)) fail(`${label} PASS violates the task discard-rate gate`);
      if (!(task.maximumDetectorLatencyMs <= gates.maxDetectorLatencyMs)) fail(`${label} PASS violates the detector-latency gate`);
      if (!(task.maximumCriticalPathLatencyMs <= gates.maxCriticalPathLatencyMs)) fail(`${label} PASS violates the critical-path gate`);
    }
    if (!(step.averageDiscardRate <= gates.maxAverageDiscardRate)) fail(`${label} PASS violates the aggregate discard-rate gate`);
    if (!(step.maximumDetectorLatencyMs <= gates.maxDetectorLatencyMs)) fail(`${label} PASS violates the aggregate detector-latency gate`);
    if (!(step.maximumCriticalPathLatencyMs <= gates.maxCriticalPathLatencyMs)) fail(`${label} PASS violates the aggregate critical-path gate`);
  }
}

function validateVlmEvidence(manifestValue, definitions) {
  const value = readJsonIfPresent(root, 'results/vlm-observations.json');
  if (!value) return { observations: [] };
  if (value.schemaVersion !== 3 || value.evidenceStatus !== 'refreshed-controlled-vlm-evidence'
      || value.refreshedWithControlledInput !== true || value.refreshDate !== '2026-08-20') {
    fail('canonical VLM evidence status is inconsistent');
  }
  const refresh = manifestValue?.evidence?.vlmRefresh;
  if (!refresh || value.crossPlatformComparable !== refresh.crossPlatformComparable
      || value.crossPlatformComparable !== false) {
    fail('mixed VLM readiness protocols must not be marked cross-platform comparable');
  }
  const publicationPolicy = value.publicationEvaluation;
  const manifestPublicationPolicy = manifestValue?.evidence?.vlmPublicationEvaluation;
  expectObjectKeys(publicationPolicy, [
    'classification',
    'targetFpsPerChannel',
    'minimumActiveRouteFpsRatio',
    'requiresNonFpsGatePass',
    'usesContiguousCompletedPrefix',
    'startupSensitiveStopsArePerformanceFailures',
    'capacityClaimAllowed',
  ], 'canonical VLM publication evaluation');
  if (!deepEqual(publicationPolicy, manifestPublicationPolicy)) {
    fail('canonical VLM publication evaluation differs from the release manifest');
  }
  if (publicationPolicy?.classification !== 'conservative-post-evaluation'
      || publicationPolicy?.targetFpsPerChannel !== manifestValue?.controls?.vlmTargetFpsPerChannel
      || publicationPolicy?.minimumActiveRouteFpsRatio !== 0.8
      || publicationPolicy?.requiresNonFpsGatePass !== true
      || publicationPolicy?.usesContiguousCompletedPrefix !== true
      || publicationPolicy?.startupSensitiveStopsArePerformanceFailures !== false
      || publicationPolicy?.capacityClaimAllowed !== false
      || manifestValue?.controls?.vlmFpsGateEnabled !== false) {
    fail('VLM publication evaluation policy is inconsistent');
  }
  if (typeof refresh?.projectionPolicy !== 'string' || !refresh.projectionPolicy.includes('first non-FPS gate')) {
    fail('manifest VLM projection policy is missing');
  }
  const releaseIds = new Set(definitions.filter((item) => item.scope === 'release-platform').map((item) => item.id));
  const observations = Array.isArray(value.observations) ? value.observations : [];
  const ids = observations.map((item) => item?.platformId);
  if (new Set(ids).size !== ids.length) fail('canonical VLM file contains duplicate platforms');
  for (const observation of observations) {
    const label = `${observation?.platformId ?? '<missing>'} VLM observation`;
    if (!releaseIds.has(observation?.platformId)) fail(`${label} is not attached to a release platform`);
    if (!sha256(observation?.sourceSummarySha256)) fail(`${label} sourceSummarySha256 is invalid`);
    if (!sha256(observation?.sourceMetricsSha256)) fail(`${label} sourceMetricsSha256 is invalid`);
    if (!sha1(observation?.source?.commit) || !sha1(observation?.source?.tree)) fail(`${label} source commit/tree is invalid`);
    if (!sha256(observation?.source?.toolPatchSha256)) fail(`${label} source tool-patch hash is invalid`);
    if (!['final-per-route-readiness', 'pre-readiness-startup-sensitive'].includes(observation?.source?.protocolClass)) {
      fail(`${label} readiness protocol class is invalid`);
    }
    if (observation?.source?.commit !== refresh?.sourceCommit || observation?.source?.tree !== refresh?.sourceTree) {
      fail(`${label} source provenance differs from the release manifest`);
    }
    const expectedToolPatch = observation?.source?.protocolClass === 'final-per-route-readiness'
      ? refresh?.finalToolPatchSha256
      : refresh?.preReadinessToolPatchSha256;
    if (observation?.source?.toolPatchSha256 !== expectedToolPatch) {
      fail(`${label} tool-patch provenance differs from the release manifest`);
    }
    if (observation?.source?.protocolClass === 'final-per-route-readiness') {
      if (observation?.source?.artifactForm !== 'native-completed-run'
          || observation?.source?.projectionCutoff !== null
          || observation?.source?.executionContinuedBeyondProjection !== false) {
        fail(`${label} must identify its native completed-run artifact`);
      }
    } else if (observation?.source?.artifactForm !== 'first-failure-public-projection'
        || observation?.source?.projectionCutoff !== 'first non-FPS gate stop'
        || observation?.source?.executionContinuedBeyondProjection !== true
        || !sha256(observation?.source?.originalRunSummarySha256)
        || !sha256(observation?.source?.originalRunMetricsSha256)) {
      fail(`${label} must identify its first-failure projection and original-run hashes`);
    }
    if (observation?.workload?.targetFpsPerChannel !== 0.1) fail(`${label} target FPS must remain 0.1 per channel`);
    if (observation?.workload?.counterSemantics !== 'task-local-completion-counter') fail(`${label} must use task-local completion counters`);
    if (observation?.observedBoundary?.capacityClaimAllowed !== false) fail(`${label} must not claim capacity`);
    const steps = Array.isArray(observation?.steps) ? observation.steps : [];
    let previousChannel = 0;
    for (const [index, step] of steps.entries()) {
      if (!positiveInteger(step?.channels) || step.channels <= previousChannel) fail(`${label} channels must be strictly increasing`);
      previousChannel = step?.channels ?? previousChannel;
      if (step?.targetFpsPerChannel !== observation.workload.targetFpsPerChannel) fail(`${label} step ${index + 1} target FPS changed`);
      if (step?.fpsGateEnabled !== false) fail(`${label} step ${index + 1} FPS gate must remain disabled`);
      if (!nullableNonNegativeNumber(step?.currentRouteFps)) fail(`${label} step ${index + 1} current-route FPS is invalid`);
      if (!nullableNonNegativeNumber(step?.fpsAchievementRatioObserved)) fail(`${label} step ${index + 1} FPS ratio is invalid`);
      if (!nullableNonNegativeNumber(step?.minimumActiveRouteFps)) fail(`${label} step ${index + 1} active-route minimum FPS is invalid`);
      if (!nullableNonNegativeNumber(step?.minimumActiveRouteFpsRatioObserved)) fail(`${label} step ${index + 1} active-route minimum FPS ratio is invalid`);
      if (step?.minimumActiveRouteFps != null && step?.minimumActiveRouteFpsRatioObserved != null) {
        const expectedRatio = step.minimumActiveRouteFps / step.targetFpsPerChannel;
        if (Math.abs(step.minimumActiveRouteFpsRatioObserved - expectedRatio) > 0.001) {
          fail(`${label} step ${index + 1} active-route minimum FPS ratio is inconsistent`);
        }
      }
    }
    const passing = steps.filter((step) => step?.nonFpsGateResult === 'PASS').map((step) => step.channels);
    const stopped = steps.filter((step) => step?.nonFpsGateResult !== 'PASS').map((step) => step.channels);
    const highestPass = passing.length ? Math.max(...passing) : null;
    const firstStop = stopped.length ? Math.min(...stopped) : null;
    if (observation?.observedBoundary?.highestNonFpsPassingChannels !== highestPass) fail(`${label} highest non-FPS passing boundary is inconsistent`);
    if (observation?.observedBoundary?.firstNonFpsStopChannels !== firstStop) fail(`${label} first non-FPS stop boundary is inconsistent`);

    const publicationBoundary = observation?.publicationBoundary;
    expectObjectKeys(publicationBoundary, [
      'displayChannels',
      'firstExcludedChannels',
      'firstExcludedReason',
      'claimClass',
      'capacityClaimAllowed',
      'reason',
    ], `${label} publication boundary`);
    let expectedDisplayChannels = null;
    let expectedFirstExcludedChannels = null;
    let expectedFirstExcludedReason = null;
    for (const step of steps) {
      if (expectedFirstExcludedChannels != null) break;
      const meetsFpsReference = step?.minimumActiveRouteFpsRatioObserved != null
        && step.minimumActiveRouteFpsRatioObserved >= publicationPolicy?.minimumActiveRouteFpsRatio;
      const hasCompleteNonFpsWindow = step?.nonFpsGateResult === 'PASS';
      if (meetsFpsReference && hasCompleteNonFpsWindow) {
        expectedDisplayChannels = step.channels;
        continue;
      }
      expectedFirstExcludedChannels = step?.channels ?? null;
      expectedFirstExcludedReason = !meetsFpsReference
        ? 'below-fps-reference'
        : observation?.source?.protocolClass === 'pre-readiness-startup-sensitive'
          ? 'startup-sensitive-incomplete'
          : 'non-fps-gate-stop';
    }
    if (publicationBoundary?.displayChannels !== expectedDisplayChannels) {
      fail(`${label} conservative display boundary is inconsistent with the contiguous 80% FPS prefix`);
    }
    if (publicationBoundary?.firstExcludedChannels !== expectedFirstExcludedChannels
        || publicationBoundary?.firstExcludedReason !== expectedFirstExcludedReason) {
      fail(`${label} first publication-excluded step is inconsistent`);
    }
    if (publicationBoundary?.claimClass !== 'conservative-performance-display'
        || publicationBoundary?.capacityClaimAllowed !== false
        || typeof publicationBoundary?.reason !== 'string'
        || publicationBoundary.reason.length < 20) {
      fail(`${label} publication boundary claim class is invalid`);
    }
    if (publicationBoundary?.firstExcludedReason === 'startup-sensitive-incomplete'
        && observation?.source?.protocolClass !== 'pre-readiness-startup-sensitive') {
      fail(`${label} cannot exclude a step as startup-sensitive under the final readiness protocol`);
    }
  }
  if (manifestValue?.evidence?.vlmObservations !== 'results/vlm-observations.json') fail('manifest VLM reference changed');
  return value;
}

function validateArchivedEvidence(file, manifestValue, platforms) {
  const resolved = path.resolve(file);
  if (!fs.existsSync(resolved) || !fs.statSync(resolved).isFile()) {
    fail(`full evidence archive is missing: ${resolved}`);
    return;
  }
  const archiveBytes = fs.readFileSync(resolved);
  const archiveDigest = crypto.createHash('sha256').update(archiveBytes).digest('hex');
  if (archiveDigest !== manifestValue?.evidence?.fullEvidenceArchive?.sha256) {
    fail('full evidence archive SHA-256 differs from the manifest');
    return;
  }

  const archiveRoot = 'docs/benchmarks/scenario-bench/v1.1/results';
  for (const platform of platforms) {
    for (const benchmarkCase of platform.cases) {
      const entry = `${archiveRoot}/${platform.platformId}/cases/${benchmarkCase.sourceCaseId}/summary.json`;
      const bytes = readArchiveEntry(resolved, entry);
      if (!bytes) continue;
      const digest = crypto.createHash('sha256').update(bytes).digest('hex');
      if (digest !== benchmarkCase.sourceSummarySha256) fail(`archive summary hash differs for ${platform.platformId}/${benchmarkCase.caseId}`);
      let raw;
      try {
        raw = JSON.parse(bytes.toString('utf8'));
      } catch (error) {
        fail(`archive summary is invalid JSON for ${platform.platformId}/${benchmarkCase.caseId}: ${error.message}`);
        continue;
      }
      const normalizedRaw = normalizeArchivedCase(raw);
      if (raw.caseId !== benchmarkCase.sourceCaseId) {
        fail(`canonical sourceCaseId differs from archived caseId for ${platform.platformId}/${benchmarkCase.caseId}`);
      }
      for (const key of [
        'workload', 'targetFps', 'configuredChannels', 'outcome', 'boundaryKind',
        'lastPassingChannels', 'firstBlockedChannels', 'blockedReason', 'steps',
      ]) {
        if (!deepEqual(normalizedRaw[key], benchmarkCase[key])) fail(`canonical ${key} differs from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
      }
      const archivedScope = normalizeArchivedPlatformScope(platform.platformId, raw.platformScope);
      if (raw.platformId !== platform.platformId || raw.platform !== platform.platform || archivedScope !== platform.scope) {
        fail(`canonical platform identity differs from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
      }
      if (raw.evidenceDate !== platform.evidenceDate) fail(`canonical evidenceDate differs from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
      const normalizedGates = {
        minFpsRatio: raw.gates?.minimumFpsRatio,
        maxMissingRate: raw.gates?.maximumMissingRate,
        maxAverageDiscardRate: raw.gates?.maximumAverageDiscardRate,
        maxCriticalPathLatencyMs: raw.gates?.maximumCriticalPathLatencyMs,
        maxDetectorLatencyMs: raw.gates?.maximumDetectorLatencyMs,
      };
      if (!deepEqual(normalizedGates, platform.gates)) fail(`canonical gates differ from archived summary for ${platform.platformId}/${benchmarkCase.caseId}`);
    }
  }

}

function readArchiveEntry(archive, entry) {
  const result = spawnSync('tar', ['-xOf', archive, entry], { encoding: null, maxBuffer: 16 * 1024 * 1024 });
  if (result.status !== 0) {
    fail(`full evidence archive is missing ${entry}`);
    return null;
  }
  return result.stdout;
}

function validateCanonicalSourceLayout(definitions) {
  for (const relativePath of ['report.html', 'report.zh-CN.html']) {
    if (fs.existsSync(path.join(root, relativePath))) fail(`generated report must not be tracked in canonical source: ${relativePath}`);
  }
  const resultsRoot = path.join(root, 'results');
  if (!fs.existsSync(resultsRoot)) return;
  const allowedRootEntries = new Set(['cases.schema.json', 'vlm-observations.json', ...definitions.map((item) => item.id)]);
  for (const entry of fs.readdirSync(resultsRoot, { withFileTypes: true })) {
    if (!allowedRootEntries.has(entry.name)) fail(`derived or undeclared result is tracked in canonical source: results/${entry.name}`);
  }
  for (const definition of definitions) {
    const platformRoot = path.join(resultsRoot, definition.id);
    if (!fs.existsSync(platformRoot)) continue;
    for (const entry of fs.readdirSync(platformRoot, { withFileTypes: true })) {
      if (entry.name !== 'cases.json' || !entry.isFile()) {
        fail(`derived platform artifact is tracked in canonical source: results/${definition.id}/${entry.name}`);
      }
    }
  }
}

function validateSchemaDocument() {
  const schema = readJsonIfPresent(root, 'results/cases.schema.json');
  if (!schema) return;
  if (schema.$schema !== 'https://json-schema.org/draft/2020-12/schema') fail('case schema must use JSON Schema draft 2020-12');
  if (schema.type !== 'object' || schema.additionalProperties !== false) fail('case schema root must be a closed object');
  for (const definition of ['gates', 'case', 'step', 'task']) {
    if (!schema.$defs?.[definition]) fail(`case schema is missing $defs.${definition}`);
  }
  if (!schema.$defs?.case?.required?.includes('sourceCaseId')) fail('case schema must require sourceCaseId provenance');
}

function validateGeneratedPack(outputRoot, manifestValue, platforms, vlmValue, generationResult) {
  const caseCount = platforms.reduce((count, item) => count + item.cases.length, 0);
  const vlmIds = new Set((vlmValue?.observations ?? []).map((item) => item.platformId));
  const expectedReports = ['report.html', 'report.zh-CN.html'];
  for (const platform of platforms) {
    for (const localeSuffix of ['', '.zh-CN']) {
      expectedReports.push(
        `results/${platform.platformId}/report${localeSuffix}.html`,
        `results/${platform.platformId}/cases/report${localeSuffix}.html`,
        `results/${platform.platformId}/single-workload/report${localeSuffix}.html`,
        `results/${platform.platformId}/concurrent-mixed/report${localeSuffix}.html`,
      );
      if (vlmIds.has(platform.platformId)) {
        expectedReports.push(`results/${platform.platformId}/vlm-observation/report${localeSuffix}.html`);
      }
      for (const benchmarkCase of platform.cases) {
        expectedReports.push(`results/${platform.platformId}/cases/${benchmarkCase.caseId}/report${localeSuffix}.html`);
      }
    }
  }
  const actualReports = walk(outputRoot)
    .filter((file) => file.toLowerCase().endsWith('.html'))
    .map((file) => path.relative(outputRoot, file).replaceAll('\\', '/'))
    .sort();
  const sortedExpected = [...expectedReports].sort();
  if (!sameArray(actualReports, sortedExpected)) compareSets('generated report inventory', new Set(actualReports), new Set(sortedExpected));
  if (generationResult.reportCount !== expectedReports.length) fail('generator reportCount does not match the manifest-derived inventory');
  if (generationResult.platformCount !== platforms.length || generationResult.caseCount !== caseCount) fail('generator platform/case counts are inconsistent');

  for (const report of expectedReports) {
    const file = path.join(outputRoot, ...report.split('/'));
    if (!fs.existsSync(file)) continue;
    const html = fs.readFileSync(file, 'utf8');
    const expectedLang = report.endsWith('.zh-CN.html') ? 'zh-CN' : 'en';
    const actualLang = html.match(/<html\b[^>]*\blang=["']([^"']+)["']/i)?.[1];
    if (actualLang !== expectedLang) fail(`generated report has incorrect lang: ${report}`);
    if (!/<\/html>\s*$/i.test(html)) fail(`generated report is incomplete: ${report}`);
    if (!html.includes('class="report-nav"')) fail(`generated report navigation is missing: ${report}`);
    const tables = html.match(/<table\b/gi)?.length ?? 0;
    const wrappers = html.match(/<div class="table"/gi)?.length ?? 0;
    if (tables !== wrappers) fail(`generated report lacks responsive table wrappers: ${report}`);
    if (!html.includes('overflow-wrap:anywhere')) fail(`generated report lacks long-token wrapping: ${report}`);
    if (/\b(?:undefined|NaN)\b|\[object Object\]/.test(html)) fail(`generated report contains an unresolved value: ${report}`);
    if (/dual-detector|RV1126B\s+(?:Experimental|实验)/i.test(html)) fail(`generated report contains obsolete public terminology: ${report}`);
  }

  for (const rootReport of ['report.html', 'report.zh-CN.html']) {
    const html = fs.readFileSync(path.join(outputRoot, rootReport), 'utf8');
    for (const platform of platforms) {
      if (!html.includes(platform.platform)) fail(`${rootReport} is missing release platform ${platform.platform}`);
    }
    if (!html.includes(rootReport.endsWith('.zh-CN.html') ? '并发混合任务矩阵' : 'Concurrent mixed-workload matrix')) {
      fail(`${rootReport} is missing the concurrent mixed-workload matrix`);
    }
    if (!html.includes(rootReport.endsWith('.zh-CN.html') ? 'VLM 性能展示边界' : 'VLM performance display boundaries')) {
      fail(`${rootReport} is missing the VLM performance display matrix`);
    }
  }

  const index = readJsonIfPresent(outputRoot, 'results/index.json');
  const casesIndex = readJsonIfPresent(outputRoot, 'results/cases.json');
  const matrix = readJsonIfPresent(outputRoot, 'results/workload-matrix.json');
  if (index?.caseCount !== caseCount || casesIndex?.caseCount !== caseCount) fail('generated indexes do not contain the canonical case total');
  if (index?.publicationStatus !== manifestValue?.release?.publicationState || matrix?.publicationStatus !== manifestValue?.release?.publicationState) {
    fail('generated index publication status differs from the manifest');
  }
  if (!deepEqual(matrix?.vlmPublicationEvaluation?.policy, vlmValue?.publicationEvaluation)) {
    fail('generated VLM publication policy differs from the canonical evidence');
  }
  const expectedPublicationBoundaries = platforms.map((platform) => ({
    platformId: platform.platformId,
    publicationBoundary: vlmValue?.observations?.find((item) => item.platformId === platform.platformId)?.publicationBoundary ?? null,
  }));
  if (!deepEqual(matrix?.vlmPublicationEvaluation?.platforms, expectedPublicationBoundaries)) {
    fail('generated VLM publication boundaries differ from the canonical evidence');
  }
}

function validatePublicScrub(files, packRoot, label) {
  const forbidden = [
    [/\bhost\d+\b/gi, 'internal host alias'],
    [/\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})\b/g, 'private IP address'],
    [/(?<![\d.])(?:4773|7463|11099|16064|41773|67093|78510|91985|43921|63606|6047042|7486163|7463001)(?![\d.])/g, 'internal algorithm/model ID'],
    [/["']?(?:algorithm|schedule|task|scene|model)[_-]?(?:id|ID)["']?\s*[:=]\s*["']?\d{3,}/gi, 'internal algorithm/task/model ID field'],
    [/\b(?:AA|BA)_\d+\b/gi, 'internal action-node ID'],
    [/\b(?:LX|DA)\d{5,}\b/gi, 'internal channel ID'],
    [/["']?(?:channel|videoChannel)[_-]?(?:id|ID)["']?\s*:/g, 'internal channel ID field'],
    [/\b[A-Za-z]:[\\/](?![\\/])[^\s<>"']*/g, 'Windows absolute path'],
    [/(?:^|[\s"'(=])\/(?:home|root|Users|data|var|tmp|opt|mnt|srv|etc)\/[^\s<>"')]+/gm, 'Linux absolute path'],
    [/\b(?:linaro|moons|admin123|songyuhang|123456)\b/gi, 'known credential'],
    [/\b(?:username|user|account|账号|用户名)\b\s*[:=：]\s*["']?(?:admin|root|linaro|moons|songyuhang)\b/gi, 'credential/account value'],
    [/\b(?:password|passwd|credential|secret|access[_-]?token)\b\s*[:=：]\s*["']?(?!<|\$\{|redacted\b|not-set\b)[A-Za-z0-9@#$%^&*._-]{4,}/gi, 'credential-like value'],
    [/:\/\/[^/\s:@]+:[^@\s/]+@/g, 'credential embedded in URL'],
    [/\b20230808003\b/g, 'device serial number'],
    [/\b(?:5\.10\.4(?:-[a-z0-9-]+)?|6\.1\.118)\b/gi, 'non-public kernel identity'],
    [/"kernel(?:Build)?"\s*:/gi, 'public kernel field'],
    [/<th>Kernel<\/th>|OS\s*\/\s*Kernel/gi, 'public kernel column'],
  ];
  for (const file of files) {
    if (path.basename(file) === 'SHA256SUMS' || !textFile(file)) continue;
    const text = fs.readFileSync(file, 'utf8');
    for (const [pattern, description] of forbidden) {
      pattern.lastIndex = 0;
      if (pattern.test(text)) fail(`${description} found in ${label} ${relativeTo(packRoot, file)}`);
    }
  }
}

function validateChecksums(packRoot, label) {
  const checksumPath = path.join(packRoot, 'SHA256SUMS');
  if (!fs.existsSync(checksumPath)) {
    fail(`${label} is missing`);
    return;
  }
  const files = walk(packRoot);
  const expected = new Set(files
    .filter((file) => path.resolve(file) !== path.resolve(checksumPath))
    .map((file) => relativeTo(packRoot, file)));
  const declared = new Map();
  for (const line of fs.readFileSync(checksumPath, 'utf8').split(/\r?\n/).filter(Boolean)) {
    const match = line.match(/^([0-9a-f]{64})  (.+)$/);
    if (!match) {
      fail(`${label} contains an invalid line: ${line}`);
      continue;
    }
    const [, digest, target] = match;
    const normalized = path.posix.normalize(target);
    if (target !== normalized || path.posix.isAbsolute(target) || normalized === '..' || normalized.startsWith('../') || target.includes('\\')) {
      fail(`${label} contains an unsafe or non-canonical target: ${target}`);
      continue;
    }
    if (target === 'SHA256SUMS') fail(`${label} must not hash itself`);
    if (declared.has(target)) fail(`${label} contains a duplicate target: ${target}`);
    declared.set(target, digest);
  }
  compareSets(`${label} inventory`, new Set(declared.keys()), expected);
  for (const [target, digest] of declared) {
    const file = path.join(packRoot, ...target.split('/'));
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) continue;
    const actual = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    if (actual !== digest) fail(`${label} mismatch: ${target}`);
  }
}

function validateLinksAndLanguages(packRoot, files, generatedFallbackRoot) {
  for (const file of files.filter((entry) => entry.toLowerCase().endsWith('.html'))) {
    const text = fs.readFileSync(file, 'utf8');
    for (const match of text.matchAll(/(?:href|src)\s*=\s*["']([^"']+)["']/gi)) {
      validateRelativeLink(packRoot, file, decodeHtmlEntities(match[1]), 'HTML', generatedFallbackRoot);
    }
  }
  for (const file of files.filter((entry) => entry.toLowerCase().endsWith('.md'))) {
    const text = fs.readFileSync(file, 'utf8');
    for (const match of text.matchAll(/!?\[[^\]]*\]\(([^)\s]+)(?:\s+["'][^)]*)?\)/g)) {
      validateRelativeLink(packRoot, file, match[1].replace(/^<|>$/g, ''), 'Markdown', generatedFallbackRoot);
    }
    for (const match of text.matchAll(/<(?:a|img)\b[^>]*(?:href|src)=["']([^"']+)["']/gi)) {
      validateRelativeLink(packRoot, file, decodeHtmlEntities(match[1]), 'Markdown HTML', generatedFallbackRoot);
    }
  }
}

function validateJsonReferences(packRoot, file, value, generatedFallbackRoot) {
  const referenceKeys = new Set([
    '$schema', 'manifest', 'releaseManifest', 'environment', 'models', 'dataset', 'card', 'overview',
    'concurrentMixed', 'singleWorkload', 'vlmObservation', 'vlmObservations', 'caseSchema', 'canonicalSmallModelCases',
    'cases', 'canonical', 'summary', 'report', 'reportZhCn', 'metrics', 'command', 'testLog', 'results',
  ]);
  visit(value, null);
  function visit(node, key) {
    if (typeof node === 'string' && referenceKeys.has(key) && looksLikeArtifactReference(node)) {
      validateRelativeLink(packRoot, file, node, 'JSON', generatedFallbackRoot);
      return;
    }
    if (Array.isArray(node)) {
      for (const item of node) visit(item, key);
    } else if (node && typeof node === 'object') {
      for (const [childKey, child] of Object.entries(node)) visit(child, childKey);
    }
  }
}

function validateRelativeLink(packRoot, sourceFile, rawTarget, kind, generatedFallbackRoot) {
  if (!rawTarget || rawTarget.startsWith('#') || /^(?:https?:|data:|mailto:|tel:)/i.test(rawTarget)) return;
  if (/^(?:javascript:|file:|\\\\|\/\/)/i.test(rawTarget)) {
    fail(`${kind} link is not an allowed relative link in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  const withoutFragment = rawTarget.split('#')[0].split('?')[0];
  if (!withoutFragment) return;
  if (withoutFragment.includes('\\') || path.posix.isAbsolute(withoutFragment)) {
    fail(`${kind} link is not relative/POSIX in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  let decoded;
  try {
    decoded = decodeURIComponent(withoutFragment);
  } catch {
    fail(`${kind} link has invalid URL encoding in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  const resolved = path.resolve(path.dirname(sourceFile), ...decoded.split('/'));
  if (!inside(packRoot, resolved)) {
    fail(`${kind} link escapes the benchmark pack in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
    return;
  }
  if (fs.existsSync(resolved)) return;
  if (generatedFallbackRoot) {
    const sourceDirectoryRelative = path.relative(packRoot, path.dirname(sourceFile));
    const generatedTarget = path.resolve(generatedFallbackRoot, sourceDirectoryRelative, ...decoded.split('/'));
    if (inside(generatedFallbackRoot, generatedTarget) && fs.existsSync(generatedTarget)) return;
  }
  fail(`broken ${kind} link in ${relativeTo(packRoot, sourceFile)}: ${rawTarget}`);
}

function parseCaseId(caseId) {
  const match = /^(person|nohelmet|mixed-cv)-(\d+(?:\.\d+)?)fps-(\d+)ch$/.exec(caseId ?? '');
  if (!match) return null;
  return {
    workload: match[1] === 'person' ? 'person-detector' : match[1] === 'nohelmet' ? 'no-safety-helmet-analysis' : 'concurrent-mixed',
    targetFps: Number(match[2]),
    configuredChannels: Number(match[3]),
  };
}

function parseSourceCaseId(caseId) {
  const match = /^(person|nohelmet|dual-cv)-(\d+(?:\.\d+)?)fps-(\d+)ch$/.exec(caseId ?? '');
  if (!match) return null;
  return {
    workload: match[1] === 'person' ? 'person-detector' : match[1] === 'nohelmet' ? 'no-safety-helmet-analysis' : 'concurrent-mixed',
    targetFps: Number(match[2]),
    configuredChannels: Number(match[3]),
  };
}

function normalizeArchivedCase(value) {
  const normalized = JSON.parse(JSON.stringify(value));
  if (normalized.workload === 'safety-helmet-detector') normalized.workload = 'no-safety-helmet-analysis';
  if (normalized.workload === 'dual-detector') normalized.workload = 'concurrent-mixed';
  for (const step of normalized.steps ?? []) {
    for (const task of step.tasks ?? []) {
      if (task.name === 'safety-helmet-detector') task.name = 'no-safety-helmet-analysis';
    }
  }
  return normalized;
}

function normalizeArchivedPlatformScope(platformId, scope) {
  return platformId === 'rv1126b' && scope === 'additional-experimental-platform' ? 'release-platform' : scope;
}

function parseArchiveArgument(args) {
  if (!args.length) return null;
  if (args.length !== 2 || args[0] !== '--archive' || !args[1]) {
    throw new Error('usage: validate-public-v1.1-multistream-benchmark.mjs [--archive <path>]');
  }
  return args[1];
}

function expectObjectKeys(value, requiredKeys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail(`${label} must be an object`);
    return;
  }
  const allowed = new Set(requiredKeys);
  for (const key of requiredKeys) if (!(key in value)) fail(`${label} is missing ${key}`);
  for (const key of Object.keys(value)) if (!allowed.has(key)) fail(`${label} contains unexpected property ${key}`);
}

function requireFile(packRoot, relativePath) {
  const file = path.join(packRoot, ...relativePath.split('/'));
  if (!fs.existsSync(file) || !fs.statSync(file).isFile()) fail(`missing required file: ${relativePath}`);
}

function readJsonIfPresent(packRoot, relativePath) {
  const file = path.join(packRoot, ...relativePath.split('/'));
  if (!fs.existsSync(file)) return null;
  return readJsonWithError(file);
}

function readJsonWithError(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) {
    fail(`invalid JSON: ${relativeTo(root, file)} (${error.message})`);
    return null;
  }
}

function compareSets(label, actual, expected) {
  for (const item of expected) if (!actual.has(item)) fail(`${label} is missing: ${item}`);
  for (const item of actual) if (!expected.has(item)) fail(`${label} contains unexpected entry: ${item}`);
}

function sameArray(actual, expected) {
  return actual.length === expected.length && actual.every((value, index) => Object.is(value, expected[index]));
}

function deepEqual(actual, expected) {
  return JSON.stringify(actual) === JSON.stringify(expected);
}

function positiveInteger(value) {
  return Number.isInteger(value) && value > 0;
}

function positiveNumber(value) {
  return Number.isFinite(value) && value > 0;
}

function nullableNonNegativeNumber(value) {
  return value === null || (Number.isFinite(value) && value >= 0);
}

function sha1(value) {
  return typeof value === 'string' && /^[0-9a-f]{40}$/.test(value);
}

function sha256(value) {
  return typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
}

function inside(directory, file) {
  const relativePath = path.relative(directory, file);
  return relativePath === '' || (!relativePath.startsWith(`..${path.sep}`) && relativePath !== '..' && !path.isAbsolute(relativePath));
}

function relativeTo(packRoot, file) {
  return path.relative(packRoot, file).replaceAll('\\', '/');
}

function walk(directory) {
  if (!fs.existsSync(directory)) return [];
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}

function textFile(file) {
  return /\.(?:md|html|json|txt|log|csv|tsv|xml|css|js|cjs|mjs|yml|yaml|toml|ini|cfg|svg|sh)$/i.test(file);
}

function looksLikeArtifactReference(value) {
  return value.endsWith('/') || /\.(?:html?|json|md|txt|log|yml|yaml|svg|csv)$/i.test(value.split('#')[0].split('?')[0]);
}

function decodeHtmlEntities(value) {
  return value.replaceAll('&amp;', '&').replaceAll('&#38;', '&');
}

function fail(message) {
  errors.push(message);
}
