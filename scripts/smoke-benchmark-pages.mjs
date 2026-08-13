import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const distRoot = resolve(repositoryRoot, process.argv[2] ?? 'docs/.vitepress/dist');
const benchmarkRoot = join(distRoot, 'benchmarks', 'scenario-bench', 'v1.1');
const platforms = ['bm1688', 'cv186x', 'rk3576'];
const workloads = ['single-detector', 'dual-detector', 'vlm-observation'];
const failures = [];
const expectedReports = ['report.html', 'report.zh-CN.html'];

for (const platform of platforms) {
  expectedReports.push(`results/${platform}/report.html`, `results/${platform}/report.zh-CN.html`);
  for (const workload of workloads) {
    expectedReports.push(
      `results/${platform}/${workload}/report.html`,
      `results/${platform}/${workload}/report.zh-CN.html`,
    );
  }
}

for (const relative of expectedReports) {
  const file = join(benchmarkRoot, ...relative.split('/'));
  if (!existsSync(file)) {
    failures.push(`${relative}: generated report is missing`);
    continue;
  }
  const html = readFileSync(file, 'utf8');
  if (!/<html\b[^>]*>/iu.test(html) || !/<title>[^<]+<\/title>/iu.test(html)) {
    failures.push(`${relative}: generated file is not a complete HTML report`);
  }
}

for (const main of ['report.html', 'report.zh-CN.html']) {
  const file = join(benchmarkRoot, main);
  if (!existsSync(file)) continue;
  const html = readFileSync(file, 'utf8');
  const reportName = main === 'report.zh-CN.html' ? 'report.zh-CN.html' : 'report.html';
  for (const platform of platforms) {
    for (const workload of workloads) {
      const target = `results/${platform}/${workload}/${reportName}`;
      if (!html.includes(`href="${target}"`)) {
        failures.push(`${main}: missing standalone-report link ${target}`);
      } else if (!existsSync(join(benchmarkRoot, ...target.split('/')))) {
        failures.push(`${main}: standalone-report target is missing ${target}`);
      }
    }
  }
}

for (const platform of platforms) {
  for (const reportName of ['report.html', 'report.zh-CN.html']) {
    const relative = `results/${platform}/${reportName}`;
    const html = readFileSync(join(benchmarkRoot, ...relative.split('/')), 'utf8');
    for (const workload of workloads) {
      const target = `${workload}/${reportName}`;
      if (!html.includes(`href="${target}"`)) {
        failures.push(`${relative}: missing workload-report link ${target}`);
      }
    }
  }
}

for (const relative of expectedReports) {
  const html = readFileSync(join(benchmarkRoot, ...relative.split('/')), 'utf8');
  const tables = html.match(/<table\b/giu)?.length ?? 0;
  const wrappers = html.match(/<div class="table"/giu)?.length ?? 0;
  if (tables !== wrappers) failures.push(`${relative}: ${tables} table(s) but ${wrappers} responsive wrapper(s)`);
  if (!html.includes('class="report-nav"')) failures.push(`${relative}: report navigation is missing`);
}

if (failures.length > 0) {
  console.error(`Benchmark page smoke test failed with ${failures.length} issue(s):`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(`Benchmark page smoke test passed: ${expectedReports.length} bilingual rendered reports, responsive tables, and cross-report navigation.`);
