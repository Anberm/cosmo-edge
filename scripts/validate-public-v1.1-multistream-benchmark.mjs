import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const workspace = path.resolve(here, '..');
const root = path.join(workspace, 'docs', 'benchmarks', 'scenario-bench', 'v1.1');
const staticAssetCopier = path.join(workspace, 'scripts', 'copy-static-benchmark-assets.mjs');
const repositoryModelRoots = {
  bm1688: path.join(workspace, 'data', 'resource', 'aiboxresource_bm1688', 'models'),
  cv186x: path.join(workspace, 'data', 'resource', 'aiboxresource_cv186x', 'models'),
};

const errors = [];
const required = [
  'README.md', 'README.zh-CN.md', 'RELEASE-CHECKLIST.md', 'release-manifest.json',
  'methodology.md', 'LICENSES.md', 'SHA256SUMS', 'report.html', 'report.zh-CN.html',
  'assets/capacity-overview.svg', 'assets/throughput-curves.svg', 'assets/resource-peaks.svg',
  'assets/capacity-overview.zh-CN.svg', 'assets/throughput-curves.zh-CN.svg', 'assets/resource-peaks.zh-CN.svg',
  'dataset/dataset-card.md', 'models/model-card.md', 'results/index.json', 'results/workload-matrix.json',
];

for (const platform of ['bm1688', 'cv186x', 'rk3576']) {
  required.push(
    `environments/${platform}.json`,
    `models/${platform}.json`,
    `results/${platform}/summary.json`,
    `results/${platform}/report.html`,
    `results/${platform}/report.zh-CN.html`,
  );
  for (const workload of ['single-detector', 'dual-detector', 'vlm-observation']) {
    for (const file of ['summary.json', 'metrics.json', 'command.txt', 'test.log', 'report.html', 'report.zh-CN.html']) {
      required.push(`results/${platform}/${workload}/${file}`);
    }
  }
}

for (const relative of required) {
  if (!fs.existsSync(path.join(root, relative))) errors.push(`missing required file: ${relative}`);
}

const manifest = readJson(path.join(root, 'release-manifest.json'));
if (manifest.manifestStatus !== 'frozen-publication-ready') errors.push('manifest status must be frozen-publication-ready');
if (manifest.doNotPublish !== false) errors.push('doNotPublish must be false for the final public benchmark');
if (manifest.release?.publicationState !== 'prepared-not-published') errors.push('manifest publicationState must be prepared-not-published before external publication');
if (manifest.qualification?.benchmarkReadyToPublish !== true) errors.push('benchmarkReadyToPublish must be true');
if (manifest.qualification?.productReleaseQualificationComplete !== false) errors.push('productReleaseQualificationComplete must remain false while declared product evidence is outstanding');
if (!/^[0-9a-f]{40}$/.test(manifest.sourceBaseline?.commit ?? '')) errors.push('source commit is not frozen');
if (!/^[0-9a-f]{40}$/.test(manifest.sourceBaseline?.tree ?? '')) errors.push('source tree is not frozen');
if (manifest.repositoryIntegration?.targetCommitAtPreparation !== manifest.sourceBaseline?.commit) errors.push('release-branch preparation commit does not match source baseline');
if (manifest.repositoryIntegration?.targetTreeAtPreparation !== manifest.sourceBaseline?.tree) errors.push('release-branch preparation tree does not match source baseline');
if (!/^[0-9a-f]{64}$/.test(manifest.dataset?.sha256 ?? '')) errors.push('dataset SHA-256 is missing');
if (!/^[0-9a-f]{64}$/.test(manifest.packageArtifacts?.open?.sha256 ?? '')) errors.push('BM1688/CV186X Open package SHA-256 is missing');
if (!/^[0-9a-f]{64}$/.test(manifest.packageArtifacts?.open?.engineSha256 ?? '')) errors.push('BM1688/CV186X running engine SHA-256 is missing');
if (manifest.packageArtifacts?.open?.profile !== 'public-runtime') errors.push('Open package profile is not public-runtime');

for (const file of walk(root).filter((entry) => entry.endsWith('.json'))) {
  try { readJson(file); } catch (error) { errors.push(`invalid JSON: ${relative(file)} (${error.message})`); }
}

for (const platform of ['bm1688', 'cv186x']) {
  const identities = readJson(path.join(root, 'models', `${platform}.json`)).models;
  for (const publicId of ['person-detector', 'safety-helmet-classifier']) {
    const identity = identities.find((model) => model.publicId === publicId);
    if (!identity) { errors.push(`${platform} model identity is missing: ${publicId}`); continue; }
    const repositoryPath = identity.repositoryPath ?? '';
    const modelPath = path.resolve(workspace, ...repositoryPath.split('/'), 'model.nn');
    const expectedRepositoryPrefix = `data/resource/aiboxresource_${platform}/models/`;
    const modelPrefix = `${path.resolve(repositoryModelRoots[platform])}${path.sep}`;
    if (!repositoryPath.startsWith(expectedRepositoryPrefix) || !modelPath.startsWith(modelPrefix)) {
      errors.push(`${platform} repository model path is invalid: ${publicId}`);
      continue;
    }
    if (!fs.existsSync(modelPath)) { errors.push(`${platform} repository model is missing: ${publicId}`); continue; }
    const bytes = fs.readFileSync(modelPath);
    const actual = crypto.createHash('sha256').update(bytes).digest('hex');
    if (actual !== identity.sha256) errors.push(`${platform} repository model hash mismatch: ${publicId}`);
    if (bytes.length !== identity.sizeBytes) errors.push(`${platform} repository model size mismatch: ${publicId}`);
  }
}

const forbidden = [
  [/\bhost(?:33|55|101)\b/gi, 'internal host alias'],
  [/\b(?:16064|11099|41773|7463|67093|78510)\b/g, 'internal algorithm ID'],
  [/\bLX\d{8,}\b/g, 'internal channel ID'],
  [/\b192\.168\.\d{1,3}\.\d{1,3}\b/g, 'private device address'],
  [/[A-Za-z]:[\\/](?:WorkSpace|Users)[\\/]/g, 'local absolute path'],
  [/\b(?:admin|root|linaro)\s+(?:password|密码|passwd)\s*[:=]\s*[^\s<]+/gi, 'credential-like text'],
  [/\b20230808003\b/g, 'device serial number'],
  [/\b(?:5\.10\.4(?:-[a-z0-9-]+)?|6\.1\.118)\b/gi, 'non-public kernel identity'],
  [/"kernel(?:Build)?"\s*:/gi, 'public kernel field'],
  [/<th>Kernel<\/th>|OS\s*\/\s*Kernel/gi, 'public kernel column'],
];

for (const file of walk(root)) {
  if (path.basename(file) === 'SHA256SUMS') continue;
  if (!textFile(file)) continue;
  const text = fs.readFileSync(file, 'utf8');
  for (const [pattern, label] of forbidden) {
    pattern.lastIndex = 0;
    if (pattern.test(text)) errors.push(`${label} found in ${relative(file)}`);
  }
}

const redistributedBinaryExtensions = new Set(['.mp4', '.mov', '.mkv', '.avi', '.nn', '.bmodel', '.rknn', '.rkllm', '.onnx']);
for (const file of walk(root)) {
  if (redistributedBinaryExtensions.has(path.extname(file).toLowerCase())) errors.push(`model or video binary must not be redistributed: ${relative(file)}`);
}

const checksumPath = path.join(root, 'SHA256SUMS');
if (fs.existsSync(checksumPath)) {
  for (const line of fs.readFileSync(checksumPath, 'utf8').split(/\r?\n/).filter(Boolean)) {
    const match = line.match(/^([0-9a-f]{64})  (.+)$/);
    if (!match) { errors.push(`invalid SHA256SUMS line: ${line}`); continue; }
    const checksumTarget = match[2].replaceAll('\\', '/');
    const targetParts = checksumTarget.split('/');
    if (path.isAbsolute(checksumTarget) || targetParts.some((part) => part === '..' || part === '')) {
      errors.push(`unsafe checksum target: ${match[2]}`);
      continue;
    }
    const file = path.join(root, ...targetParts);
    if (!fs.existsSync(file)) { errors.push(`checksum target missing: ${checksumTarget}`); continue; }
    const actual = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    if (actual !== match[1]) errors.push(`checksum mismatch: ${checksumTarget}`);
  }
}

for (const file of walk(root).filter((entry) => entry.endsWith('.html'))) {
  const text = fs.readFileSync(file, 'utf8');
  for (const match of text.matchAll(/(?:href|src)="([^"]+)"/g)) {
    const target = match[1];
    if (/^(?:https?:|data:|#)/.test(target)) continue;
    const resolved = path.resolve(path.dirname(file), target.split('#')[0]);
    if (!fs.existsSync(resolved)) errors.push(`broken HTML asset/link in ${relative(file)}: ${target}`);
  }
}

const zh = fs.readFileSync(path.join(root, 'report.zh-CN.html'), 'utf8');
for (const marker of ['方法与复现', '测试环境', '单算法容量矩阵', '实验结果：VLM 运行观测', '关联附件', 'assets/capacity-overview.zh-CN.svg', 'assets/throughput-curves.zh-CN.svg', 'assets/resource-peaks.zh-CN.svg']) {
  if (!zh.includes(marker)) errors.push(`Chinese report missing marker: ${marker}`);
}
if (!zh.includes('<html lang="zh-CN">')) errors.push('Chinese report lang attribute is incorrect');
for (const marker of ['results/bm1688/single-detector/report.zh-CN.html', 'results/cv186x/dual-detector/report.zh-CN.html', 'results/rk3576/vlm-observation/report.zh-CN.html', '<th>平台</th><th>板卡</th><th>操作系统</th><th>运行时 / 媒体链路</th>']) {
  if (!zh.includes(marker)) errors.push(`Chinese report missing localized marker: ${marker}`);
}

for (const file of walk(root).filter((entry) => entry.endsWith('.html'))) {
  const html = fs.readFileSync(file, 'utf8');
  const tables = html.match(/<table\b/giu)?.length ?? 0;
  const wrappers = html.match(/<div class="table"/giu)?.length ?? 0;
  if (tables !== wrappers) errors.push(`responsive table wrapper mismatch in ${relative(file)}: ${tables} table(s), ${wrappers} wrapper(s)`);
  if (!html.includes('class="report-nav"')) errors.push(`report navigation is missing in ${relative(file)}`);
  if (!html.includes('td[data-status="PASS"]')) errors.push(`status styling is missing in ${relative(file)}`);
}

for (const file of walk(root).filter((entry) => entry.endsWith('.zh-CN.html'))) {
  const html = fs.readFileSync(file, 'utf8');
  if (!html.includes('<html lang="zh-CN">')) errors.push(`Chinese attachment lang attribute is incorrect in ${relative(file)}`);
}

const packageJson = readJson(path.join(workspace, 'package.json'));
if (!fs.existsSync(staticAssetCopier)) errors.push('static benchmark asset copier is missing');
if (!packageJson.scripts?.['docs:build']?.includes('copy-static-benchmark-assets.mjs')) errors.push('docs:build does not publish static benchmark assets');
const gitignore = fs.readFileSync(path.join(workspace, '.gitignore'), 'utf8');
if (!gitignore.includes('!docs/benchmarks/scenario-bench/v1.1/**/*.log')) errors.push('sanitized benchmark logs are not explicitly included by .gitignore');

const rkVlm = readJson(path.join(root, 'results', 'rk3576', 'vlm-observation', 'summary.json'));
const expectedRkVlm = [0.100, 0.120, 0.116, 0.115, 0.091, 0.076, 0.063, 0.057];
const actualRkVlm = rkVlm.steps.map((step) => step.observedEquivalentPerChannelFps);
if (JSON.stringify(actualRkVlm) !== JSON.stringify(expectedRkVlm)) errors.push('RK3576 reviewed equivalent VLM FPS series changed');
if (rkVlm.steps.some((step) => step.targetFpsPerChannel !== 0.1 || step.fpsGateEnabled !== false)) errors.push('VLM target/gate wording is inconsistent');

if (errors.length) {
  console.error(`Validation failed (${errors.length}):`);
  for (const error of errors) console.error(`- ${error}`);
  process.exit(1);
}

console.log(`Validation passed: ${walk(root).length} files, publication-ready benchmark, checksums, binary-distribution, and public-scrub rules verified.`);

function walk(dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(dir, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}

function readJson(file) { return JSON.parse(fs.readFileSync(file, 'utf8')); }
function relative(file) { return path.relative(workspace, file).replaceAll('\\', '/'); }
function textFile(file) { return /\.(?:md|html|json|txt|log|yml|yaml|svg|sh|mjs)$/i.test(file); }
