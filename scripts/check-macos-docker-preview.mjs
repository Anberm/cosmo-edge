import { readFileSync, statSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const paths = {
  compose: 'docker-compose.x86.macos.yml',
  dockerfile: 'Dockerfile.x86',
  cpuBuild: 'scripts/build_cpu.sh',
  runtimeStartup: 'scripts/run_start.sh',
  healthcheck: 'scripts/docker-healthcheck.x86.sh',
  launcher: 'scripts/macos-docker-preview.sh',
  dockerignore: '.dockerignore',
  docsZh: 'docs/guide/macos-docker-preview.md',
  docsEn: 'docs/en/guide/macos-docker-preview.md',
  readmeZh: 'README.zh-CN.md',
  readmeEn: 'README.md'
}

const failures = []

function read(relativePath) {
  return readFileSync(join(repositoryRoot, relativePath), 'utf8')
}

function requireText(relativePath, source, expected, label = expected) {
  if (!source.includes(expected)) failures.push(`${relativePath}: missing ${label}`)
}

function requireExecutable(relativePath) {
  if (process.platform === 'win32') return
  const mode = statSync(join(repositoryRoot, relativePath)).mode
  if ((mode & 0o111) === 0) failures.push(`${relativePath}: script must be executable`)
}

const compose = read(paths.compose)
const dockerfile = read(paths.dockerfile)
const cpuBuild = read(paths.cpuBuild)
const runtimeStartup = read(paths.runtimeStartup)
const healthcheck = read(paths.healthcheck)
const launcher = read(paths.launcher)
const dockerignore = read(paths.dockerignore)
const docsZh = read(paths.docsZh)
const docsEn = read(paths.docsEn)
const readmeZh = read(paths.readmeZh)
const readmeEn = read(paths.readmeEn)

for (const [expected, label] of [
  ['name: cosmo-x86-macos-preview', 'isolated Compose project name'],
  ['platform: linux/amd64', 'explicit linux/amd64 platform'],
  ['cosmo-x86-macos-data:/data/cwaiuserdata', 'isolated persistent data volume'],
  ['cosmo-x86-macos-app-resource:/appfs/cosmo_wander/cwai_data/resource', 'isolated resource volume'],
  ['./build_output/macos-x86:/build_output', 'isolated build output'],
  ['BUILD_ENV_IMAGE: "ghcr.io/cosmo-wander-ai/cosmo_edge-build-env_x86:v1@sha256:3345825c9255b9b73369af7ab6346c2cc7079786eb23309258acf7212ca03c1d"', 'pinned amd64 build environment'],
  ['RUNTIME_BASE_IMAGE: "debian:12-slim@sha256:a7ffa3fd2ba09498788cd398575f4340599626c37610af57cc368e70fd564d75"', 'pinned Debian runtime base'],
  ['COSMO_BUILD_JOBS: "${COSMO_X86_BUILD_JOBS:-1}"', 'serial emulation build default'],
  ['COSMO_STREAM_PLAY_MODE: httpflv-srs', 'deterministic loopback HTTP-FLV playback'],
  ['COSMO_STREAM_HTTP_PORT: "18088"', 'published SRS HTTP-FLV port'],
  ['name: cosmo-x86-macos-preview-data', 'stable Preview data volume name'],
  ['name: cosmo-x86-macos-preview-app-resource', 'stable Preview resource volume name'],
  ['test: ["CMD", "/usr/local/bin/cosmo-x86-healthcheck"]', 'container health check']
]) {
  requireText(paths.compose, compose, expected, label)
}

const publishedPorts = [...compose.matchAll(/^\s+-\s+"([^"\n]+:[0-9]+(?:\/udp)?)"\s*$/gmu)].map((match) => match[1])
if (publishedPorts.length !== 4) {
  failures.push(`${paths.compose}: expected 4 published ports, found ${publishedPorts.length}`)
}
for (const port of publishedPorts) {
  if (!port.startsWith('127.0.0.1:')) {
    failures.push(`${paths.compose}: host port is not loopback-only: ${port}`)
  }
}
if (/^\s+devices:/mu.test(compose) || compose.includes('/dev/video')) {
  failures.push(`${paths.compose}: USB or video devices must not be exposed by the Mac Preview`)
}
if (/^\s+cap_add:/mu.test(compose) || /^\s+sysctls:/mu.test(compose)) {
  failures.push(`${paths.compose}: the local-video Preview must not request network administration privileges`)
}
if (publishedPorts.some((port) => port.endsWith('/udp'))) {
  failures.push(`${paths.compose}: LAN discovery UDP must not be published by the Mac Preview`)
}

requireText(paths.dockerfile, dockerfile, 'FROM ${BUILD_ENV_IMAGE} AS builder', 'overridable x86 build environment')
requireText(paths.dockerfile, dockerfile, 'FROM ${RUNTIME_BASE_IMAGE} AS runtime', 'overridable x86 runtime base')
requireText(paths.dockerfile, dockerfile, 'COPY scripts/docker-healthcheck.x86.sh /usr/local/bin/cosmo-x86-healthcheck')
requireText(paths.dockerfile, dockerfile, 'ARG COSMO_BUILD_JOBS', 'configurable CPU build parallelism')
requireText(paths.dockerfile, dockerfile, 'chmod +x /usr/local/bin/cosmo-x86-entrypoint /usr/local/bin/cosmo-x86-healthcheck')
requireText(paths.cpuBuild, cpuBuild, 'BUILD_JOBS="${COSMO_BUILD_JOBS:-$(nproc)}"', 'CPU build parallelism default')
requireText(paths.cpuBuild, cpuBuild, 'cmake --build . --target install -j"${BUILD_JOBS}"', 'CPU install build parallelism')
requireText(paths.cpuBuild, cpuBuild, 'cmake --build . --target package_all', 'existing CPU package build behavior')
requireText(paths.runtimeStartup, runtimeStartup,
  'export COSMO_STREAM_PLAY_MODE="${COSMO_STREAM_PLAY_MODE:-srs}"',
  'deployment stream mode must override the runtime default')
requireText(paths.runtimeStartup, runtimeStartup,
  'export COSMO_STREAM_HTTP_PORT="${COSMO_STREAM_HTTP_PORT:-18088}"',
  'deployment stream port must override the runtime default')

for (const processName of ['nginx', 'srs', 'cosmo-engine']) {
  requireText(paths.healthcheck, healthcheck, processName, `process check for ${processName}`)
}
requireText(paths.healthcheck, healthcheck, '/dev/tcp/127.0.0.1/80', 'web response check')
requireText(paths.dockerignore, dockerignore, '/output', 'private agent-run output exclusion')

for (const command of ['doctor)', 'up)', 'status)', 'logs)', 'down)', 'url)']) {
  requireText(paths.launcher, launcher, command, `launcher action ${command.slice(0, -1)}`)
}
requireText(paths.launcher, launcher, '/Applications/Docker.app/Contents/Resources/bin', 'Docker Desktop CLI and credential-helper fallback')
requireText(paths.launcher, launcher, 'COSMO_X86_BUILD_JOBS', 'Mac build-parallelism override')
requireText(paths.launcher, launcher, 'reserved for Preview media services', 'reserved media-port rejection')
requireText(paths.launcher, launcher, 'docker_cmd port "${CONTAINER_NAME}" 80/tcp', 'running-container web-port comparison')
requireText(paths.launcher, launcher, "'{{.Os}}/{{.Architecture}}'", 'existing image platform check')
requireText(paths.launcher, launcher, '"linux/amd64"', 'required reusable image platform')
requireText(paths.launcher, launcher, 'compose up -d --no-build "${SERVICE_NAME}"', 'restart without an implicit rebuild')
requireText(paths.launcher, launcher, "use 'up --build'", 'explicit rebuild guidance')
requireText(paths.launcher, launcher, 'compose down', 'non-destructive default shutdown')
if (launcher.includes('down -v') || launcher.includes('volume rm')) {
  failures.push(`${paths.launcher}: default lifecycle must not remove persistent volumes`)
}

for (const [relativePath, source, languageChecks] of [
  [paths.docsZh, docsZh, ['Preview', 'linux/amd64', '127.0.0.1', 'Model Guard', 'CEMC', '两次']],
  [paths.docsEn, docsEn, ['Preview', 'linux/amd64', '127.0.0.1', 'Model Guard', 'CEMC', 'two consecutive']]
]) {
  for (const expected of languageChecks) requireText(relativePath, source, expected)
}

requireText(paths.readmeZh, readmeZh, 'scripts/macos-docker-preview.sh', 'macOS Preview entry point')
requireText(paths.readmeEn, readmeEn, 'scripts/macos-docker-preview.sh', 'macOS Preview entry point')

requireExecutable(paths.healthcheck)
requireExecutable(paths.launcher)

if (failures.length > 0) {
  console.error(`macOS Docker Preview check failed with ${failures.length} issue(s):`)
  for (const failure of failures) console.error(`- ${failure}`)
  process.exit(1)
}

console.log(
  `macOS Docker Preview check passed: isolated amd64 Compose, ${publishedPorts.length} loopback-only ports, health/lifecycle contracts, persistence, and bilingual boundaries.`
)
