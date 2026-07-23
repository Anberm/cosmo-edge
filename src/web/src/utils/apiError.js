const firstMessage = payload => {
  const messages = payload?.resMsg
  return Array.isArray(messages) && messages.length ? messages[0] : {}
}

const responsePayload = error =>
  error?.response?.data || error?.data || (error?.resCode !== undefined ? error : {})

const requestUsesMultipart = error => {
  const data = error?.config?.data
  if (typeof globalThis.FormData === 'function' && data instanceof globalThis.FormData) {
    return true
  }

  const headers = error?.config?.headers
  let contentType = ''
  if (typeof headers?.get === 'function') {
    contentType = headers.get('Content-Type') || headers.get('content-type') || ''
  } else if (headers && typeof headers === 'object') {
    const entry = Object.entries(headers).find(([name]) => name.toLowerCase() === 'content-type')
    contentType = entry?.[1] || ''
  }
  return /^multipart\/form-data(?:;|$)/i.test(String(contentType))
}

const visibleActions = new Set([
  'CHANGE_DEPLOYMENT_POLICY',
  'USE_LARGER_CHUNKS_OR_CHANGE_POLICY',
  'USE_LARGER_CHUNKS',
  'RETRY_LATER',
  'FREE_DISK_SPACE',
  'CHECK_UPLOAD_PARAMETERS',
  'USE_CHUNKED_UPLOAD',
  'REDUCE_REQUEST_BODY',
  'RETRY',
  'RESIZE_OR_RECOMPRESS_IMAGE',
  'RESIZE_IMAGE'
])

export const formatBytes = value => {
  const bytes = Number(value)
  if (!Number.isFinite(bytes) || bytes < 0) return ''
  // Product UI uses KB/MB/GB labels for binary multiples; keep this aligned
  // with docs/i18n/GLOSSARY.md rather than exposing IEC labels in one toast.
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let amount = bytes
  let unit = 0
  while (amount >= 1024 && unit < units.length - 1) {
    amount /= 1024
    unit += 1
  }
  const digits = unit === 0 || amount >= 100 ? 0 : amount >= 10 ? 1 : 2
  return `${amount.toFixed(digits)} ${units[unit]}`
}

export const normalizeApiError = error => {
  const payload = responsePayload(error)
  const item = firstMessage(payload)
  const status = error?.response?.status ?? error?.status
  const isUnstructuredPayloadTooLarge =
    Number(status) === 413 && !item?.msgCode && !payload?.msgCode
  const fallbackAction = requestUsesMultipart(error)
    ? 'USE_CHUNKED_UPLOAD'
    : 'REDUCE_REQUEST_BODY'
  return {
    status,
    payload,
    code:
      item?.msgCode ||
      payload?.msgCode ||
      (isUnstructuredPayloadTooLarge ? 'HTTP_BODY_TOO_LARGE' : ''),
    messageKey:
      item?.messageKey ||
      item?.msgKey ||
      payload?.messageKey ||
      payload?.msgKey ||
      (isUnstructuredPayloadTooLarge ? 'api.error.httpBodyTooLarge' : ''),
    message:
      item?.msgText ||
      payload?.message ||
      payload?.msg ||
      (isUnstructuredPayloadTooLarge
        ? 'HTTP request body exceeds the request boundary'
        : ''),
    details: item?.details || {},
    retryable: item?.retryable === true,
    recommendedAction:
      item?.recommendedAction || (isUnstructuredPayloadTooLarge ? fallbackAction : ''),
    retryAfterSeconds: Number(item?.retryAfterSeconds || 0)
  }
}

export const formatActionableApiError = (
  error,
  translateMessage,
  translate = key => key
) => {
  const normalized = normalizeApiError(error)
  const base = translateMessage(normalized.messageKey, normalized.message) || normalized.message
  const { details } = normalized
  const facts = []

  if (details.requiredBytes !== undefined) {
    facts.push(`${translate('api.details.required')} ${formatBytes(details.requiredBytes)}`)
  }
  if (details.availableBytes !== undefined) {
    facts.push(`${translate('api.details.available')} ${formatBytes(details.availableBytes)}`)
  }
  if (details.limitBytes !== undefined) {
    facts.push(`${translate('api.details.limit')} ${formatBytes(details.limitBytes)}`)
  }
  if (details.actualBytes !== undefined) {
    facts.push(`${translate('api.details.actual')} ${formatBytes(details.actualBytes)}`)
  }
  if (details.limitCount !== undefined) {
    facts.push(`${translate('api.details.limit')} ${details.limitCount}`)
  }
  if (details.actualCount !== undefined) {
    facts.push(`${translate('api.details.actual')} ${details.actualCount}`)
  }

  const action = visibleActions.has(normalized.recommendedAction)
    ? translate(`api.actions.${normalized.recommendedAction}`)
    : ''
  const retry = normalized.retryAfterSeconds > 0
    ? translate('api.retryAfter', { seconds: normalized.retryAfterSeconds })
    : ''
  const guidance = [action, retry].filter(Boolean)
  const factualMessage = facts.length ? `${base} (${facts.join(', ')})` : base

  return {
    ...normalized,
    displayMessage: guidance.length
      ? `${factualMessage}; ${translate('api.actionLabel')}: ${guidance.join('; ')}`
      : factualMessage
  }
}
