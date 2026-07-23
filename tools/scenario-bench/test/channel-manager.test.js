import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { ChannelManager } from '../src/channel-manager.js';

test('reuses local AddVideo channels by bench-prefixed channelName', async () => {
  let addCalls = 0;
  const client = {
    async cameraPage() {
      return {
        rows: [
          { channelName: 'bench-case-01-clip', videoChannelId: 'existing-1' },
          { channelCode: 'bench-case-02', videoChannelId: 'existing-2' },
        ],
      };
    },
    async cameraAddVideo() {
      addCalls++;
      return { resData: { id: `created-${addCalls}` } };
    },
  };
  const manager = new ChannelManager(client, {
    channelPrefix: 'bench-case',
    reuse: true,
  });

  const ids = await manager.ensureChannels({
    mode: 'local',
    local: [{ name: 'clip', filePath: '/device/clip.mp4', contentLength: 100 }],
  }, 2);

  assert.deepEqual(ids, ['existing-1', 'existing-2']);
  assert.equal(addCalls, 0);
});

test('created local AddVideo channel names keep a stable bench prefix', async () => {
  const payloads = [];
  const client = {
    async cameraPage() {
      return { rows: [] };
    },
    async cameraAddVideo(payload) {
      payloads.push(payload);
      return { resData: { id: `created-${payloads.length}` } };
    },
  };
  const manager = new ChannelManager(client, {
    channelPrefix: 'bench-case',
    reuse: true,
  });

  const ids = await manager.ensureChannels({
    mode: 'local',
    local: [{ name: 'clip', filePath: '/device/clip.mp4', contentLength: 100 }],
  }, 1);

  assert.deepEqual(ids, ['created-1']);
  assert.equal(payloads[0].channelName, 'bench-case-01-clip');
  assert.equal(payloads[0].filePath, '/device/clip.mp4');
  assert.equal(payloads[0].contentLength, '100');
});

test('uploads local videos with bounded chunks and consumes the server upload ID', async (t) => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'scenario-bench-upload-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const video = path.join(dir, 'clip.mp4');
  fs.writeFileSync(video, '');
  fs.truncateSync(video, 8 * 1024 * 1024 + 1);

  const chunks = [];
  const payloads = [];
  const client = {
    async cameraPage() {
      return { rows: [] };
    },
    async uploadTempChunk(buffer, fileName, meta) {
      chunks.push({ size: buffer.length, fileName, meta });
      return {
        resData: {
          uploadId: 'server-upload-id',
          nextChunkIndex: String(meta.chunkIndex + 1),
          complete: meta.chunkIndex + 1 === meta.totalChunks,
        },
      };
    },
    async cameraAddVideo(payload) {
      payloads.push(payload);
      return { resData: { id: 'created-1' } };
    },
  };
  const manager = new ChannelManager(client, {
    channelPrefix: 'bench-case',
    reuse: true,
  });

  const ids = await manager.ensureChannels({
    mode: 'local',
    local: [{ name: 'clip', file: video }],
  }, 1);

  assert.deepEqual(ids, ['created-1']);
  assert.deepEqual(chunks.map((chunk) => chunk.size), [8 * 1024 * 1024, 1]);
  assert.equal(chunks[0].meta.uploadId, '');
  assert.equal(chunks[1].meta.uploadId, 'server-upload-id');
  assert.equal(chunks[0].meta.purpose, 'video');
  assert.equal(chunks[0].meta.clientRequestId, chunks[1].meta.clientRequestId);
  assert.equal(payloads[0].uploadId, 'server-upload-id');
  assert.equal(payloads[0].filePath, undefined);
  assert.equal(payloads[0].contentLength, undefined);
});
