import { spawn } from 'node:child_process';

const DEFAULT_KEEPALIVE_MS = 5_000;
const DEFAULT_START_TIMEOUT_MS = 30_000;
const DEFAULT_START_RETRY_MS = 1_000;

export class PreviewLoad {
  constructor(client, {
    mode = 'none',
    streamLimit = 'all',
    clientsPerStream = 1,
    mediaBase = null,
    srsApiBase = null,
    ffmpeg = 'ffmpeg',
    logger = null,
    startTimeoutMs = DEFAULT_START_TIMEOUT_MS,
    startRetryMs = DEFAULT_START_RETRY_MS,
  } = {}) {
    if (!['none', 'raw', 'algorithm'].includes(mode)) {
      throw new Error(`unsupported preview mode "${mode}", expected none, raw, or algorithm`);
    }
    this.client = client;
    this.mode = mode;
    this.streamLimit = normalizeStreamLimit(streamLimit);
    this.clientsPerStream = nonNegativeInteger(clientsPerStream, 'preview clients');
    this.mediaBase = mediaBase?.replace(/\/+$/, '') ?? null;
    this.srsApiBase = srsApiBase?.replace(/\/+$/, '') ?? null;
    this.ffmpeg = ffmpeg;
    this.log = logger;
    this.startTimeoutMs = positiveInteger(startTimeoutMs, 'preview start timeout');
    this.startRetryMs = positiveInteger(startRetryMs, 'preview start retry interval');
    this.streams = new Map();
    this.errors = [];
  }

  profile() {
    return {
      mode: this.mode,
      streamLimit: this.streamLimit,
      clientsPerStream: this.clientsPerStream,
      mediaBase: this.mediaBase,
      srsApiBase: this.srsApiBase,
    };
  }

  async sync(entries) {
    if (this.mode === 'none') return;
    if (!this.mediaBase && this.clientsPerStream > 0) {
      throw new Error('--media-base is required when preview clients are enabled');
    }

    const desired = desiredStreams(entries, this.mode, this.streamLimit);
    const desiredKeys = new Set(desired.map((item) => item.key));
    for (const [key, state] of [...this.streams]) {
      if (!desiredKeys.has(key)) await this._stopStream(key, state);
    }
    for (const item of desired) {
      if (!this.streams.has(item.key)) await this._startStream(item);
    }
  }

  assertHealthy() {
    if (this.errors.length) {
      throw new Error(`preview load failed: ${this.errors.join('; ')}`);
    }
    for (const state of this.streams.values()) {
      if (!state.stream?.flvUrl) throw new Error(`preview ${state.key} has no HTTP-FLV URL`);
      const alive = state.clients.filter((client) => client.exitCode == null && client.signalCode == null);
      if (alive.length !== this.clientsPerStream) {
        throw new Error(
          `preview ${state.key} has ${alive.length}/${this.clientsPerStream} live media clients`,
        );
      }
    }
  }

  async snapshot() {
    const result = {
      mode: this.mode,
      requestedStreams: this.streams.size,
      mediaClients: [...this.streams.values()].reduce(
        (total, state) => total + state.clients.filter((c) => c.exitCode == null).length,
        0,
      ),
      errors: [...this.errors],
    };
    if (!this.srsApiBase) return result;

    try {
      const [streams, clients] = await Promise.all([
        fetchSrs(`${this.srsApiBase}/api/v1/streams/`),
        fetchSrs(`${this.srsApiBase}/api/v1/clients/`),
      ]);
      const names = new Set([...this.streams.values()].map((state) => state.streamName));
      const matchingStreams = (streams.streams ?? []).filter(
        (stream) => stream.app === 'live' && names.has(stream.name),
      );
      const matchingIds = new Set(matchingStreams.map((stream) => stream.id));
      const matchingClients = (clients.clients ?? []).filter(
        (client) => matchingIds.has(client.stream),
      );
      result.srsStreams = matchingStreams.length;
      result.srsPublishingStreams = matchingStreams.filter((stream) => stream.publish?.active).length;
      result.srsClients = matchingClients.length;
    } catch (err) {
      result.srsError = err.message;
    }
    return result;
  }

  async stop() {
    for (const [key, state] of [...this.streams]) await this._stopStream(key, state);
  }

  async _startStream(item) {
    this.log?.info(`Starting ${this.mode} preview ${item.key}...`);
    const stream = await this._requestAfterWarmup(item);
    if (!stream?.flvUrl) throw new Error(`preview ${item.key} did not return flvUrl`);
    const streamName = streamNameFromFlvUrl(stream.flvUrl);
    const state = { ...item, stream, streamName, clients: [], keepalive: null };
    this.streams.set(item.key, state);

    state.keepalive = setInterval(() => {
      this.client.streamKeepAlive(item).catch((err) => {
        this.errors.push(`${item.key} keepalive: ${err.message}`);
      });
    }, DEFAULT_KEEPALIVE_MS);
    state.keepalive.unref?.();

    if (this.clientsPerStream > 0) {
      const url = new URL(stream.flvUrl, `${this.mediaBase}/`).toString();
      for (let index = 0; index < this.clientsPerStream; index++) {
        state.clients.push(this._spawnClient(item.key, index, url));
      }
    }
  }

  async _requestAfterWarmup(item) {
    const deadline = Date.now() + this.startTimeoutMs;
    let attempts = 0;
    while (true) {
      attempts++;
      try {
        return await this.client.requestLiveStream(item);
      } catch (err) {
        const remainingMs = deadline - Date.now();
        if (!isRetryableStartError(err) || remainingMs <= 0) throw err;
        this.log?.warn(
          `Preview ${item.key} is waiting for the first decoded frame `
          + `(attempt ${attempts}, retrying within ${Math.ceil(remainingMs / 1000)}s): ${err.message}`,
        );
        await sleep(Math.min(this.startRetryMs, remainingMs));
      }
    }
  }

  async _stopStream(key, state) {
    if (state.keepalive) clearInterval(state.keepalive);
    for (const client of state.clients) stopProcess(client);
    await Promise.all(state.clients.map((client) => waitForExit(client, 5_000)));
    try {
      await this.client.streamStop(state);
    } catch (err) {
      this.errors.push(`${key} stop: ${err.message}`);
    }
    this.streams.delete(key);
  }

  _spawnClient(key, index, url) {
    const child = spawn(this.ffmpeg, [
      '-nostdin',
      '-hide_banner',
      '-loglevel', 'error',
      '-rw_timeout', '5000000',
      '-i', url,
      '-map', '0:v:0',
      '-an',
      '-f', 'null',
      '-',
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    let stderr = '';
    child.stderr.setEncoding('utf8');
    child.stderr.on('data', (chunk) => {
      stderr = `${stderr}${chunk}`.slice(-2_000);
    });
    child.once('error', (err) => {
      this.errors.push(`${key} client ${index + 1}: ${err.message}`);
    });
    child.once('exit', (code, signal) => {
      if (!child.__cosmoStopping) {
        this.errors.push(
          `${key} client ${index + 1} exited code=${code} signal=${signal ?? '-'} ${stderr.trim()}`.trim(),
        );
      }
    });
    return child;
  }
}

function desiredStreams(entries, mode, streamLimit) {
  const byChannel = new Map();
  for (const entry of entries ?? []) {
    if (!byChannel.has(entry.channelId)) byChannel.set(entry.channelId, entry);
  }
  const candidates = [...byChannel.values()];
  const limit = streamLimit === 'all' ? candidates.length : Math.min(streamLimit, candidates.length);
  return candidates.slice(0, limit).map((entry) => {
    const algorithmId = mode === 'algorithm' ? String(entry.algorithmCode ?? entry.algorithmId) : '';
    return {
      channelId: entry.channelId,
      algorithmId,
      key: `${entry.channelId}/${algorithmId || 'raw'}`,
    };
  });
}

function normalizeStreamLimit(value) {
  if (value == null || value === 'all') return 'all';
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) throw new Error('preview stream limit must be all or >= 1');
  return parsed;
}

function nonNegativeInteger(value, label) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 0) throw new Error(`${label} must be a non-negative integer`);
  return parsed;
}

function positiveInteger(value, label) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) throw new Error(`${label} must be a positive integer`);
  return parsed;
}

function isRetryableStartError(err) {
  const message = String(err?.message ?? err);
  return /(?:code\s*12303\b|DemuxNoData|取流无数据)/i.test(message);
}

function streamNameFromFlvUrl(url) {
  const name = String(url).split('/').pop()?.replace(/\.flv(?:\?.*)?$/, '');
  if (!name) throw new Error(`cannot derive stream name from ${url}`);
  return name;
}

async function fetchSrs(url) {
  const response = await fetch(url, { signal: AbortSignal.timeout(5_000) });
  if (!response.ok) throw new Error(`HTTP ${response.status} from ${url}`);
  const body = await response.json();
  if (body.code !== 0) throw new Error(`SRS API code ${body.code} from ${url}`);
  return body;
}

function stopProcess(child) {
  if (child.exitCode != null || child.signalCode != null) return;
  child.__cosmoStopping = true;
  child.kill('SIGTERM');
}

async function waitForExit(child, timeoutMs) {
  if (child.exitCode != null || child.signalCode != null) return;
  await new Promise((resolve) => {
    const timer = setTimeout(() => {
      child.__cosmoStopping = true;
      child.kill('SIGKILL');
      resolve();
    }, timeoutMs);
    child.once('exit', () => {
      clearTimeout(timer);
      resolve();
    });
  });
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export const _previewTest = { desiredStreams, streamNameFromFlvUrl, isRetryableStartError };
