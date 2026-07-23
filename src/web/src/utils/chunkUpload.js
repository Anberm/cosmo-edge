export const UPLOAD_CHUNK_SIZE = 8 * 1024 * 1024
export const UPLOAD_MAX_TOTAL_SIZE = 500 * 1024 * 1024
export const UPLOAD_MAX_CHUNKS = 128

export const UploadPurpose = Object.freeze({
  MODEL_COMPONENT: 'model-component',
  MODEL_ARCHIVE: 'model-archive',
  VIDEO: 'video',
  FACE_IMPORT: 'face-import',
  AUDIO: 'audio',
  ALGORITHM: 'algorithm',
  UPGRADE: 'upgrade'
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

/**
 * Upload a file sequentially through the authenticated UploadTemp endpoint.
 *
 * Protocol:
 * - chunk 0 omits uploadId; the server creates and returns an opaque uploadId;
 * - later chunks carry that server-issued uploadId;
 * - the final response contains uploadId and, during the R1 compatibility
 *   window, the exact registered legacy filePath.
 */
export const uploadFileInChunks = async (file, options) => {
  const {
    purpose,
    uploadChunk,
    cancelUpload,
    chunkSize = UPLOAD_CHUNK_SIZE,
    onProgress
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
  if (!Number.isSafeInteger(chunkSize) || chunkSize <= 0 || chunkSize > UPLOAD_CHUNK_SIZE) {
    throw new RangeError(`chunkSize must be between 1 and ${UPLOAD_CHUNK_SIZE}`)
  }

  const totalSize = Number(file.size || 0)
  if (
    !Number.isSafeInteger(totalSize) ||
    totalSize <= 0 ||
    totalSize > UPLOAD_MAX_TOTAL_SIZE
  ) {
    throw new RangeError('Empty or oversized files cannot be uploaded')
  }
  const totalChunks = Math.ceil(totalSize / chunkSize)
  if (totalChunks > UPLOAD_MAX_CHUNKS) {
    throw new RangeError('Too many upload chunks')
  }
  let uploadId = ''
  let lastResponse
  const clientRequestId = createClientRequestId()

  try {
    for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
      const start = chunkIndex * chunkSize
      const end = Math.min(totalSize, start + chunkSize)
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
        // A lost first response leaves the browser without the canonical ID.
        // Retrying with the same principal-scoped request ID resumes the same
        // server session and replays chunk zero idempotently.
        if (chunkIndex !== 0 || uploadId || error?.resCode !== undefined) throw error
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
      const isFinalChunk = chunkIndex + 1 === totalChunks
      if (!Number.isInteger(nextChunkIndex) || nextChunkIndex !== chunkIndex + 1) {
        throw new Error('Upload response contained an invalid nextChunkIndex')
      }
      if (payload?.complete !== isFinalChunk) {
        throw new Error('Upload response contained an invalid completion state')
      }

      if (typeof onProgress === 'function') {
        onProgress({
          uploadId,
          uploadedBytes: end,
          totalSize,
          chunkIndex,
          totalChunks,
          percent: Math.round((end * 100) / totalSize)
        })
      }
    }
  } catch (error) {
    if (uploadId && typeof cancelUpload === 'function') {
      try {
        await cancelUpload({ uploadId })
      } catch (_) {
        // Preserve the original upload failure. Server-side TTL cleanup remains
        // the fallback when cancellation cannot be delivered.
      }
    }
    throw error
  }

  return {
    uploadId,
    filePath: extractFilePath(lastResponse),
    totalSize,
    response: lastResponse
  }
}
