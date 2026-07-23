import axios from 'axios' // 引入 axios
import { message } from '@/utils/message'
import actions from '@/micro/state.js'
import { currentLocale, t, translateApiMessage } from '@/i18n'

const longTimeoutApi = [
  '/gtw/cwai/System/Upgrade',
  '/gtw/cwai/System/QueryDeviceStatus',
  '/gtw/cwai/File/ImportFile',
  '/gtw/cwai/Camera/AddVideo',
  '/gtw/cwai/atomic/model/uploadTemp',
  '/gtw/cwai/atomic/model/upload',
  '/gtw/cwai/atomic/model/add',
  '/gtw/cwai/atomic/model/importModel',
  '/gtw/cwai/algorithm/Upload',
  '/gtw/cwai/algorithm/version/add'
]

const silentApi = [
  '/gtw/cwai/System/Upgrade',
  '/gtw/cwai/System/QueryHardwareResource',
  '/gtw/cwai/network/IpAccessibleCheck',
  '/gtw/cwai/System/QueryDeviceStatus',
  '/gtw/cwai/LiveStream/RequestLiveStream',
  '/gtw/cwai/LiveStream/StreamKeepAlive',
  '/gtw/cwai/LiveStream/StreamStop',
  '/gtw/cwai/File/QueryImportStatus',
  '/gtw/cwai/File/ImportFile',
  '/gtw/cwai/atomic/model/uploadTemp',
  '/gtw/cwai/atomic/model/cancelUpload'
]

const service = axios.create({
  baseURL: import.meta.env.VITE_APP_BASE_URL || '/',
  timeout: 1000 * 20 // request timeout
})

/**
 * request拦截器，附加请求参数
 */
service.interceptors.request.use(config => {
  config.headers.mtk = localStorage.getItem('mtk')   // gam token
  config.headers.token = localStorage.getItem('mtk')
  config.headers.fileMode = 1 //告知后端返回相对路径
  config.headers.lang = currentLocale.value.replace('-', '_')
  config.headers['Accept-Language'] = currentLocale.value
  if (longTimeoutApi.includes(config.url)) {
    config.timeout = 1000 * 60 * 10  // 
  }
  return config
}, error => {
  return Promise.reject(error)
})

/**
 * response拦截器，处理响应
 */

const clearLoginInfo = () => {
  actions.setGlobalState({ loading: false, loginState: true })
  localStorage.removeItem('mtk')
  localStorage.removeItem('token')
  // 使用 hash 路由跳转
  window.location.hash = '#/boxLogin'
}

export const request = params => {
  return new Promise((resolve, reject) => {
    const isSilent = silentApi.includes(params.url)
    if (!isSilent) actions.setGlobalState({ loading: true }) // 开启过渡效果
    service(params)
      .then(res => {
        const data = res?.data || {}
        const resCode = data?.resCode
        const firstMsg = Array.isArray(data?.resMsg) ? data.resMsg[0] : {}
        const msgCode = firstMsg?.msgCode
        const messageKey = firstMsg?.messageKey || firstMsg?.msgKey || data?.messageKey || data?.msgKey
        const msgText = translateApiMessage(messageKey, firstMsg?.msgText || data?.message || data?.msg)
        if (resCode === 1) {
          resolve(data)
        } else {
          if (msgCode === '10005') {
            message.error(t('api.loginExpired'))
            clearLoginInfo()
          } else if (!params.silentError) {
            message.error(msgText)
          }
          reject(data)
        }
      })
      .catch(err => {
        const status = err?.response?.status ?? err?.status
        const msgCode = err?.response?.data?.resMsg?.[0]?.msgCode
        if (status === 401 || msgCode === '10005') {
          message.error(t('api.loginExpired'))
          clearLoginInfo()
          return reject(err)
        }
        if (status === 502 || status === 500 || status === 404 || status === 400) {
          if (params.url === '/gtw/cwai/System/QueryDeviceStatus') return reject(err)
          if (!params.silentError) message.error(t('api.networkError'))
          return reject(err)
        }
        return reject(err)
      })
      .finally(() => {
        if (!isSilent) actions.setGlobalState({ loading: false })
      })
  })
}
