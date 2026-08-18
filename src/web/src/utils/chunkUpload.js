import { formatActionableApiError, normalizeApiError } from '@/utils/apiError'
import { message } from '@/utils/message'
import { t, translateApiMessage } from '@/i18n'

export const UPLOAD_CHUNK_SIZE = 8 * 1024 * 1024

export const UploadPurpose = Object.freeze({
  MODEL_COMPONENT: 'model-component',
  MODEL_ARCHIVE: 'model-archive',
  VIDEO: 'video',
  FACE_IMPORT: 'face-import',
  AUDIO: 'audio',
  ALGORITHM: 'algorithm',
  UPGRADE: 'upgrade',
  IMAGE: 'image',
  MODEL_AUTHORIZATION_CERTIFICATE: 'model-authorization-certificate'
})

const validPurposes = new Set(Object.values(UploadPurpose))

const extractPayload = response => {
  const envelope = response?.data || response || {}
  return envelope?.resData?.resData || envelope?.resData || envelope
}

const extractUploadId = response => {
  const envelope = response?.data || response || {}
  const payload = extractPayload(response)
  return payload?.uploadId || envelope?.uploadId || ''
}

const extractFilePath = response => {
  const envelope = response?.data || response || {}
  const payload = extractPayload(response)
  return payload?.filePath || envelope?.filePath || ''
}

const extractCapabilities = response => {
  const payload = extractPayload(response)
  const number = (key, missingValue = 0) => {
    const rawValue = payload?.[key]
    if (rawValue === undefined || rawValue === null || rawValue === '') {
      return missingValue
    }
    const value = Number(rawValue)
    if (!Number.isFinite(value) || value < 0 || !Number.isInteger(value)) {
      return missingValue
    }
    // Capability byte counts are encoded as decimal strings to preserve the
    // server's uint64 values. A browser File cannot exceed MAX_SAFE_INTEGER,
    // so clamping a larger server allowance is conservative and comparison-safe.
    return Math.min(value, Number.MAX_SAFE_INTEGER)
  }
  return {
    maxTotalSize: number('maxTotalSize'),
    maxChunkSize: number('maxChunkSize'),
    maxChunks: number('maxChunks'),
    idleTimeoutMs: number('idleTimeoutMs'),
    availableForNewUploadsBytes: number('availableForNewUploadsBytes', null),
    maxEncodedImageBytes: number('maxEncodedImageBytes'),
    maxImagePixels: number('maxImagePixels'),
    resumable: payload?.resumable === true,
    persistentAcrossRestart: payload?.persistentAcrossRestart === true
  }
}

const makePreflightError = (code, message, details, recommendedAction) => {
  const error = new Error(message)
  error.resCode = 0
  error.resMsg = [{
    msgCode: code,
    messageKey: `api.error.${code}`,
    msgText: message,
    details,
    retryable: code === 'storageReserveReached',
    recommendedAction
  }]
  return error
}

const throwVisiblePreflightError = error => {
  const actionable = formatActionableApiError(error, translateApiMessage, t)
  message.error(actionable.displayMessage || error.message)
  throw error
}

const validateImageCapabilities = async (file, capabilities) => {
  if (
    capabilities.maxEncodedImageBytes &&
    file.size > capabilities.maxEncodedImageBytes
  ) {
    throwVisiblePreflightError(makePreflightError(
      'imageInputTooLarge',
      'Encoded image exceeds the device decoding input limit',
      {
        actualBytes: file.size,
        limitBytes: capabilities.maxEncodedImageBytes
      },
      'RESIZE_OR_RECOMPRESS_IMAGE'
    ))
  }
  if (
    !capabilities.maxImagePixels ||
    typeof globalThis.createImageBitmap !== 'function'
  ) {
    return
  }

  let bitmap
  try {
    bitmap = await globalThis.createImageBitmap(file)
    const pixels = bitmap.width * bitmap.height
    if (!Number.isSafeInteger(pixels) || pixels > capabilities.maxImagePixels) {
      throwVisiblePreflightError(makePreflightError(
        'imageResolutionTooLarge',
        'Image resolution exceeds the device processing capability',
        {
          actualCount: pixels,
          limitCount: capabilities.maxImagePixels
        },
        'RESIZE_IMAGE'
      ))
    }
  } catch (error) {
    if (error?.resMsg) throw error
    // Format validity remains authoritative at the decoder. Some browsers do
    // not support createImageBitmap for every image format accepted by Cosmo.
  } finally {
    bitmap?.close?.()
  }
}

const createClientRequestId = () => {
  const cryptoApi = globalThis.crypto
  if (typeof cryptoApi?.randomUUID === 'function') {
    return cryptoApi.randomUUID()
  }
  if (typeof cryptoApi?.getRandomValues === 'function') {
    const values = new Uint32Array(4)
    cryptoApi.getRandomValues(values)
    return Array.from(values, value => value.toString(16).padStart(8, '0')).join('')
  }
  return `${Date.now()}_${Math.random().toString(36).slice(2)}_${Math.random().toString(36).slice(2)}`
}

const resumeStorageKey = (file, purpose) => {
  const identity = [
    purpose,
    file.name,
    String(file.size || 0),
    String(file.lastModified || 0)
  ].join('|')
  return `cosmo.upload.resume.v1:${encodeURIComponent(identity)}`
}

const resolveClientRequestId = (file, purpose, persistent) => {
  if (!persistent) return { clientRequestId: createClientRequestId(), storageKey: '' }
  const storageKey = resumeStorageKey(file, purpose)
  try {
    const existing = globalThis.localStorage?.getItem(storageKey)
    if (existing && /^[A-Za-z0-9_.:-]{1,128}$/.test(existing)) {
      return { clientRequestId: existing, storageKey }
    }
    const clientRequestId = createClientRequestId()
    globalThis.localStorage?.setItem(storageKey, clientRequestId)
    return { clientRequestId, storageKey }
  } catch (_) {
    return { clientRequestId: createClientRequestId(), storageKey: '' }
  }
}

const clearResumeIdentity = storageKey => {
  if (!storageKey) return
  try {
    globalThis.localStorage?.removeItem(storageKey)
  } catch (_) {
    // Storage is an optional resume aid; server-side TTL remains authoritative.
  }
}

/**
 * Upload a file sequentially through the authenticated UploadTemp endpoint.
 *
 * Protocol:
 * - chunk 0 omits uploadId; the server creates and returns an opaque uploadId;
 * - later chunks carry that server-issued uploadId;
 * - the final response contains uploadId and, during the R1 compatibility
 *   window, an opaque upload:// filePath alias (never a server path).
 */
export const uploadFileInChunks = async (file, options) => {
  const {
    purpose,
    uploadChunk,
    cancelUpload,
    getCapabilities,
    chunkSize = UPLOAD_CHUNK_SIZE,
    onProgress,
    onStateChange,
    onResume
  } = options || {}

  if (!file || typeof file.slice !== 'function' || !file.name) {
    throw new TypeError('A named File or Blob is required')
  }
  if (!validPurposes.has(purpose)) {
    throw new TypeError('A supported upload purpose is required')
  }
  if (typeof uploadChunk !== 'function') {
    throw new TypeError('uploadChunk must be a function')
  }
  if (!Number.isSafeInteger(chunkSize) || chunkSize <= 0) {
    throw new RangeError('chunkSize must be a positive safe integer')
  }

  const totalSize = Number(file.size || 0)
  if (
    !Number.isSafeInteger(totalSize) ||
    totalSize <= 0
  ) {
    throw new RangeError('A non-empty file with a safe integer size is required')
  }
  let capabilities = {}
  if (typeof getCapabilities === 'function') {
    try {
      capabilities = extractCapabilities(await getCapabilities())
    } catch (error) {
      const actionable = formatActionableApiError(error, translateApiMessage, t)
      message.error(actionable.displayMessage || t('api.networkError'))
      throw error
    }
  }
  if (capabilities.maxTotalSize && totalSize > capabilities.maxTotalSize) {
    throwVisiblePreflightError(makePreflightError(
      'uploadFilePolicyLimit',
      'File exceeds the deployment upload policy',
      { actualBytes: totalSize, limitBytes: capabilities.maxTotalSize },
      'CHANGE_DEPLOYMENT_POLICY'
    ))
  }
  if (
    capabilities.availableForNewUploadsBytes !== null &&
    totalSize > capabilities.availableForNewUploadsBytes
  ) {
    throwVisiblePreflightError(makePreflightError(
      'storageReserveReached',
      'Insufficient safe disk space for this upload',
      {
        requiredBytes: totalSize,
        availableBytes: capabilities.availableForNewUploadsBytes
      },
      'FREE_DISK_SPACE'
    ))
  }
  if (purpose === UploadPurpose.IMAGE) {
    await validateImageCapabilities(file, capabilities)
  }
  const effectiveChunkSize = capabilities.maxChunkSize
    ? Math.min(chunkSize, capabilities.maxChunkSize)
    : chunkSize
  const totalChunks = Math.ceil(totalSize / effectiveChunkSize)
  if (capabilities.maxChunks && totalChunks > capabilities.maxChunks) {
    throwVisiblePreflightError(makePreflightError(
      'uploadChunkPolicyLimit',
      'Upload uses more chunks than the deployment policy',
      { actualCount: totalChunks, limitCount: capabilities.maxChunks },
      'USE_LARGER_CHUNKS_OR_CHANGE_POLICY'
    ))
  }
  if (typeof onStateChange === 'function') {
    onStateChange({ state: 'uploading', capabilities, totalSize, totalChunks })
  }
  let uploadId = ''
  let lastResponse
  let resumed = false
  const { clientRequestId, storageKey } =
    resolveClientRequestId(file, purpose, capabilities.persistentAcrossRestart)

  try {
    let chunkIndex = 0
    while (chunkIndex < totalChunks) {
      const start = chunkIndex * effectiveChunkSize
      const end = Math.min(totalSize, start + effectiveChunkSize)
      const formData = new FormData()
      formData.append('file', file.slice(start, end), file.name)
      formData.append('purpose', purpose)
      formData.append('chunkIndex', String(chunkIndex))
      formData.append('totalChunks', String(totalChunks))
      formData.append('totalSize', String(totalSize))
      formData.append('chunkSize', String(end - start))
      formData.append('clientRequestId', clientRequestId)
      if (uploadId) formData.append('uploadId', uploadId)

      try {
        lastResponse = await uploadChunk(formData)
      } catch (error) {
        // The server records each chunk before acknowledging it. One retry with
        // the same alias/upload ID is safe whether the first attempt arrived or
        // its response was lost.
        const normalized = normalizeApiError(error)
        if (normalized.code || (normalized.status && normalized.status < 500)) throw error
        lastResponse = await uploadChunk(formData)
      }
      const responseUploadId = extractUploadId(lastResponse)
      if (!responseUploadId) {
        throw new Error('Upload response did not contain uploadId')
      }
      if (uploadId && responseUploadId !== uploadId) {
        throw new Error('Upload response changed uploadId')
      }
      uploadId = responseUploadId

      const payload = extractPayload(lastResponse)
      const nextChunkIndex = Number(payload?.nextChunkIndex)
      if (
        !Number.isInteger(nextChunkIndex) ||
        nextChunkIndex <= chunkIndex ||
        nextChunkIndex > totalChunks
      ) {
        throw new Error('Upload response contained an invalid nextChunkIndex')
      }
      const isFinalChunk = nextChunkIndex === totalChunks
      if (payload?.complete !== isFinalChunk) {
        throw new Error('Upload response contained an invalid completion state')
      }

      if (nextChunkIndex > chunkIndex + 1) {
        resumed = true
        if (typeof onResume === 'function') {
          onResume({
            uploadId,
            nextChunkIndex,
            totalChunks,
            persistentAcrossRestart: capabilities.persistentAcrossRestart
          })
        } else {
          message.info(t('common.uploadResumed'))
        }
        if (typeof onStateChange === 'function') {
          onStateChange({
            state: 'resumed',
            uploadId,
            nextChunkIndex,
            totalChunks,
            capabilities
          })
        }
      }
      const uploadedBytes = Math.min(totalSize, nextChunkIndex * effectiveChunkSize)
      if (typeof onProgress === 'function') {
        onProgress({
          uploadId,
          uploadedBytes,
          totalSize,
          chunkIndex: nextChunkIndex - 1,
          totalChunks,
          percent: Math.round((uploadedBytes * 100) / totalSize),
          resumed
        })
      }
      chunkIndex = nextChunkIndex
    }
  } catch (error) {
    const normalized = normalizeApiError(error)
    const canResume =
      capabilities.resumable &&
      (normalized.retryable || !normalized.code || (normalized.status || 0) >= 500)
    if (typeof onStateChange === 'function') {
      onStateChange({
        state: 'failed',
        error,
        uploadId,
        canResume,
        persistentAcrossRestart: capabilities.persistentAcrossRestart
      })
    } else {
      const actionable = formatActionableApiError(error, translateApiMessage, t)
      message.error(actionable.displayMessage || t('api.requestFailed'))
      if (canResume) {
        message.warning(t('common.uploadInterruptedResumable'))
      }
    }
    try {
      error.uploadCanResume = canResume
      error.uploadPersistentAcrossRestart = capabilities.persistentAcrossRestart
    } catch (_) {
      // Some host errors are immutable; the visible resume hint was already shown.
    }
    if (!canResume && uploadId && typeof cancelUpload === 'function') {
      try {
        await cancelUpload({ uploadId })
        clearResumeIdentity(storageKey)
      } catch (_) {
        // Preserve the original upload failure. Server-side TTL cleanup remains
        // the fallback when cancellation cannot be delivered.
      }
    }
    throw error
  }

  if (typeof onStateChange === 'function') {
    onStateChange({ state: 'completed', uploadId, totalSize, totalChunks })
  }
  // A completed upload no longer needs its browser-side resume alias. The
  // canonical uploadId returned below remains valid for the business request.
  clearResumeIdentity(storageKey)
  return {
    uploadId,
    filePath: extractFilePath(lastResponse),
    totalSize,
    resumed,
    persistentAcrossRestart: capabilities.persistentAcrossRestart,
    response: lastResponse
  }
}
