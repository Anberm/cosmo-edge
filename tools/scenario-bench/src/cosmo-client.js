// cosmo-client.js — Thin HTTP client for the CosmoEdge /gtw/cwai API.
//
// Authentication convention mirrors the frontend (src/web/src/utils/request.js):
//   - Login with { account, pwd: md5(password).toUpperCase() }
//   - Subsequent requests carry the returned mtk in the `mtk` / `token` headers.
// All routes are POST unless noted. Error handling follows the wire contract:
//   { resCode: 1, resData: {...}, resMsg: [...] }  -> success
//   { resCode: 0|<non-1>, resMsg: [{msgCode, msgText}] } -> failure

import crypto from 'node:crypto';

const API_PREFIX = '/gtw/cwai';
const DEFAULT_TIMEOUT_MS = 20_000;
const LONG_TIMEOUT_MS = 10 * 60_000;  // AddVideo / layout save can be slow
const LONG_TIMEOUT_ROUTES = new Set([
  '/Camera/AddVideo',
  '/algorithm/layout/save',
  '/atomic/model/uploadTemp',
]);

/** MD5-hashed + uppercased password, matching the backend's ToUpper(passwdMd5) comparison. */
export function hashPassword(plain) {
  return crypto.createHash('md5').update(String(plain), 'utf8').digest('hex').toUpperCase();
}

export class CosmoClient {
  /**
   * @param {object} opts
   * @param {string} opts.base   Device base URL, e.g. http://192.168.1.10:8080
   * @param {string} [opts.user] Login account.
   * @param {string} [opts.password] Plain-text password (hashed internally).
   * @param {string} [opts.token] Existing short-lived device token.
   * @param {string} [opts.lang] Accept-Language header value, default zh-CN.
   */
  constructor({ base, user, password, token = null, lang = 'zh-CN' }) {
    this.base = base.replace(/\/+$/, '');
    this.user = user;
    this.password = password;
    this.lang = lang;
    this.mtk = token;
  }

  /** Log in and store the mtk token. Returns the login response resData. */
  async login() {
    if (this.mtk) return { mtk: this.mtk };
    if (!this.user || !this.password) throw new Error('login requires user/password or an existing token');
    const res = await this._post('/login/dologin', {
      account: this.user,
      pwd: hashPassword(this.password),
    });
    this.mtk = res.resData?.mtk;
    if (!this.mtk) {
      throw new Error('Login succeeded but no mtk returned');
    }
    return res.resData;
  }

  async queryDeviceInfo() {
    return (await this._post('/System/QueryDeviceInfo', {})).resData;
  }

  async queryHardwareResource() {
    return (await this._post('/System/QueryHardwareResource', {})).resData;
  }

  /** Save or update an algorithm orchestration layout. payload = parsed export JSON. */
  async layoutSave(payload) {
    return this._post('/algorithm/layout/save', payload);
  }

  /**
   * Upload one chunk of a file via multipart/form-data to the temp store.
   * Mirrors the frontend uploadVideoByChunk (src/web/src/views/gam/taskManager/index.vue).
   * The final chunk returns resData.filePath on the device.
   * @param {Buffer} chunkBuf  raw chunk bytes
   * @param {string} fileName  original file name (used for extension + naming)
   * @param {object} meta upload session and chunk metadata
   * @returns {Promise<object>} wire response with the canonical resData.uploadId
   */
  async uploadTempChunk(chunkBuf, fileName, {
    uploadId,
    clientRequestId,
    purpose,
    chunkIndex,
    totalChunks,
    totalSize,
    chunkSize,
  }) {
    const fields = {
      file: { value: chunkBuf, filename: fileName },
      purpose,
      clientRequestId,
      chunkIndex: String(chunkIndex),
      totalChunks: String(totalChunks),
      totalSize: String(totalSize),
      chunkSize: String(chunkSize),
    };
    // The first chunk creates the server-side session. Later chunks must use
    // the opaque upload ID returned by that first response.
    if (uploadId) fields.uploadId = uploadId;
    return this._postMultipart('/atomic/model/uploadTemp', fields);
  }

  async cancelUpload(uploadId) {
    return this._post('/atomic/model/cancelUpload', { uploadId });
  }

  /** Add an RTSP camera channel. */
  async cameraAdd(payload) {
    return this._post('/Camera/Add', payload);
  }

  /** Add a local video channel. */
  async cameraAddVideo(payload) {
    return this._post('/Camera/AddVideo', payload);
  }

  /** Query existing channels (paginated). */
  async cameraPage(payload) {
    return (await this._post('/Camera/Page', payload)).resData;
  }

  /** Batch delete channels by videoChannelId. */
  async cameraBatchDelete(videoChannelIds) {
    return this._post('/Camera/BatchDelete', { videoChannelIds });
  }

  /** Single-channel save/update task. */
  async taskSaveOrUpdate(payload) {
    return this._post('/Task/SaveOrUpdate', payload);
  }

  /**
   * Apply the same taskConfig/scheduleId/algorithmId to multiple channels at once.
   * NOTE: this auto-enables tasks on each channel (see design doc §任务绑定).
   *
   * The backend DTO (MsgApplyParamsBatchRecv → MsgChannelTask::from_json, VideoTaskDto.cc:20)
   * requires a top-level `channelId` and `algorithmId` via j.at(). For a batch call the
   * `channelId` is a template value; the engine iterates `targetChannelIds` instead. We pass
   * the first target channel as channelId to satisfy deserialization.
   * @returns {import('./types').ApplyResult}
   */
  async taskApplyParamsBatch({ algorithmId, scheduleId, taskConfig, targetChannelIds }) {
    const res = await this._post('/Task/ApplyParamsBatch', {
      channelId: targetChannelIds[0],
      algorithmId,
      scheduleId,
      taskConfig,
      targetChannelIds,
    });
    return { failedList: res.resData?.failedList ?? [] };
  }

  /** Batch switch tasks on/off. tasks = [{ id, channelId, algorithmId, enable }]. */
  async taskBatchSwitch(tasks) {
    const wireTasks = tasks.map(({ enable, ...task }) => ({
      ...task,
      switch: enable,
    }));
    const res = await this._post('/Task/BatchSwitchTask', { tasks: wireTasks });
    return { failedList: res.resData?.failedList ?? [] };
  }

  /** Query running detail for the given taskIds (NOT channelIds). */
  async taskRunningDetail(taskIds) {
    return (await this._post('/Task/RunningDetail', { tasks: taskIds })).resData;
  }

  async eventPage(payload) {
    return (await this._post('/event/page', payload)).resData;
  }

  /** Start or join a live preview and wait until its first frame reaches SRS. */
  async requestLiveStream({ channelId, algorithmId = '' }) {
    return (await this._post('/LiveStream/RequestLiveStream', { channelId, algorithmId })).resData?.stream;
  }

  async streamKeepAlive({ channelId, algorithmId = '' }) {
    return this._post('/LiveStream/StreamKeepAlive', { channelId, algorithmId });
  }

  async streamStop({ channelId, algorithmId = '' }) {
    return this._post('/LiveStream/StreamStop', { channelId, algorithmId });
  }

  /**
   * Multipart POST. Builds a multipart/form-data body (manual, to avoid FormData/Blob
   * quirks), sends it, and applies the same wire-contract normalization as _post.
   * @param {string} path route path after /gtw/cwai
   * @param {object} fields  scalars are sent as text; {value:Buffer,filename:string} as file
   * @returns {Promise<object>} full wire response
   */
  async _postMultipart(path, fields) {
    const url = `${this.base}${API_PREFIX}${path}`;
    const boundary = '----bench' + crypto.randomBytes(8).toString('hex');
    const enc = (s) => s;
    const textParts = [];
    for (const [k, v] of Object.entries(fields)) {
      if (v && typeof v === 'object' && 'value' in v) continue;  // file field handled below
      textParts.push(`--${boundary}\r\nContent-Disposition: form-data; name="${k}"\r\n\r\n${v}\r\n`);
    }
    const headBuf = Buffer.from(textParts.join(''), 'utf8');
    const bufs = [headBuf];
    for (const [k, v] of Object.entries(fields)) {
      if (v && typeof v === 'object' && 'value' in v) {
        const fileHead = Buffer.from(
          `--${boundary}\r\nContent-Disposition: form-data; name="${k}"; filename="${v.filename}"\r\nContent-Type: application/octet-stream\r\n\r\n`,
          'utf8',
        );
        bufs.push(fileHead, Buffer.from(v.value), Buffer.from('\r\n', 'utf8'));
      }
    }
    bufs.push(Buffer.from(`--${boundary}--\r\n`, 'utf8'));
    const body = Buffer.concat(bufs);

    const headers = {
      'Content-Type': `multipart/form-data; boundary=${boundary}`,
      'Accept-Language': this.lang,
    };
    if (this.mtk) {
      headers.mtk = this.mtk;
      headers.token = this.mtk;
    }
    const timeout = LONG_TIMEOUT_ROUTES.has(path) ? LONG_TIMEOUT_MS : DEFAULT_TIMEOUT_MS;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    let resp;
    try {
      resp = await fetch(url, { method: 'POST', headers, body, signal: controller.signal, duplex: 'half' });
    } catch (err) {
      if (err.name === 'AbortError') {
        throw new Error(`Request timed out after ${timeout}ms: POST ${path}`);
      }
      throw new Error(`Network error on POST ${path}: ${err.message}`);
    } finally {
      clearTimeout(timer);
    }
    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status} on POST ${path}`);
    }
    let data;
    try {
      data = await resp.json();
    } catch {
      throw new Error(`Non-JSON response on POST ${path}`);
    }
    if (data.resCode !== 1) {
      const firstMsg = Array.isArray(data.resMsg) ? data.resMsg[0] : null;
      const text = firstMsg?.msgText || firstMsg?.msgKey || data.msg || 'unknown error';
      const code = firstMsg?.msgCode || '';
      const err = new Error(`API error on POST ${path}: ${text}${code ? ` (code ${code})` : ''}`);
      err.resCode = data.resCode;
      err.msgCode = code;
      throw err;
    }
    return data;
  }

  /**
   * Core POST. Prepends the /gtw/cwai prefix, injects auth headers, normalizes errors.
   * @param {string} path route path after /gtw/cwai, e.g. /System/QueryHardwareResource
   * @param {object} body JSON body
   * @returns {Promise<object>} full wire response (with resCode/resData/resMsg)
   */
  async _post(path, body) {
    const url = `${this.base}${API_PREFIX}${path}`;
    const headers = {
      'Content-Type': 'application/json',
      'Accept-Language': this.lang,
    };
    if (this.mtk || path.toLowerCase() === '/login/dologin') {
      if (this.mtk) {
        headers.mtk = this.mtk;
        headers.token = this.mtk;
      }
    }
    const timeout = LONG_TIMEOUT_ROUTES.has(path) ? LONG_TIMEOUT_MS : DEFAULT_TIMEOUT_MS;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    let resp;
    try {
      resp = await fetch(url, {
        method: 'POST',
        headers,
        body: JSON.stringify(body ?? {}),
        signal: controller.signal,
      });
    } catch (err) {
      if (err.name === 'AbortError') {
        throw new Error(`Request timed out after ${timeout}ms: POST ${path}`);
      }
      throw new Error(`Network error on POST ${path}: ${err.message}`);
    } finally {
      clearTimeout(timer);
    }

    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status} on POST ${path}`);
    }
    let data;
    try {
      data = await resp.json();
    } catch {
      throw new Error(`Non-JSON response on POST ${path}`);
    }
    // Wire contract: resCode === 1 means success.
    if (data.resCode !== 1) {
      const firstMsg = Array.isArray(data.resMsg) ? data.resMsg[0] : null;
      const text = firstMsg?.msgText || firstMsg?.msgKey || data.msg || 'unknown error';
      const code = firstMsg?.msgCode || '';
      const err = new Error(`API error on POST ${path}: ${text}${code ? ` (code ${code})` : ''}`);
      err.resCode = data.resCode;
      err.msgCode = code;
      throw err;
    }
    return data;
  }
}
