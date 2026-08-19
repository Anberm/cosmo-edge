import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const workspace = path.resolve(here, '..');
const root = path.join(workspace, 'docs', 'benchmarks', 'scenario-bench', 'v1.1');

const errors = [];
const officialPlatforms = ['bm1688', 'cv186x', 'rk3576'];
const experimentalPlatforms = ['rv1126b'];
const platforms = [...officialPlatforms, ...experimentalPlatforms];
const attachmentFiles = ['summary.json', 'metrics.json', 'command.txt', 'test.log', 'report.html', 'report.zh-CN.html'];

const expectedCases = {
  bm1688: [
    'person-24fps-8ch', 'person-10fps-16ch', 'person-7fps-16ch', 'person-5fps-16ch',
    'nohelmet-24fps-8ch', 'nohelmet-10fps-16ch', 'nohelmet-7fps-16ch', 'nohelmet-5fps-16ch',
    'dual-cv-5fps-16ch',
  ],
  cv186x: [
    'person-24fps-8ch', 'person-24fps-16ch', 'person-10fps-8ch', 'person-10fps-16ch',
    'person-7fps-8ch', 'person-7fps-16ch', 'person-5fps-8ch', 'person-5fps-16ch',
    'nohelmet-24fps-8ch', 'nohelmet-10fps-8ch', 'nohelmet-10fps-16ch',
    'nohelmet-7fps-8ch', 'nohelmet-7fps-16ch', 'nohelmet-5fps-8ch', 'nohelmet-5fps-16ch',
    'dual-cv-5fps-8ch',
  ],
  rk3576: [
    'person-24fps-8ch', 'person-10fps-8ch', 'person-10fps-16ch',
    'person-7fps-8ch', 'person-7fps-16ch', 'person-5fps-8ch', 'person-5fps-16ch',
    'nohelmet-24fps-8ch', 'nohelmet-10fps-8ch', 'nohelmet-10fps-16ch',
    'nohelmet-7fps-8ch', 'nohelmet-7fps-16ch', 'nohelmet-5fps-8ch', 'nohelmet-5fps-16ch',
    'dual-cv-5fps-8ch',
  ],
  rv1126b: [
    'person-24fps-4ch', 'person-10fps-4ch', 'person-7fps-4ch', 'person-5fps-4ch',
    'nohelmet-24fps-4ch', 'nohelmet-10fps-4ch', 'nohelmet-7fps-4ch', 'nohelmet-5fps-4ch',
    'dual-cv-5fps-4ch',
  ],
};

const expectedCaseCount = Object.values(expectedCases).reduce((count, cases) => count + cases.length, 0);
if (expectedCaseCount !== 49) errors.push(`validator case contract is invalid: expected 49, defined ${expectedCaseCount}`);

const required = [
  'README.md', 'README.zh-CN.md', 'RELEASE-CHECKLIST.md', 'release-manifest.json',
  'methodology.md', 'LICENSES.md', 'SHA256SUMS', 'report.html', 'report.zh-CN.html',
  'assets/capacity-overview.svg', 'assets/throughput-curves.svg', 'assets/resource-peaks.svg',
  'assets/capacity-overview.zh-CN.svg', 'assets/throughput-curves.zh-CN.svg', 'assets/resource-peaks.zh-CN.svg',
  'dataset/dataset-card.md', 'dataset/download-samples.sh',
  'models/model-card.md', 'models/io-contract.json', 'models/download-model.sh',
  'scenarios/README.md', 'scenarios/single-detector/README.md',
  'scenarios/single-detector/person-detector.public.yml',
  'scenarios/single-detector/safety-helmet-detector.public.yml',
  'scenarios/dual-detector/scenario.public.yml', 'scenarios/vlm-observation/scenario.public.yml',
  'results/index.json', 'results/cases.json', 'results/workload-matrix.json',
];

for (const platform of platforms) {
  required.push(
    `environments/${platform}.json`,
    `models/${platform}.json`,
    `results/${platform}/environment.json`,
    `results/${platform}/cases/index.json`,
  );
  for (const file of attachmentFiles) required.push(`results/${platform}/${file}`);
  for (const workload of ['single-detector', 'dual-detector']) {
    for (const file of attachmentFiles) required.push(`results/${platform}/${workload}/${file}`);
  }
  if (officialPlatforms.includes(platform)) {
    for (const file of attachmentFiles) required.push(`results/${platform}/vlm-observation/${file}`);
  }
  for (const caseId of expectedCases[platform]) {
    for (const file of attachmentFiles) required.push(`results/${platform}/cases/${caseId}/${file}`);
  }
}

if (!fs.existsSync(root)) {
  errors.push(`benchmark pack root is missing: ${root}`);
} else {
  for (const relativePath of required) requireFile(relativePath);
}

const allFiles = fs.existsSync(root) ? walk(root) : [];
const manifest = readJsonIfPresent('release-manifest.json');
if (manifest) validateManifest(manifest);

for (const file of allFiles.filter((entry) => entry.toLowerCase().endsWith('.json'))) {
  let value;
  try {
    value = readJson(file);
  } catch (error) {
    errors.push(`invalid JSON: ${relative(file)} (${error.message})`);
    continue;
  }
  validateJsonReferences(file, value);
}

validateCaseIndexes();
validateCaseArtifacts();
validateVlmEvidence();
validatePublicScrub(allFiles);
validateChecksums(allFiles);
validateLinksAndLanguages(allFiles);

if (errors.length) {
  console.error(`Validation failed (${errors.length}):`);
  for (const error of errors) console.error(`- ${error}`);
  process.exit(1);
}

console.log(
  `Validation passed: ${allFiles.length} files, ${platforms.length} platforms, ` +
  `${expectedCaseCount} small-model cases, retained VLM evidence, relative links, public scrub, and complete checksums verified.`,
);

function validateManifest(value) {
  if (value.release?.publicationState !== 'prepared-not-published') {
    errors.push('manifest release.publicationState must be prepared-not-published');
  }
  if (!/^[0-9a-f]{40}$/.test(value.sourceBaseline?.commit ?? '')) {
    errors.push('manifest sourceBaseline.commit must be a frozen 40-character Git commit');
  }
  if (!/^[0-9a-f]{40}$/.test(value.sourceBaseline?.tree ?? '')) {
    errors.push('manifest sourceBaseline.tree must be a frozen 40-character Git tree');
  }
  if (value.qualification?.benchmarkPackComplete !== true) {
    errors.push('manifest qualification.benchmarkPackComplete must be true');
  }
  if (!/^[0-9a-f]{64}$/.test(value.dataset?.sha256 ?? '')) {
    errors.push('manifest dataset.sha256 must be a 64-character SHA-256');
  }

  const manifestPlatforms = Array.isArray(value.platforms) ? value.platforms : [];
  const actualIds = manifestPlatforms.map((item) => item?.id).filter(Boolean).sort();
  const expectedIds = [...platforms].sort();
  if (!sameArray(actualIds, expectedIds)) {
    errors.push(`manifest platform inventory must be exactly: ${expectedIds.join(', ')}`);
  }
}

function validateCaseIndexes() {
  const globalIndex = readJsonIfPresent('results/cases.json');
  const expectedKeys = new Set(platforms.flatMap((platform) => expectedCases[platform].map((caseId) => `${platform}/${caseId}`)));
  if (globalIndex) {
    if (globalIndex.caseCount !== expectedCaseCount) errors.push(`results/cases.json caseCount must be ${expectedCaseCount}`);
    const cases = Array.isArray(globalIndex.cases) ? globalIndex.cases : [];
    const actualKeys = cases.map((item) => `${item?.platformId}/${item?.caseId}`);
    compareSets('results/cases.json case inventory', new Set(actualKeys), expectedKeys);
    if (actualKeys.length !== new Set(actualKeys).size) errors.push('results/cases.json contains duplicate cases');
    for (const item of cases) {
      if (!item || !expectedKeys.has(`${item.platformId}/${item.caseId}`)) continue;
      const prefix = `${item.platformId}/cases/${item.caseId}`;
      if (item.summary !== `${prefix}/summary.json`) errors.push(`incorrect summary link for ${item.platformId}/${item.caseId}`);
      if (item.report !== `${prefix}/report.html`) errors.push(`incorrect English report link for ${item.platformId}/${item.caseId}`);
      if (item.reportZhCn !== `${prefix}/report.zh-CN.html`) errors.push(`incorrect Chinese report link for ${item.platformId}/${item.caseId}`);
    }
  }

  const resultsIndex = readJsonIfPresent('results/index.json');
  if (resultsIndex) {
    const actual = new Map((Array.isArray(resultsIndex.platforms) ? resultsIndex.platforms : [])
      .map((item) => [platformIdFromOverview(item?.overview), item]));
    if (actual.size !== platforms.length || platforms.some((platform) => !actual.has(platform))) {
      errors.push(`results/index.json must contain exactly ${platforms.length} platforms`);
    }
    for (const platform of platforms) {
      const item = actual.get(platform);
      if (!item) continue;
      const expectedScope = officialPlatforms.includes(platform) ? 'release-platform' : 'additional-experimental-platform';
      if (item.scope !== expectedScope) errors.push(`results/index.json scope is incorrect for ${platform}`);
      if (item.cases !== `${platform}/cases/index.json`) errors.push(`results/index.json cases link is incorrect for ${platform}`);
      if (officialPlatforms.includes(platform) && item.vlmObservation !== `${platform}/vlm-observation/summary.json`) {
        errors.push(`results/index.json VLM link is incorrect for ${platform}`);
      }
      if (platform === 'rv1126b' && item.vlmObservation !== null) errors.push('RV1126B must not expose a VLM observation link');
    }
  }

  for (const platform of platforms) {
    const index = readJsonIfPresent(`results/${platform}/cases/index.json`);
    if (!index) continue;
    if (index.platformId !== platform) errors.push(`case index platformId is incorrect for ${platform}`);
    if (index.caseCount !== expectedCases[platform].length) errors.push(`${platform} case index count must be ${expectedCases[platform].length}`);
    const cases = Array.isArray(index.cases) ? index.cases : [];
    const ids = cases.map((item) => typeof item === 'string' ? item : item?.caseId);
    compareSets(`${platform} case index`, new Set(ids), new Set(expectedCases[platform]));
    if (ids.length !== new Set(ids).size) errors.push(`${platform} case index contains duplicate cases`);
  }
}

function validateCaseArtifacts() {
  for (const platform of platforms) {
    const casesRoot = path.join(root, 'results', platform, 'cases');
    if (fs.existsSync(casesRoot)) {
      const actualDirectories = fs.readdirSync(casesRoot, { withFileTypes: true })
        .filter((entry) => entry.isDirectory())
        .map((entry) => entry.name);
      compareSets(`${platform} case directories`, new Set(actualDirectories), new Set(expectedCases[platform]));
    }

    const enPlatformReport = readTextIfPresent(`results/${platform}/report.html`);
    const zhPlatformReport = readTextIfPresent(`results/${platform}/report.zh-CN.html`);
    for (const caseId of expectedCases[platform]) {
      const summaryPath = `results/${platform}/cases/${caseId}/summary.json`;
      const summary = readJsonIfPresent(summaryPath);
      const identity = parseCaseId(caseId);
      if (summary) {
        if (summary.platformId !== platform) errors.push(`${summaryPath} platformId is incorrect`);
        if (summary.caseId !== caseId) errors.push(`${summaryPath} caseId is incorrect`);
        if (summary.workload !== identity.workload) errors.push(`${summaryPath} workload is incorrect`);
        if (summary.targetFps !== identity.targetFps) errors.push(`${summaryPath} targetFps is incorrect`);
        if (summary.configuredChannels !== identity.configuredChannels) errors.push(`${summaryPath} configuredChannels is incorrect`);
      }
      for (const file of attachmentFiles) {
        const relativePath = `results/${platform}/cases/${caseId}/${file}`;
        const absolutePath = path.join(root, ...relativePath.split('/'));
        if (fs.existsSync(absolutePath) && fs.statSync(absolutePath).size === 0) errors.push(`empty case attachment: ${relativePath}`);
      }
      if (enPlatformReport && !enPlatformReport.includes(`cases/${caseId}/report.html`)) {
        errors.push(`English ${platform} overview does not link case report: ${caseId}`);
      }
      if (zhPlatformReport && !zhPlatformReport.includes(`cases/${caseId}/report.zh-CN.html`)) {
        errors.push(`Chinese ${platform} overview does not link case report: ${caseId}`);
      }
    }
  }
}

function validateVlmEvidence() {
  const expectedSeries = {
    bm1688: [0.100, 0.100, 0.100, 0.100, 0.060, 0.100, 0.080, 0.040],
    cv186x: [0.120, 0.100, 0.100, 0.100, 0.060, 0.060, 0.060, 0.080],
    rk3576: [0.100, 0.120, 0.116, 0.115, 0.091, 0.076, 0.063, 0.057],
  };
  for (const platform of officialPlatforms) {
    const summary = readJsonIfPresent(`results/${platform}/vlm-observation/summary.json`);
    if (!summary) continue;
    const steps = Array.isArray(summary.steps) ? summary.steps : [];
    const series = steps.map((step) => step.observedEquivalentPerChannelFps);
    if (!sameArray(series, expectedSeries[platform])) errors.push(`${platform} retained VLM FPS series changed`);
    if (!sameArray(steps.map((step) => step.channels), [1, 2, 3, 4, 5, 6, 7, 8])) errors.push(`${platform} retained VLM channel staircase changed`);
    if (summary.workload?.targetFpsPerChannel !== 0.1 || steps.some((step) => step.targetFpsPerChannel !== 0.1)) {
      errors.push(`${platform} retained VLM target FPS must remain 0.1 per channel`);
    }
    if (steps.some((step) => step.fpsGateEnabled !== false)) errors.push(`${platform} retained VLM FPS gate semantics changed`);
  }
  const rvVlm = path.join(root, 'results', 'rv1126b', 'vlm-observation');
  if (fs.existsSync(rvVlm)) errors.push('RV1126B must not contain a VLM observation directory');
}

function validatePublicScrub(files) {
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
    for (const [pattern, label] of forbidden) {
      pattern.lastIndex = 0;
      if (pattern.test(text)) errors.push(`${label} found in ${relative(file)}`);
    }
  }
}

function validateChecksums(files) {
  const checksumPath = path.join(root, 'SHA256SUMS');
  if (!fs.existsSync(checksumPath)) return;

  const expected = new Set(files
    .filter((file) => path.resolve(file) !== path.resolve(checksumPath))
    .map((file) => path.relative(root, file).replaceAll('\\', '/')));
  const declared = new Map();
  const lines = fs.readFileSync(checksumPath, 'utf8').split(/\r?\n/).filter((line) => line.length > 0);
  for (const line of lines) {
    const match = line.match(/^([0-9a-f]{64})  (.+)$/);
    if (!match) {
      errors.push(`invalid SHA256SUMS line: ${line}`);
      continue;
    }
    const [, digest, target] = match;
    const normalized = path.posix.normalize(target);
    if (target !== normalized || path.posix.isAbsolute(target) || normalized === '..' || normalized.startsWith('../') || target.includes('\\')) {
      errors.push(`unsafe or non-canonical SHA256SUMS target: ${target}`);
      continue;
    }
    if (target === 'SHA256SUMS') errors.push('SHA256SUMS must not hash itself');
    if (declared.has(target)) errors.push(`duplicate SHA256SUMS target: ${target}`);
    declared.set(target, digest);
  }

  compareSets('SHA256SUMS file inventory', new Set(declared.keys()), expected);
  for (const [target, digest] of declared) {
    const file = path.join(root, ...target.split('/'));
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) continue;
    const actual = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    if (actual !== digest) errors.push(`checksum mismatch: ${target}`);
  }
}

function validateLinksAndLanguages(files) {
  for (const file of files.filter((entry) => entry.toLowerCase().endsWith('.html'))) {
    const text = fs.readFileSync(file, 'utf8');
    const lang = text.match(/<html\b[^>]*\blang\s*=\s*["']([^"']+)["']/i)?.[1];
    const expectedLang = file.toLowerCase().endsWith('.zh-cn.html') ? 'zh-CN' : 'en';
    if (lang !== expectedLang) errors.push(`incorrect HTML lang in ${relative(file)}: expected ${expectedLang}, found ${lang ?? 'missing'}`);
    if (!/<\/html>\s*$/i.test(text)) errors.push(`incomplete HTML document: ${relative(file)}`);
    for (const match of text.matchAll(/(?:href|src)\s*=\s*["']([^"']+)["']/gi)) {
      validateRelativeLink(file, decodeHtmlEntities(match[1]), 'HTML');
    }
  }

  for (const file of files.filter((entry) => entry.toLowerCase().endsWith('.md'))) {
    const text = fs.readFileSync(file, 'utf8');
    for (const match of text.matchAll(/!?\[[^\]]*\]\(([^)\s]+)(?:\s+["'][^)]*)?\)/g)) {
      validateRelativeLink(file, match[1].replace(/^<|>$/g, ''), 'Markdown');
    }
  }
}

function validateJsonReferences(file, value) {
  const referenceKeys = new Set([
    'manifest', 'releaseManifest', 'environment', 'models', 'dataset', 'overview',
    'dualDetector', 'singleDetector', 'vlmObservation', 'cases', 'summary',
    'report', 'reportZhCn', 'metrics', 'command', 'testLog', 'results',
  ]);
  visit(value, null);

  function visit(node, key) {
    if (typeof node === 'string' && referenceKeys.has(key) && looksLikeArtifactReference(node)) {
      validateRelativeLink(file, node, 'JSON');
      return;
    }
    if (Array.isArray(node)) {
      for (const item of node) visit(item, key);
      return;
    }
    if (node && typeof node === 'object') {
      for (const [childKey, child] of Object.entries(node)) visit(child, childKey);
    }
  }
}

function looksLikeArtifactReference(value) {
  return value.includes('/') && (
    value.endsWith('/') ||
    /\.(?:html?|json|md|txt|log|yml|yaml|svg|csv)$/i.test(value.split('#')[0].split('?')[0])
  );
}

function validateRelativeLink(sourceFile, rawTarget, kind) {
  if (!rawTarget || rawTarget.startsWith('#')) return;
  if (/^(?:https?:|data:|mailto:|tel:)/i.test(rawTarget)) return;
  if (/^(?:javascript:|file:|\\\\|\/\/)/i.test(rawTarget)) {
    errors.push(`${kind} link is not an allowed relative link in ${relative(sourceFile)}: ${rawTarget}`);
    return;
  }
  const withoutFragment = rawTarget.split('#')[0].split('?')[0];
  if (!withoutFragment) return;
  if (withoutFragment.includes('\\') || path.posix.isAbsolute(withoutFragment)) {
    errors.push(`${kind} link is not relative/POSIX in ${relative(sourceFile)}: ${rawTarget}`);
    return;
  }
  let decoded;
  try {
    decoded = decodeURIComponent(withoutFragment);
  } catch {
    errors.push(`${kind} link has invalid URL encoding in ${relative(sourceFile)}: ${rawTarget}`);
    return;
  }
  const resolved = path.resolve(path.dirname(sourceFile), ...decoded.split('/'));
  if (!insideRoot(resolved)) {
    errors.push(`${kind} link escapes the benchmark pack in ${relative(sourceFile)}: ${rawTarget}`);
    return;
  }
  if (!fs.existsSync(resolved)) errors.push(`broken ${kind} link in ${relative(sourceFile)}: ${rawTarget}`);
}

function parseCaseId(caseId) {
  const match = /^(person|nohelmet|dual-cv)-(\d+)fps-(\d+)ch$/.exec(caseId);
  if (!match) return { workload: null, targetFps: null, configuredChannels: null };
  return {
    workload: match[1] === 'person' ? 'person-detector' : match[1] === 'nohelmet' ? 'safety-helmet-detector' : 'dual-detector',
    targetFps: Number(match[2]),
    configuredChannels: Number(match[3]),
  };
}

function platformIdFromOverview(value) {
  const match = /^(bm1688|cv186x|rk3576|rv1126b)\/summary\.json$/.exec(value ?? '');
  return match?.[1] ?? null;
}

function readJsonIfPresent(relativePath) {
  const file = path.join(root, ...relativePath.split('/'));
  if (!fs.existsSync(file)) return null;
  try {
    return readJson(file);
  } catch {
    return null;
  }
}

function readTextIfPresent(relativePath) {
  const file = path.join(root, ...relativePath.split('/'));
  return fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : null;
}

function requireFile(relativePath) {
  const file = path.join(root, ...relativePath.split('/'));
  if (!fs.existsSync(file) || !fs.statSync(file).isFile()) errors.push(`missing required file: ${relativePath}`);
}

function compareSets(label, actual, expected) {
  for (const item of expected) if (!actual.has(item)) errors.push(`${label} is missing: ${item}`);
  for (const item of actual) if (!expected.has(item)) errors.push(`${label} contains unexpected entry: ${item}`);
}

function sameArray(actual, expected) {
  return actual.length === expected.length && actual.every((value, index) => Object.is(value, expected[index]));
}

function insideRoot(file) {
  const relativePath = path.relative(root, file);
  return relativePath === '' || (!relativePath.startsWith('..' + path.sep) && relativePath !== '..' && !path.isAbsolute(relativePath));
}

function walk(dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function relative(file) {
  return path.relative(workspace, file).replaceAll('\\', '/');
}

function textFile(file) {
  return /\.(?:md|html|json|txt|log|csv|tsv|xml|css|js|cjs|mjs|yml|yaml|toml|ini|cfg|svg|sh)$/i.test(file);
}

function decodeHtmlEntities(value) {
  return value.replaceAll('&amp;', '&').replaceAll('&#38;', '&');
}
