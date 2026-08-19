'use strict';

const crypto = require('crypto');

const DEFAULT_CHUNK_SIZE = 8 * 1024 * 1024;

class DeviceApiError extends Error {
  constructor(message, options = {}) {
    super(message);
    this.name = 'DeviceApiError';
    this.status = options.status || 0;
    this.path = options.path || '';
    this.payload = options.payload || null;
  }
}

function normalizeBaseUrl(input) {
  const value = String(input || '').trim();
  if (!value) throw new DeviceApiError('设备地址不能为空');

  let url;
  try {
    url = new URL(value);
  } catch (_) {
    throw new DeviceApiError('设备地址格式无效，请填写完整的 http:// 或 https:// 地址');
  }
  if (url.protocol !== 'http:' && url.protocol !== 'https:') {
    throw new DeviceApiError('设备地址只支持 HTTP 或 HTTPS');
  }
  if (url.username || url.password) {
    throw new DeviceApiError('设备地址中不能包含用户名或密码');
  }
  if (url.search || url.hash) {
    throw new DeviceApiError('设备地址中不能包含查询参数或锚点');
  }
  return url.toString().replace(/\/$/, '');
}

function md5Password(password) {
  return crypto.createHash('md5').update(String(password || ''), 'utf8').digest('hex');
}

function payloadData(payload) {
  return payload && payload.resData && payload.resData.resData
    ? payload.resData.resData
    : (payload && payload.resData) || {};
}

function errorMessage(payload, fallback) {
  const item = payload && Array.isArray(payload.resMsg) ? payload.resMsg[0] : null;
  return (item && (item.msgText || item.messageKey || item.msgCode)) || fallback;
}

async function fetchWithTimeout(url, options, timeoutMs) {
  if (typeof fetch !== 'function' || typeof FormData !== 'function' || typeof Blob !== 'function') {
    throw new DeviceApiError('当前 Node.js 版本不支持内置 fetch/FormData，请使用 Node.js 18 或更高版本');
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, { ...options, signal: controller.signal });
  } catch (error) {
    if (error && error.name === 'AbortError') {
      throw new DeviceApiError(`请求超时（${timeoutMs} ms）`);
    }
    if (error instanceof DeviceApiError) throw error;
    throw new DeviceApiError(`无法访问设备：${error.message}`);
  } finally {
    clearTimeout(timer);
  }
}

async function parseDeviceResponse(response, path) {
  const text = await response.text();
  let payload;
  try {
    payload = text ? JSON.parse(text) : {};
  } catch (_) {
    throw new DeviceApiError(`设备接口返回了非 JSON 数据：${path}`, {
      status: response.status,
      path,
    });
  }

  if (!response.ok || payload.resCode !== 1) {
    throw new DeviceApiError(errorMessage(payload, `设备接口调用失败：${path}`), {
      status: response.status,
      path,
      payload,
    });
  }
  return payload;
}

async function postJson(baseUrl, path, body, mtk = '', timeoutMs = 30000) {
  const response = await fetchWithTimeout(`${normalizeBaseUrl(baseUrl)}${path}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(mtk ? { mtk, token: mtk } : {}),
    },
    body: JSON.stringify(body || {}),
  }, timeoutMs);
  return parseDeviceResponse(response, path);
}

async function postMultipart(baseUrl, path, form, mtk, timeoutMs = 120000) {
  const response = await fetchWithTimeout(`${normalizeBaseUrl(baseUrl)}${path}`, {
    method: 'POST',
    headers: { mtk, token: mtk },
    body: form,
  }, timeoutMs);
  return parseDeviceResponse(response, path);
}

async function loginAndListAlgorithms({ baseUrl, account, password }) {
  if (!String(account || '').trim()) throw new DeviceApiError('登录账号不能为空');
  if (!String(password || '')) throw new DeviceApiError('登录密码不能为空');

  const normalizedBaseUrl = normalizeBaseUrl(baseUrl);
  const login = await postJson(normalizedBaseUrl, '/gtw/cwai/login/DoLogin', {
    account: String(account).trim(),
    pwd: md5Password(password),
  });
  const loginData = payloadData(login);
  if (!loginData.mtk) throw new DeviceApiError('登录成功响应中缺少 mtk');

  const page = await postJson(normalizedBaseUrl, '/gtw/cwai/algorithm/page', {
    algorithmUsage: '2',
    algorithmName: '',
    supplier: '',
    algorithmId: '',
    algorithmCategory: '',
    pageNum: 1,
    pageSize: 1000,
  }, loginData.mtk);
  const rows = payloadData(page).rows;

  return {
    baseUrl: normalizedBaseUrl,
    mtk: loginData.mtk,
    accountName: loginData.accountName || String(account).trim(),
    passwordChangeRequired: loginData.passwordChangeRequired === true,
    algorithms: Array.isArray(rows) ? rows : [],
  };
}

function positiveInteger(value) {
  const number = Number(value);
  return Number.isSafeInteger(number) && number > 0 ? number : 0;
}

async function cancelUpload(baseUrl, mtk, uploadId) {
  if (!uploadId) return;
  await postJson(baseUrl, '/gtw/cwai/atomic/model/cancelUpload', { uploadId }, mtk, 30000);
}

async function uploadImage({ baseUrl, mtk, fileName, imageBuffer }) {
  if (!Buffer.isBuffer(imageBuffer) || imageBuffer.length === 0) {
    throw new DeviceApiError('请选择非空图片');
  }
  const safeFileName = String(fileName || 'image.jpg').replace(/[\r\n"\\/]/g, '_').slice(0, 255);
  const capabilitiesResponse = await postJson(
    baseUrl,
    '/gtw/cwai/atomic/model/uploadCapabilities',
    {},
    mtk,
    30000
  );
  const capabilities = payloadData(capabilitiesResponse);
  const maxEncodedImageBytes = positiveInteger(capabilities.maxEncodedImageBytes);
  const availableBytes = positiveInteger(capabilities.availableForNewUploadsBytes);
  if (maxEncodedImageBytes && imageBuffer.length > maxEncodedImageBytes) {
    throw new DeviceApiError(`图片大小 ${imageBuffer.length} 字节超过设备上限 ${maxEncodedImageBytes} 字节`);
  }
  if (availableBytes && imageBuffer.length > availableBytes) {
    throw new DeviceApiError(`设备安全可用空间不足：需要 ${imageBuffer.length} 字节，可用 ${availableBytes} 字节`);
  }

  const maxChunkSize = positiveInteger(capabilities.maxChunkSize) || DEFAULT_CHUNK_SIZE;
  const chunkSize = Math.min(DEFAULT_CHUNK_SIZE, maxChunkSize);
  const totalChunks = Math.ceil(imageBuffer.length / chunkSize);
  const clientRequestId = crypto.randomUUID
    ? crypto.randomUUID()
    : crypto.randomBytes(16).toString('hex');
  let uploadId = '';

  try {
    for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
      const start = chunkIndex * chunkSize;
      const end = Math.min(start + chunkSize, imageBuffer.length);
      const chunk = imageBuffer.subarray(start, end);
      const form = new FormData();
      form.append('file', new Blob([chunk]), safeFileName);
      form.append('purpose', 'image');
      form.append('chunkIndex', String(chunkIndex));
      form.append('totalChunks', String(totalChunks));
      form.append('totalSize', String(imageBuffer.length));
      form.append('chunkSize', String(chunk.length));
      form.append('clientRequestId', clientRequestId);
      if (uploadId) form.append('uploadId', uploadId);

      const uploadResponse = await postMultipart(
        baseUrl,
        '/gtw/cwai/atomic/model/uploadTemp',
        form,
        mtk
      );
      const uploadData = payloadData(uploadResponse);
      uploadId = uploadData.uploadId || uploadId;
      if (!uploadId) throw new DeviceApiError('上传响应中缺少 uploadId');
      const expectedNext = chunkIndex + 1;
      if (Number(uploadData.nextChunkIndex) !== expectedNext) {
        throw new DeviceApiError(`设备期望分片 ${uploadData.nextChunkIndex}，本地已发送至 ${expectedNext}`);
      }
      if (chunkIndex === totalChunks - 1 && uploadData.complete !== true) {
        throw new DeviceApiError('最后一个分片发送后，设备未确认上传完成');
      }
    }
  } catch (error) {
    if (uploadId) await cancelUpload(baseUrl, mtk, uploadId).catch(() => {});
    throw error;
  }

  return {
    uploadId,
    totalSize: imageBuffer.length,
    totalChunks,
    capabilities,
  };
}

async function runImageAnalysis({ baseUrl, mtk, algorithmCode, taskId, fileName, imageBuffer }) {
  if (!String(mtk || '').trim()) throw new DeviceApiError('mtk 不能为空，请重新连接设备');
  if (!String(algorithmCode || '').trim()) throw new DeviceApiError('请选择图片分析算法');
  if (!String(taskId || '').trim()) throw new DeviceApiError('taskId 不能为空');

  const startedAt = Date.now();
  await postJson(baseUrl, '/gtw/cwai/aihost/PTaskCreate', {
    mvDebug: 'Cosmo-Debug',
    taskId: String(taskId).trim(),
    algorithmCode: String(algorithmCode).trim(),
    algorithmUpdateTime: String(Date.now()),
  }, mtk, 120000);

  let upload = null;
  try {
    upload = await uploadImage({ baseUrl, mtk, fileName, imageBuffer });
    const detection = await postJson(baseUrl, '/gtw/cwai/aihost/PTaskDetectPic', {
      taskId: String(taskId).trim(),
      algorithmCode: String(algorithmCode).trim(),
      uploadId: upload.uploadId,
    }, mtk, 120000);
    return {
      taskId: String(taskId).trim(),
      algorithmCode: String(algorithmCode).trim(),
      elapsedMs: Date.now() - startedAt,
      upload: {
        totalSize: upload.totalSize,
        totalChunks: upload.totalChunks,
      },
      response: detection,
    };
  } catch (error) {
    if (upload && upload.uploadId) {
      await cancelUpload(baseUrl, mtk, upload.uploadId).catch(() => {});
    }
    await cancelImageTask({ baseUrl, mtk, algorithmCode, taskId }).catch(() => {});
    throw error;
  }
}

async function cancelImageTask({ baseUrl, mtk, algorithmCode, taskId }) {
  return postJson(baseUrl, '/gtw/cwai/aihost/PTaskCancle', {
    mvDebug: 'Cosmo-Debug',
    taskId: String(taskId || '').trim(),
    algorithmCode: String(algorithmCode || '').trim(),
  }, mtk, 30000);
}

module.exports = {
  DeviceApiError,
  normalizeBaseUrl,
  md5Password,
  payloadData,
  loginAndListAlgorithms,
  uploadImage,
  runImageAnalysis,
  cancelImageTask,
};
