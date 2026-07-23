import assert from 'node:assert/strict';
import test from 'node:test';

import { CosmoClient } from '../src/cosmo-client.js';

test('existing device token bypasses password login', async () => {
  const client = new CosmoClient({ base: 'http://device', token: 'short-lived-token' });
  client._post = async () => {
    throw new Error('login endpoint must not be called');
  };

  assert.deepEqual(await client.login(), { mtk: 'short-lived-token' });
});

test('login rejects missing credentials when no token is supplied', async () => {
  const client = new CosmoClient({ base: 'http://device' });
  await assert.rejects(client.login(), /requires user\/password or an existing token/);
});

test('batch task switch uses the wire-level switch field', async () => {
  const client = new CosmoClient({ base: 'http://device', token: 'token' });
  let request = null;
  client._post = async (route, payload) => {
    request = { route, payload };
    return { resData: { failedList: [] } };
  };

  await client.taskBatchSwitch([{
    id: 'LX1_7463',
    channelId: 'LX1',
    algorithmId: '7463',
    enable: 1,
  }]);

  assert.deepEqual(request, {
    route: '/Task/BatchSwitchTask',
    payload: {
      tasks: [{ id: 'LX1_7463', channelId: 'LX1', algorithmId: '7463', switch: 1 }],
    },
  });
});
