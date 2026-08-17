#!/usr/bin/env node

import { spawnSync } from 'node:child_process';

const scriptArgs = process.argv.slice(2);
if (scriptArgs.length === 0) {
  console.error('Usage: node scripts/run-python.mjs <script.py> [args...]');
  process.exit(2);
}

const configuredPython = process.env.PYTHON?.trim();
const candidates = configuredPython
  ? [[configuredPython, []]]
  : process.platform === 'win32'
    ? [['py', ['-3']], ['python', []], ['python3', []]]
    : [['python3', []], ['python', []]];

for (const [executable, prefixArgs] of candidates) {
  const probe = spawnSync(executable, [...prefixArgs, '--version'], {
    encoding: 'utf8',
    windowsHide: true,
  });
  if (probe.status !== 0) continue;

  const result = spawnSync(executable, [...prefixArgs, ...scriptArgs], {
    stdio: 'inherit',
    windowsHide: true,
  });
  if (result.error) {
    console.error(`Failed to run ${executable}: ${result.error.message}`);
    process.exit(1);
  }
  if (result.signal) {
    console.error(`${executable} terminated by signal ${result.signal}`);
    process.exit(1);
  }
  process.exit(result.status ?? 1);
}

console.error(
  configuredPython
    ? `PYTHON does not name a working Python 3 interpreter: ${configuredPython}`
    : 'Python 3 was not found. Install Python 3 or set PYTHON to its executable path.',
);
process.exit(1);
