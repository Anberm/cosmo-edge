import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const distRoot = resolve(repositoryRoot, process.argv[2] ?? 'docs/.vitepress/dist');
const benchmarkRoot = join(distRoot, 'benchmarks', 'scenario-bench', 'v1.1');
const platforms = [
  { id: 'bm1688', vlm: true, cases: 9 },
  { id: 'cv186x', vlm: true, cases: 16 },
  { id: 'rk3576', vlm: true, cases: 15 },
  { id: 'rv1126b', vlm: false, cases: 9 },
];
const failures = [];
const expectedReports = ['report.html', 'report.zh-CN.html'];

for (const platform of platforms) {
  const workloads = ['single-detector', 'dual-detector', ...(platform.vlm ? ['vlm-observation'] : [])];
  expectedReports.push(
    `results/${platform.id}/report.html`,
    `results/${platform.id}/report.zh-CN.html`,
    `results/${platform.id}/cases/report.html`,
    `results/${platform.id}/cases/report.zh-CN.html`,
  );
  for (const workload of workloads) {
    expectedReports.push(
      `results/${platform.id}/${workload}/report.html`,
      `results/${platform.id}/${workload}/report.zh-CN.html`,
    );
  }

  const casesRoot = join(benchmarkRoot, 'results', platform.id, 'cases');
  const caseDirs = existsSync(casesRoot)
    ? readdirSync(casesRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory())
    : [];
  if (caseDirs.length !== platform.cases) {
    failures.push(`${platform.id}: expected ${platform.cases} case directories, found ${caseDirs.length}`);
  }
  for (const entry of caseDirs) {
    expectedReports.push(
      `results/${platform.id}/cases/${entry.name}/report.html`,
      `results/${platform.id}/cases/${entry.name}/report.zh-CN.html`,
    );
  }
}

for (const report of expectedReports) {
  const file = join(benchmarkRoot, ...report.split('/'));
  if (!existsSync(file)) {
    failures.push(`${report}: generated report is missing`);
    continue;
  }
  const html = readFileSync(file, 'utf8');
  if (!/<html\b[^>]*>/iu.test(html) || !/<title>[^<]+<\/title>/iu.test(html)) {
    failures.push(`${report}: generated file is not a complete HTML report`);
  }
  const tables = html.match(/<table\b/giu)?.length ?? 0;
  const wrappers = html.match(/<div class="table"/giu)?.length ?? 0;
  if (tables !== wrappers) failures.push(`${report}: ${tables} table(s) but ${wrappers} responsive wrapper(s)`);
  if (!html.includes('class="report-nav"')) failures.push(`${report}: report navigation is missing`);
}

for (const reportName of ['report.html', 'report.zh-CN.html']) {
  const html = readFileSync(join(benchmarkRoot, reportName), 'utf8');
  for (const platform of platforms) {
    for (const target of [
      `results/${platform.id}/${reportName}`,
      `results/${platform.id}/single-detector/${reportName}`,
      `results/${platform.id}/dual-detector/${reportName}`,
      `results/${platform.id}/cases/${reportName}`,
      ...(platform.vlm ? [`results/${platform.id}/vlm-observation/${reportName}`] : []),
    ]) {
      if (!html.includes(`href="${target}"`)) failures.push(`${reportName}: missing report link ${target}`);
    }
  }
}

if (failures.length) {
  console.error(`Benchmark page smoke test failed with ${failures.length} issue(s):`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(`Benchmark page smoke test passed: ${expectedReports.length} bilingual reports across 4 platforms and 49 independent cases.`);
