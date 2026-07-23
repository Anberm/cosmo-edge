import assert from 'node:assert/strict';
import test from 'node:test';

import { PreviewLoad, _previewTest } from '../src/preview-load.js';

const entries = [
  { channelId: 'LX1', algorithmId: 'external-1', algorithmCode: '7463' },
  { channelId: 'LX1', algorithmId: 'external-2', algorithmCode: 'other' },
  { channelId: 'LX2', algorithmId: 'external-1', algorithmCode: '7463' },
];

test('preview load deduplicates channels and selects algorithm code', () => {
  assert.deepEqual(_previewTest.desiredStreams(entries, 'algorithm', 'all'), [
    { channelId: 'LX1', algorithmId: '7463', key: 'LX1/7463' },
    { channelId: 'LX2', algorithmId: '7463', key: 'LX2/7463' },
  ]);
});

test('raw preview omits algorithm and honors stream limit', () => {
  assert.deepEqual(_previewTest.desiredStreams(entries, 'raw', 1), [
    { channelId: 'LX1', algorithmId: '', key: 'LX1/raw' },
  ]);
});

test('preview load derives stable SRS stream name from FLV URL', () => {
  assert.equal(_previewTest.streamNameFromFlvUrl('/live/LX1_7463.flv'), 'LX1_7463');
  assert.equal(_previewTest.streamNameFromFlvUrl('/live/LX1.flv?token=x'), 'LX1');
});

test('preview load retries bounded first-frame warmup errors', async () => {
  let requests = 0;
  let stops = 0;
  const client = {
    async requestLiveStream() {
      requests++;
      if (requests < 3) throw new Error('API error: 取流无数据 (code 12303)');
      return { flvUrl: '/live/LX1_7463.flv' };
    },
    async streamKeepAlive() {},
    async streamStop() { stops++; },
  };
  const load = new PreviewLoad(client, {
    mode: 'algorithm',
    clientsPerStream: 0,
    startTimeoutMs: 100,
    startRetryMs: 1,
  });

  await load.sync(entries.slice(0, 1));
  assert.equal(requests, 3);
  await load.stop();
  assert.equal(stops, 1);
});

test('preview load does not retry non-warmup start failures', async () => {
  let requests = 0;
  const client = {
    async requestLiveStream() {
      requests++;
      throw new Error('API error: invalid algorithm (code 12201)');
    },
  };
  const load = new PreviewLoad(client, {
    mode: 'algorithm',
    clientsPerStream: 0,
    startTimeoutMs: 100,
    startRetryMs: 1,
  });

  await assert.rejects(load.sync(entries.slice(0, 1)), /invalid algorithm/);
  assert.equal(requests, 1);
});
