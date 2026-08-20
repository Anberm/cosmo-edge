import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const distRoot = path.resolve(repositoryRoot, process.argv[2] ?? 'docs/.vitepress/dist');
const benchmarkRoot = path.join(distRoot, 'benchmarks', 'scenario-bench', 'v1.1');
const failures = [];

const manifest = readJson('release-manifest.json');
const vlm = readJson('results/vlm-observations.json');
const vlmPlatformIds = new Set((vlm?.observations ?? []).map((item) => item.platformId));
const platforms = (manifest?.platforms ?? []).map((definition) => ({
  ...definition,
  canonical: readJson(`results/${definition.id}/cases.json`),
}));

const expectedReports = ['report.html', 'report.zh-CN.html'];
for (const platform of platforms) {
  const cases = platform.canonical?.cases ?? [];
  for (const suffix of ['', '.zh-CN']) {
    expectedReports.push(
      `results/${platform.id}/report${suffix}.html`,
      `results/${platform.id}/cases/report${suffix}.html`,
      `results/${platform.id}/single-detector/report${suffix}.html`,
      `results/${platform.id}/dual-detector/report${suffix}.html`,
    );
    if (vlmPlatformIds.has(platform.id)) {
      expectedReports.push(`results/${platform.id}/vlm-observation/report${suffix}.html`);
    }
    for (const benchmarkCase of cases) {
      expectedReports.push(`results/${platform.id}/cases/${benchmarkCase.caseId}/report${suffix}.html`);
    }
  }
}

const actualReports = fs.existsSync(benchmarkRoot)
  ? walk(benchmarkRoot)
    .filter((file) => /^report(?:\.zh-cn)?\.html$/i.test(path.basename(file)))
    .map((file) => path.relative(benchmarkRoot, file).replaceAll('\\', '/'))
    .sort()
  : [];
compareSets('report inventory', new Set(actualReports), new Set(expectedReports));

for (const report of expectedReports) {
  const file = path.join(benchmarkRoot, ...report.split('/'));
  if (!fs.existsSync(file)) continue;
  const html = fs.readFileSync(file, 'utf8');
  if (!/<html\b[^>]*>/iu.test(html) || !/<title>[^<]+<\/title>/iu.test(html) || !/<\/html>\s*$/iu.test(html)) {
    failures.push(`${report}: generated file is not a complete HTML report`);
  }
  const expectedLang = report.endsWith('.zh-CN.html') ? 'zh-CN' : 'en';
  const actualLang = html.match(/<html\b[^>]*\blang=["']([^"']+)["']/iu)?.[1];
  if (actualLang !== expectedLang) failures.push(`${report}: expected lang ${expectedLang}, found ${actualLang ?? 'missing'}`);
  const tables = html.match(/<table\b/giu)?.length ?? 0;
  const wrappers = html.match(/<div class="table"/giu)?.length ?? 0;
  if (tables !== wrappers) failures.push(`${report}: ${tables} table(s) but ${wrappers} responsive wrapper(s)`);
  if (!html.includes('class="report-nav"')) failures.push(`${report}: report navigation is missing`);
  if (/\b(?:undefined|NaN)\b|\[object Object\]/u.test(html)) failures.push(`${report}: unresolved generated value`);
}

for (const reportName of ['report.html', 'report.zh-CN.html']) {
  const file = path.join(benchmarkRoot, reportName);
  if (!fs.existsSync(file)) continue;
  const html = fs.readFileSync(file, 'utf8');
  for (const platform of platforms) {
    for (const target of [
      `results/${platform.id}/${reportName}`,
      `results/${platform.id}/single-detector/${reportName}`,
      `results/${platform.id}/dual-detector/${reportName}`,
      `results/${platform.id}/cases/${reportName}`,
      ...(vlmPlatformIds.has(platform.id) ? [`results/${platform.id}/vlm-observation/${reportName}`] : []),
    ]) {
      if (!html.includes(`href="${target}"`)) failures.push(`${reportName}: missing report link ${target}`);
    }
  }
}

const expectedCaseCount = manifest?.evidence?.smallModelCaseCount;
const actualCaseCount = platforms.reduce((count, platform) => count + (platform.canonical?.cases?.length ?? 0), 0);
if (actualCaseCount !== expectedCaseCount) {
  failures.push(`canonical case count ${actualCaseCount} differs from manifest count ${expectedCaseCount}`);
}

if (failures.length) {
  console.error(`Benchmark page smoke test failed with ${failures.length} issue(s):`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(
  `Benchmark page smoke test passed: ${expectedReports.length} generated bilingual reports across ` +
  `${platforms.length} manifest-defined platforms and ${actualCaseCount} canonical cases.`,
);

function readJson(relativePath) {
  const file = path.join(benchmarkRoot, ...relativePath.split('/'));
  if (!fs.existsSync(file)) {
    failures.push(`${relativePath}: generated JSON is missing`);
    return null;
  }
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) {
    failures.push(`${relativePath}: invalid JSON (${error.message})`);
    return null;
  }
}

function compareSets(label, actual, expected) {
  for (const item of expected) if (!actual.has(item)) failures.push(`${label} is missing: ${item}`);
  for (const item of actual) if (!expected.has(item)) failures.push(`${label} contains unexpected entry: ${item}`);
}

function walk(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const full = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(full) : [full];
  });
}
