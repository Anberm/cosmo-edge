import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';

const DEFAULT_SAMPLE_WIDTH = 320;
const DEFAULT_SAMPLE_HEIGHT = 180;
const DEFAULT_SAMPLE_FPS = 5;

export async function runPreviewValidation(client, options) {
  const outputDir = path.resolve(options.output);
  fs.mkdirSync(outputDir, { recursive: true });
  const mode = normalizeMode(options.mode, options.algorithmId);
  const durationSec = positiveNumber(options.durationSec ?? 8, 'duration seconds');
  const clients = positiveInteger(options.clients ?? 4, 'clients');
  const lifecycleIterations = nonNegativeInteger(
    options.lifecycleIterations ?? 0,
    'lifecycle iterations',
  );
  const context = {
    ...options,
    outputDir,
    durationSec,
    clients,
    sampleWidth: positiveInteger(options.sampleWidth ?? DEFAULT_SAMPLE_WIDTH, 'sample width'),
    sampleHeight: positiveInteger(options.sampleHeight ?? DEFAULT_SAMPLE_HEIGHT, 'sample height'),
    sampleFps: positiveNumber(options.sampleFps ?? DEFAULT_SAMPLE_FPS, 'sample FPS'),
    ffmpeg: options.ffmpeg ?? 'ffmpeg',
    ffprobe: options.ffprobe ?? 'ffprobe',
    mediaBase: requiredUrl(options.mediaBase, 'media base'),
    srsApiBase: requiredUrl(options.srsApiBase, 'SRS API base'),
    log: options.logger,
  };

  const report = {
    startedAt: new Date().toISOString(),
    status: 'running',
    channelId: options.channelId,
    algorithmId: options.algorithmId ?? '',
    mode,
    durationSec,
    clients,
    lifecycleIterations,
    streams: {},
    lifecycle: null,
    concurrency: null,
    invalidRequests: null,
  };

  try {
    const baseline = await client.queryHardwareResource();
    report.acceleratorBaseline = baseline.accelerator ?? null;

    if (mode === 'raw' || mode === 'both') {
      report.streams.raw = await validateStream(client, {
        ...context,
        channelId: options.channelId,
        algorithmId: '',
        label: 'raw',
      });
    }
    if (mode === 'algorithm' || mode === 'both') {
      if (!options.algorithmId) throw new Error('--algorithm is required for algorithm preview');
      report.streams.algorithm = await validateStream(client, {
        ...context,
        channelId: options.channelId,
        algorithmId: options.algorithmId,
        label: 'algorithm',
      });
    }

    if (report.streams.algorithm) {
      const rawPixels = report.streams.raw?.capture?.maxOverlayPixels ?? 0;
      const algorithmPixels = report.streams.algorithm.capture.maxOverlayPixels;
      const minimumDelta = nonNegativeInteger(options.minOverlayPixelDelta ?? 0, 'overlay pixel delta');
      report.osdPixelCheck = {
        rawMaxOverlayPixels: rawPixels,
        algorithmMaxOverlayPixels: algorithmPixels,
        delta: algorithmPixels - rawPixels,
        minimumDelta,
        pass: algorithmPixels - rawPixels >= minimumDelta,
      };
      if (!report.osdPixelCheck.pass) {
        throw new Error(
          `algorithm preview overlay pixel delta ${report.osdPixelCheck.delta} < ${minimumDelta}`,
        );
      }
    }

    report.concurrency = await validateConcurrentCreate(client, context);
    report.invalidRequests = await validateInvalidRequests(client, context);
    if (lifecycleIterations > 0) {
      report.lifecycle = await validateLifecycle(client, {
        ...context,
        iterations: lifecycleIterations,
      });
    }

    report.status = 'passed';
    report.endedAt = new Date().toISOString();
    writeReport(outputDir, report);
    return report;
  } catch (err) {
    report.status = 'failed';
    report.error = err.message;
    report.endedAt = new Date().toISOString();
    writeReport(outputDir, report);
    throw err;
  }
}

async function validateStream(client, context) {
  const key = `${context.channelId}/${context.algorithmId || 'raw'}`;
  context.log?.info(`Validating ${context.label} preview ${key}...`);
  const before = await client.queryHardwareResource();
  const startedAt = Date.now();
  const stream = await client.requestLiveStream(context);
  if (!stream?.flvUrl) throw new Error(`${key} returned no flvUrl`);
  const firstFrameRequestMs = Date.now() - startedAt;
  const streamName = streamNameFromFlvUrl(stream.flvUrl);
  const streamUrl = new URL(stream.flvUrl, `${context.mediaBase}/`).toString();
  const keepalive = setInterval(() => {
    client.streamKeepAlive(context).catch(() => {});
  }, 5_000);
  keepalive.unref?.();

  try {
    const probe = await probeStream(context.ffprobe, streamUrl);
    if (probe.codec_name !== 'h264') {
      throw new Error(`${key} output codec is ${probe.codec_name ?? 'unknown'}, expected h264`);
    }
    if (!(probe.width > 0 && probe.height > 0)) throw new Error(`${key} has invalid resolution`);

    const captures = Array.from({ length: context.clients }, (_, index) => startCapture({
      ...context,
      url: streamUrl,
      key: `${key}#${index + 1}`,
    }));
    await sleep(Math.min(2_000, context.durationSec * 250));
    const activeSrs = await querySrsStream(context.srsApiBase, streamName);
    if (!activeSrs?.publish?.active) throw new Error(`${key} is not actively published in SRS`);
    if (Number(activeSrs.clients ?? 0) < context.clients) {
      throw new Error(`${key} SRS clients=${activeSrs.clients ?? 0}, expected >= ${context.clients}`);
    }
    const during = await waitForAcceleratorRefresh(client, (accelerator) =>
      Number(accelerator?.activePreviewStreams) >= 1,
    );
    const captureResults = await Promise.all(captures.map((capture) => capture.done));
    for (const result of captureResults) validateCapture(result, context);
    const representative = captureResults.reduce(
      (best, result) => result.maxOverlayPixels > best.maxOverlayPixels ? result : best,
      captureResults[0],
    );
    const screenshot = path.join(context.outputDir, `${context.label}-preview.ppm`);
    writePpm(screenshot, representative.bestFrame, context.sampleWidth, context.sampleHeight);

    // A media client may disconnect without releasing the backend publisher.
    const stillPublished = await querySrsStream(context.srsApiBase, streamName);
    if (!stillPublished?.publish?.active) {
      throw new Error(`${key} backend publisher stopped after viewers disconnected`);
    }
    const reconnect = await startCapture({
      ...context,
      durationSec: Math.min(3, context.durationSec),
      url: streamUrl,
      key: `${key}#reconnect`,
    }).done;
    validateCapture(reconnect, { ...context, durationSec: Math.min(3, context.durationSec) });

    // Stop the backend while a frontend media connection is still open.
    const drain = startDrainClient(context.ffmpeg, streamUrl);
    await sleep(1_000);
    await client.streamStop(context);
    await waitForProcessExit(drain, 8_000);
    await waitForSrsAbsent(context.srsApiBase, streamName, 10_000);
    const after = await waitForAcceleratorRefresh(client, (accelerator) =>
      Number(accelerator?.activePreviewStreams) === 0
      && Number(accelerator?.activePreviewPublishers) === 0,
    );
    validateBackendMetrics(context, before.accelerator, during.accelerator, after.accelerator);

    return {
      streamName,
      output: probe,
      firstFrameRequestMs,
      srsClientsPeak: Number(activeSrs.clients ?? 0),
      capture: summarizeCapture(representative),
      reconnectFrames: reconnect.frameCount,
      screenshot,
      acceleratorBefore: before.accelerator ?? null,
      acceleratorDuring: during.accelerator ?? null,
      acceleratorAfter: after.accelerator ?? null,
    };
  } finally {
    clearInterval(keepalive);
    await client.streamStop(context).catch(() => {});
  }
}

function validateBackendMetrics(context, before, during, after) {
  if (!during) throw new Error(`${context.label} preview returned no pipeline metrics`);
  if (delta(during, before, 'publishedFrames') <= 0) {
    throw new Error(`${context.label} preview did not increment publishedFrames`);
  }
  if (delta(during, before, 'firstFrames') <= 0) {
    throw new Error(`${context.label} preview did not increment firstFrames`);
  }
  if (context.algorithmId) {
    if (delta(during, before, 'osdFrames') <= 0) {
      throw new Error('algorithm preview did not increment osdFrames');
    }
  }
  if (Number(after?.activePreviewStreams) !== 0 || Number(after?.activePreviewPublishers) !== 0) {
    throw new Error(`${context.label} preview resources did not return to zero`);
  }
  if (delta(after, before, 'previewStreamFailures') !== 0) {
    throw new Error(`${context.label} preview incremented its failure counter`);
  }
}

async function validateConcurrentCreate(client, context) {
  const request = { channelId: context.channelId, algorithmId: context.algorithmId ?? '' };
  const [first, second] = await Promise.all([
    client.requestLiveStream(request),
    client.requestLiveStream(request),
  ]);
  if (first?.flvUrl !== second?.flvUrl) throw new Error('concurrent preview requests returned different streams');
  await Promise.all([client.streamStop(request), client.streamStop(request)]);
  const streamName = streamNameFromFlvUrl(first.flvUrl);
  await waitForSrsAbsent(context.srsApiBase, streamName, 10_000);
  return { pass: true, streamName, sharedUrl: first.flvUrl };
}

async function validateInvalidRequests(client, context) {
  const result = {};
  result.invalidChannel = await expectApiFailure(() => client.requestLiveStream({
    channelId: `${context.channelId}-invalid`,
    algorithmId: '',
  }));
  if (context.algorithmId) {
    result.invalidAlgorithm = await expectApiFailure(() => client.requestLiveStream({
      channelId: context.channelId,
      algorithmId: `${context.algorithmId}-invalid`,
    }));
    await client.streamStop({
      channelId: context.channelId,
      algorithmId: `${context.algorithmId}-invalid`,
    }).catch(() => {});
  }
  return result;
}

async function validateLifecycle(client, context) {
  const request = { channelId: context.channelId, algorithmId: context.algorithmId ?? '' };
  const before = await client.queryHardwareResource();
  const startedAt = Date.now();
  let streamName = null;
  const checkpointEvery = Math.min(100, Math.max(1, Math.floor(context.iterations / 10)));
  for (let iteration = 0; iteration < context.iterations; iteration++) {
    const stream = await client.requestLiveStream(request);
    streamName = streamNameFromFlvUrl(stream.flvUrl);
    await client.streamStop(request);
    if ((iteration + 1) % checkpointEvery === 0 || iteration + 1 === context.iterations) {
      await waitForSrsAbsent(context.srsApiBase, streamName, 10_000);
      context.log?.info(`Preview lifecycle ${iteration + 1}/${context.iterations}`);
    }
  }
  const after = await waitForAcceleratorRefresh(
    client,
    (accelerator) => lifecycleMetricsReady(accelerator, before.accelerator, context.iterations),
  );
  const starts = delta(after.accelerator, before.accelerator, 'previewStreamStarts');
  const stops = delta(after.accelerator, before.accelerator, 'previewStreamStops');
  if (starts < context.iterations || stops < context.iterations) {
    throw new Error(`lifecycle metrics starts/stops=${starts}/${stops}, expected >= ${context.iterations}`);
  }
  if (delta(after.accelerator, before.accelerator, 'previewStreamFailures') !== 0) {
    throw new Error('preview lifecycle incremented failure counter');
  }
  return {
    pass: true,
    iterations: context.iterations,
    elapsedMs: Date.now() - startedAt,
    starts,
    stops,
    streamName,
    acceleratorBefore: before.accelerator,
    acceleratorAfter: after.accelerator,
  };
}

function lifecycleMetricsReady(accelerator, before, iterations) {
  return Number(accelerator?.activePreviewStreams) === 0
    && Number(accelerator?.activePreviewPublishers) === 0
    && delta(accelerator, before, 'previewStreamStarts') >= iterations
    && delta(accelerator, before, 'previewStreamStops') >= iterations;
}

function startCapture(context) {
  const args = captureArgs(context);
  const child = spawn(context.ffmpeg, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  const stdout = [];
  let stderr = '';
  child.stdout.on('data', (chunk) => stdout.push(chunk));
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => { stderr = `${stderr}${chunk}`.slice(-8_000); });
  const done = new Promise((resolve, reject) => {
    let settled = false;
    const finish = (operation, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      operation(value);
    };
    const timeoutMs = captureTimeoutMs(context.durationSec);
    const timer = setTimeout(() => {
      child.kill('SIGKILL');
      finish(reject, new Error(`${context.key} ffmpeg timed out after ${timeoutMs}ms`));
    }, timeoutMs);
    child.once('error', (err) => finish(reject, err));
    child.once('exit', (code, signal) => {
      if (code !== 0) {
        finish(reject, new Error(`${context.key} ffmpeg exit=${code} signal=${signal ?? '-'} ${stderr.trim()}`));
        return;
      }
      try {
        finish(resolve, analyzeFrames(Buffer.concat(stdout), stderr, context));
      } catch (err) {
        finish(reject, err);
      }
    });
  });
  return { child, done };
}

function captureTimeoutMs(durationSec) {
  return Math.max(15_000, Math.ceil(Number(durationSec) * 3_000 + 10_000));
}

function captureArgs(context) {
  return [
    '-nostdin', '-hide_banner', '-loglevel', 'error',
    '-rw_timeout', '5000000',
    '-i', context.url,
    '-an',
    '-vf', `fps=${context.sampleFps},scale=${context.sampleWidth}:${context.sampleHeight}`,
    '-frames:v', String(Math.ceil(context.durationSec * context.sampleFps)),
    '-pix_fmt', 'rgb24',
    '-f', 'rawvideo',
    '-progress', 'pipe:2',
    'pipe:1',
  ];
}

function analyzeFrames(buffer, progressText, context) {
  const frameSize = context.sampleWidth * context.sampleHeight * 3;
  if (buffer.length % frameSize !== 0) {
    throw new Error(`${context.key} raw capture has ${buffer.length % frameSize} trailing bytes`);
  }
  const frameCount = buffer.length / frameSize;
  let maxOverlayPixels = -1;
  let bestFrame = null;
  let overlayPixelTotal = 0;
  for (let index = 0; index < frameCount; index++) {
    const frame = buffer.subarray(index * frameSize, (index + 1) * frameSize);
    const overlayPixels = countOverlayLikePixels(frame);
    overlayPixelTotal += overlayPixels;
    if (overlayPixels > maxOverlayPixels) {
      maxOverlayPixels = overlayPixels;
      bestFrame = Buffer.from(frame);
    }
  }
  // FFmpeg has emitted this microsecond-valued field under both names across releases.
  const outTimes = [...progressText.matchAll(/out_time_(?:us|ms)=(\d+)/g)].map((match) => Number(match[1]));
  return {
    frameCount,
    maxOverlayPixels: Math.max(maxOverlayPixels, 0),
    avgOverlayPixels: frameCount ? overlayPixelTotal / frameCount : 0,
    bestFrame,
    outTimeUs: outTimes.length ? Math.max(...outTimes) : 0,
  };
}

function validateCapture(result, context) {
  const minimumFrames = Math.max(2, Math.floor(context.durationSec * context.sampleFps * 0.6));
  if (result.frameCount < minimumFrames) {
    throw new Error(`${context.key ?? context.label} decoded ${result.frameCount} frames, expected >= ${minimumFrames}`);
  }
  if (result.outTimeUs < context.durationSec * 500_000) {
    throw new Error(`${context.key ?? context.label} timestamps advanced only ${result.outTimeUs}us`);
  }
}

function summarizeCapture(result) {
  return {
    frameCount: result.frameCount,
    maxOverlayPixels: result.maxOverlayPixels,
    avgOverlayPixels: Math.round(result.avgOverlayPixels),
    outTimeUs: result.outTimeUs,
  };
}

function countOverlayLikePixels(rgb) {
  let count = 0;
  for (let index = 0; index < rgb.length; index += 3) {
    const red = rgb[index];
    const green = rgb[index + 1];
    const blue = rgb[index + 2];
    const max = Math.max(red, green, blue);
    const min = Math.min(red, green, blue);
    if (max >= 175 && max - min >= 85) count++;
  }
  return count;
}

async function probeStream(ffprobe, url) {
  const output = await runProcess(ffprobe, [
    '-v', 'error', '-rw_timeout', '5000000',
    '-select_streams', 'v:0',
    '-show_entries', 'stream=codec_name,width,height,avg_frame_rate',
    '-of', 'json',
    url,
  ], 15_000);
  const stream = JSON.parse(output).streams?.[0];
  if (!stream) throw new Error('ffprobe returned no video stream');
  return stream;
}

function startDrainClient(ffmpeg, url) {
  return spawn(ffmpeg, [
    '-nostdin', '-hide_banner', '-loglevel', 'error',
    '-rw_timeout', '5000000', '-i', url,
    '-map', '0:v:0', '-an', '-f', 'null', '-',
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
}

async function waitForProcessExit(child, timeoutMs) {
  if (child.exitCode != null || child.signalCode != null) return;
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error('media client did not exit after backend preview stopped'));
    }, timeoutMs);
    child.once('exit', () => {
      clearTimeout(timer);
      resolve();
    });
    child.once('error', (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });
}

async function runProcess(command, args, timeoutMs) {
  const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  const stdout = [];
  const stderr = [];
  child.stdout.on('data', (chunk) => stdout.push(chunk));
  child.stderr.on('data', (chunk) => stderr.push(chunk));
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error(`${command} timed out after ${timeoutMs}ms`));
    }, timeoutMs);
    child.once('error', (err) => {
      clearTimeout(timer);
      reject(err);
    });
    child.once('exit', (code) => {
      clearTimeout(timer);
      if (code === 0) resolve(Buffer.concat(stdout).toString('utf8'));
      else reject(new Error(`${command} exit=${code}: ${Buffer.concat(stderr).toString('utf8').trim()}`));
    });
  });
}

async function querySrsStream(base, streamName) {
  const response = await fetch(`${base}/api/v1/streams/`, { signal: AbortSignal.timeout(5_000) });
  if (!response.ok) throw new Error(`SRS streams API HTTP ${response.status}`);
  const body = await response.json();
  if (body.code !== 0) throw new Error(`SRS streams API code ${body.code}`);
  return (body.streams ?? []).find((stream) => stream.app === 'live' && stream.name === streamName) ?? null;
}

async function waitForSrsAbsent(base, streamName, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const stream = await querySrsStream(base, streamName);
    if (!stream) return;
    await sleep(200);
  }
  throw new Error(`SRS stream ${streamName} remained after publisher stop`);
}

async function waitForAcceleratorRefresh(client, predicate, timeoutMs = 12_000) {
  const deadline = Date.now() + timeoutMs;
  let last = null;
  while (Date.now() < deadline) {
    last = await client.queryHardwareResource();
    if (predicate(last.accelerator)) return last;
    await sleep(500);
  }
  throw new Error(`accelerator metrics did not reach expected state: ${JSON.stringify(last?.accelerator ?? null)}`);
}

async function expectApiFailure(operation) {
  try {
    await operation();
  } catch (err) {
    return { pass: true, resCode: err.resCode ?? null, msgCode: err.msgCode ?? null, message: err.message };
  }
  throw new Error('invalid preview request unexpectedly succeeded');
}

function delta(after, before, key) {
  return Number(after?.[key] ?? 0) - Number(before?.[key] ?? 0);
}

function writePpm(file, rgb, width, height) {
  if (!rgb) throw new Error(`no decoded frame available for ${file}`);
  fs.writeFileSync(file, Buffer.concat([Buffer.from(`P6\n${width} ${height}\n255\n`), rgb]));
}

function writeReport(outputDir, report) {
  fs.writeFileSync(
    path.join(outputDir, 'preview-validation.json'),
    `${JSON.stringify(report, null, 2)}\n`,
  );
  const streamRows = Object.entries(report.streams ?? {}).map(([name, stream]) =>
    `| ${name} | ${stream.output?.codec_name ?? '-'} | ${stream.output?.width ?? '-'}x${stream.output?.height ?? '-'} | ${stream.capture?.frameCount ?? '-'} | ${stream.capture?.maxOverlayPixels ?? '-'} | ${stream.firstFrameRequestMs ?? '-'} |`,
  ).join('\n');
  fs.writeFileSync(path.join(outputDir, 'preview-validation.md'), `# Preview validation

- Status: ${report.status}
- Channel: ${report.channelId}
- Algorithm: ${report.algorithmId || '(raw only)'}
- Started: ${report.startedAt}
- Ended: ${report.endedAt ?? '-'}
- Error: ${report.error ?? 'none'}

| Mode | Codec | Resolution | Sampled frames | Overlay-like pixels | Request-to-first-frame ms |
| --- | --- | --- | ---: | ---: | ---: |
${streamRows}

- Concurrent create: ${report.concurrency?.pass ?? '-'}
- Lifecycle iterations: ${report.lifecycle?.iterations ?? 0}
- OSD pixel check: ${report.osdPixelCheck?.pass ?? 'N/A'}
`);
}

function normalizeMode(mode, algorithmId) {
  const value = mode ?? (algorithmId ? 'both' : 'raw');
  if (!['raw', 'algorithm', 'both'].includes(value)) {
    throw new Error(`unsupported preview validation mode: ${value}`);
  }
  return value;
}

function streamNameFromFlvUrl(url) {
  const value = String(url).split('/').pop()?.replace(/\.flv(?:\?.*)?$/, '');
  if (!value) throw new Error(`cannot derive stream name from ${url}`);
  return value;
}

function requiredUrl(value, label) {
  if (!value) throw new Error(`${label} is required`);
  return String(value).replace(/\/+$/, '');
}

function positiveNumber(value, label) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) throw new Error(`${label} must be > 0`);
  return parsed;
}

function positiveInteger(value, label) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) throw new Error(`${label} must be an integer >= 1`);
  return parsed;
}

function nonNegativeInteger(value, label) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 0) throw new Error(`${label} must be a non-negative integer`);
  return parsed;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export const _previewValidatorTest = {
  analyzeFrames,
  captureArgs,
  captureTimeoutMs,
  countOverlayLikePixels,
  lifecycleMetricsReady,
  streamNameFromFlvUrl,
};
