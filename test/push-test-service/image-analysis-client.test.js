'use strict';

const assert = require('node:assert/strict');
const http = require('node:http');
const { after, before, test } = require('node:test');

const {
  DeviceApiError,
  loginAndListAlgorithms,
  md5Password,
  normalizeBaseUrl,
  runImageAnalysis,
  cancelImageTask,
} = require('./image-analysis-client');

let server;
let baseUrl;
const requests = [];

function sendJson(res, body, status = 200) {
  res.writeHead(status, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(body));
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', chunk => chunks.push(chunk));
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

before(async () => {
  server = http.createServer(async (req, res) => {
    const body = await readBody(req);
    requests.push({ url: req.url, headers: req.headers, body });

    if (req.url === '/gtw/cwai/login/DoLogin') {
      const input = JSON.parse(body.toString('utf8'));
      assert.equal(input.account, 'admin');
      assert.equal(input.pwd, md5Password('secret'));
      return sendJson(res, {
        resCode: 1,
        resMsg: [],
        resData: { accountName: 'admin', mtk: 'token-1', passwordChangeRequired: false },
      });
    }
    if (req.url === '/gtw/cwai/algorithm/page') {
      assert.equal(req.headers.mtk, 'token-1');
      const input = JSON.parse(body.toString('utf8'));
      assert.equal(input.algorithmUsage, '2');
      return sendJson(res, {
        resCode: 1,
        resMsg: [],
        resData: { total: 1, rows: [{ algorithmId: '7602', algorithmName: '人脸图片分析' }] },
      });
    }
    if (req.url === '/gtw/cwai/aihost/PTaskCreate') {
      const input = JSON.parse(body.toString('utf8'));
      assert.equal(input.taskId, 'image-task-1');
      assert.equal(input.algorithmCode, '7602');
      return sendJson(res, { resCode: 1, resMsg: [] });
    }
    if (req.url === '/gtw/cwai/atomic/model/uploadCapabilities') {
      return sendJson(res, {
        resCode: 1,
        resMsg: [],
        resData: {
          maxChunkSize: '4',
          maxEncodedImageBytes: '1024',
          availableForNewUploadsBytes: '4096',
        },
      });
    }
    if (req.url === '/gtw/cwai/atomic/model/uploadTemp') {
      assert.match(req.headers['content-type'], /^multipart\/form-data; boundary=/);
      const multipart = body.toString('latin1');
      const chunkIndex = Number(multipart.match(/name="chunkIndex"\r\n\r\n(\d+)/)[1]);
      assert.match(multipart, /name="purpose"\r\n\r\nimage/);
      return sendJson(res, {
        resCode: 1,
        resMsg: [],
        resData: {
          uploadId: 'upload-1',
          nextChunkIndex: String(chunkIndex + 1),
          complete: chunkIndex === 2,
        },
      });
    }
    if (req.url === '/gtw/cwai/aihost/PTaskDetectPic') {
      const input = JSON.parse(body.toString('utf8'));
      assert.deepEqual(input, {
        taskId: 'image-task-1',
        algorithmCode: '7602',
        uploadId: 'upload-1',
      });
      return sendJson(res, {
        resCode: 1,
        resMsg: [],
        resData: {
          algorithmCode: '7602',
          areaList: [{
            areaId: '-1',
            areaName: 'default',
            bDetected: false,
            targetList: [{
              box: { x: 1, y: 2, width: 3, height: 4 },
              confidence: [{ label: 'face', confidence: 0.98 }],
            }],
          }],
        },
      });
    }
    if (req.url === '/gtw/cwai/aihost/PTaskCancle') {
      const input = JSON.parse(body.toString('utf8'));
      assert.equal(input.taskId, 'image-task-1');
      return sendJson(res, { resCode: 1, resMsg: [] });
    }
    return sendJson(res, { resCode: 0, resMsg: [{ msgText: 'not found' }] }, 404);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const address = server.address();
  baseUrl = `http://127.0.0.1:${address.port}`;
});

after(async () => {
  if (server) await new Promise(resolve => server.close(resolve));
});

test('normalizeBaseUrl only accepts credential-free HTTP(S) URLs', () => {
  assert.equal(normalizeBaseUrl(`${baseUrl}/`), baseUrl);
  assert.throws(() => normalizeBaseUrl('file:///tmp/device'), DeviceApiError);
  assert.throws(() => normalizeBaseUrl('http://user:pass@example.com'), DeviceApiError);
});

test('logs in and returns image-analysis algorithms', async () => {
  const result = await loginAndListAlgorithms({ baseUrl, account: 'admin', password: 'secret' });
  assert.equal(result.mtk, 'token-1');
  assert.equal(result.algorithms.length, 1);
  assert.equal(result.algorithms[0].algorithmId, '7602');
});

test('creates a task, uploads image chunks, detects targets, and cancels the task', async () => {
  const result = await runImageAnalysis({
    baseUrl,
    mtk: 'token-1',
    algorithmCode: '7602',
    taskId: 'image-task-1',
    fileName: 'face.jpg',
    imageBuffer: Buffer.from('0123456789'),
  });

  assert.equal(result.upload.totalSize, 10);
  assert.equal(result.upload.totalChunks, 3);
  assert.equal(result.response.resData.areaList[0].targetList[0].confidence[0].label, 'face');

  const uploadRequests = requests.filter(item => item.url === '/gtw/cwai/atomic/model/uploadTemp');
  assert.equal(uploadRequests.length, 3);
  assert.doesNotMatch(uploadRequests[0].body.toString('latin1'), /name="uploadId"/);
  assert.match(uploadRequests[1].body.toString('latin1'), /name="uploadId"\r\n\r\nupload-1/);

  await cancelImageTask({
    baseUrl,
    mtk: 'token-1',
    algorithmCode: '7602',
    taskId: 'image-task-1',
  });
});
