const DEFAULT_ICE_SERVERS = [
  { urls: 'stun:stun.l.google.com:19302' }
]

const appendCandidatesToSdp = (sdp, candidates) => {
  if (!candidates.length) {
    return sdp
  }

  const candidateLines = candidates
    .filter((candidate) => candidate?.candidate && !sdp.includes(candidate.candidate))
    .map((candidate) => `a=${candidate.candidate}`)

  if (!candidateLines.length) {
    return sdp
  }

  const endOfCandidates = sdp.includes('a=end-of-candidates')
    ? ''
    : '\r\na=end-of-candidates'

  return `${sdp.trim()}\r\n${candidateLines.join('\r\n')}${endOfCandidates}\r\n`
}

const getCandidateLines = (sdp = '') => {
  return sdp.split(/\r?\n/).filter((line) => line.startsWith('a=candidate:'))
}

const waitForFirstCandidate = (peer, candidates, timeoutMs) => {
  if (candidates.length || peer.iceGatheringState === 'complete') {
    return Promise.resolve()
  }

  return new Promise((resolve) => {
    let timeout = null
    const cleanup = () => {
      if (timeout) {
        clearTimeout(timeout)
      }
      peer.removeEventListener('icecandidate', onCandidate)
      peer.removeEventListener('icegatheringstatechange', onStateChange)
    }
    const finish = () => {
      cleanup()
      resolve()
    }
    const onCandidate = (event) => {
      if (event.candidate || peer.iceGatheringState === 'complete') {
        finish()
      }
    }
    const onStateChange = () => {
      if (peer.iceGatheringState === 'complete') {
        finish()
      }
    }

    peer.addEventListener('icecandidate', onCandidate)
    peer.addEventListener('icegatheringstatechange', onStateChange)
    timeout = setTimeout(finish, timeoutMs)
  })
}

const logPeerState = (peer, label) => {
  console.log(
    `[WHEP] ${label}:`,
    `signaling=${peer.signalingState}`,
    `iceGathering=${peer.iceGatheringState}`,
    `iceConnection=${peer.iceConnectionState}`,
    `connection=${peer.connectionState}`
  )
}

export const createWhepPlayer = ({
  video,
  url,
  onConnected,
  onError,
  timeoutMs = 5000,
  mediaTimeoutMs = 15000,
  firstCandidateTimeoutMs = 3000,
  iceServers = DEFAULT_ICE_SERVERS
}) => {
  let peer = null
  let abortController = null
  let resourceUrl = ''
  let stopped = false
  let timeout = null
  let mediaTimeout = null
  let failed = false
  let mediaReady = false
  let connectedNotified = false
  let ownedStream = null
  let videoFrameCallback = null
  let mediaReadyListener = null
  const diagnosticsTimers = []

  const clearMediaTimeout = () => {
    if (mediaTimeout) {
      clearTimeout(mediaTimeout)
      mediaTimeout = null
    }
  }

  const clearMediaReadyWait = () => {
    if (videoFrameCallback !== null && video?.cancelVideoFrameCallback) {
      video.cancelVideoFrameCallback(videoFrameCallback)
    }
    videoFrameCallback = null
    if (mediaReadyListener && video) {
      video.removeEventListener('playing', mediaReadyListener)
      video.removeEventListener('loadeddata', mediaReadyListener)
    }
    mediaReadyListener = null
  }

  const cleanup = async ({ markStopped = true } = {}) => {
    if (markStopped) {
      stopped = true
    }
    if (timeout) {
      clearTimeout(timeout)
      timeout = null
    }
    clearMediaTimeout()
    clearMediaReadyWait()
    diagnosticsTimers.forEach((timer) => clearTimeout(timer))
    diagnosticsTimers.length = 0
    if (abortController) {
      abortController.abort()
      abortController = null
    }
    if (resourceUrl) {
      resourceUrl = resourceUrl.replace('/rtc/v1/whip/', '/rtc/v1/whep/')
      fetch(resourceUrl, { method: 'DELETE' }).catch(() => {})
      resourceUrl = ''
    }
    const ownedPeer = peer
    peer = null
    if (ownedPeer) {
      ownedPeer.getReceivers().forEach((receiver) => {
        receiver.track && receiver.track.stop()
      })
      ownedPeer.close()
    }
    if (video && ownedStream && video.srcObject === ownedStream) {
      video.srcObject = null
    }
    ownedStream = null
  }

  const stop = () => cleanup({ markStopped: true })

  const fail = (error) => {
    if (!stopped && !failed && onError) {
      failed = true
      onError(error)
    }
  }

  const markMediaReady = () => {
    if (stopped) return
    mediaReady = true
    clearMediaTimeout()
    clearMediaReadyWait()
    if (!connectedNotified) {
      connectedNotified = true
      onConnected && onConnected()
    }
  }

  const waitForFirstFrame = (stream) => {
    if (!video) {
      fail(new Error('WebRTC video element is unavailable'))
      return
    }

    clearMediaReadyWait()
    const ready = () => {
      if (stopped || ownedStream !== stream || video.srcObject !== stream) return
      markMediaReady()
    }
    if (typeof video.requestVideoFrameCallback === 'function') {
      videoFrameCallback = video.requestVideoFrameCallback(() => {
        videoFrameCallback = null
        ready()
      })
    } else {
      mediaReadyListener = ready
      video.addEventListener('playing', ready)
      video.addEventListener('loadeddata', ready)
    }
  }

  const start = async () => {
    let currentPeer = null
    const isCurrent = () => !stopped && peer === currentPeer
    try {
      const controller = new AbortController()
      abortController = controller
      console.log('[WHEP] secure context:', window.isSecureContext, 'url:', url)
      currentPeer = new RTCPeerConnection({
        iceServers,
        iceCandidatePoolSize: 4
      })
      peer = currentPeer
      const candidates = []
      const onCandidate = (event) => {
        if (!isCurrent()) return
        if (event.candidate) {
          candidates.push(event.candidate)
          console.log('[WHEP] ICE candidate:', event.candidate.candidate)
        } else {
          console.log('[WHEP] ICE candidate gathering complete')
        }
      }
      currentPeer.addEventListener('icecandidate', onCandidate)
      currentPeer.addEventListener('icecandidateerror', (event) => {
        if (!isCurrent()) return
        console.warn('[WHEP] ICE candidate error:', {
          address: event.address,
          port: event.port,
          url: event.url,
          errorCode: event.errorCode,
          errorText: event.errorText
        })
      })
      currentPeer.addEventListener('icegatheringstatechange', () => {
        if (isCurrent()) logPeerState(currentPeer, 'state changed')
      })
      currentPeer.addEventListener('iceconnectionstatechange', () => {
        if (isCurrent()) logPeerState(currentPeer, 'state changed')
      })
      currentPeer.addEventListener('connectionstatechange', () => {
        if (isCurrent()) logPeerState(currentPeer, 'state changed')
      })
      currentPeer.addEventListener('signalingstatechange', () => {
        if (isCurrent()) logPeerState(currentPeer, 'state changed')
      })
      currentPeer.addTransceiver('video', { direction: 'recvonly' })
      currentPeer.ontrack = (event) => {
        if (!isCurrent()) return
        const stream = event.streams?.[0]
        if (!stream || !video) {
          fail(new Error('WebRTC track did not provide a media stream'))
          return
        }
        ownedStream = stream
        if (video.srcObject !== stream) video.srcObject = stream
        waitForFirstFrame(stream)
        Promise.resolve(video.play()).catch(fail)
      }
      currentPeer.onconnectionstatechange = () => {
        if (!isCurrent()) return
        if (['failed', 'disconnected', 'closed'].includes(currentPeer.connectionState)) {
          fail(new Error(`WebRTC ${currentPeer.connectionState}`))
        }
      }
      currentPeer.oniceconnectionstatechange = () => {
        if (!isCurrent()) return
        if (['failed', 'disconnected', 'closed'].includes(currentPeer.iceConnectionState)) {
          fail(new Error(`WebRTC ICE ${currentPeer.iceConnectionState}`))
        }
      }

      const offer = await currentPeer.createOffer()
      if (!isCurrent()) return
      await currentPeer.setLocalDescription(offer)
      if (!isCurrent()) return
      await waitForFirstCandidate(currentPeer, candidates, firstCandidateTimeoutMs)
      if (!isCurrent()) return
      console.log('[WHEP] ICE gathering state:', currentPeer.iceGatheringState, 'candidate count:', candidates.length)
      const offerSdp = appendCandidatesToSdp(currentPeer.localDescription.sdp, candidates)
      const localCandidateLines = getCandidateLines(offerSdp)
      console.log('[WHEP] local SDP candidate count:', localCandidateLines.length, localCandidateLines)
      if (!localCandidateLines.length) {
        console.warn('[WHEP] no local ICE candidates in initial offer. Continue with WHEP answer and watch live ICE candidates.')
        if (!window.isSecureContext) {
          throw new Error('WebRTC requires HTTPS or a trusted browser origin to gather local ICE candidates')
        }
      }

      timeout = setTimeout(() => {
        if (!isCurrent()) return
        controller.abort()
        fail(new Error('WebRTC connect timeout'))
      }, timeoutMs)

      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/sdp'
        },
        body: offerSdp,
        signal: controller.signal
      })

      if (!response.ok) {
        throw new Error(`WHEP request failed: ${response.status}`)
      }

      if (timeout) {
        clearTimeout(timeout)
        timeout = null
      }

      resourceUrl = response.headers.get('Location') || ''
      if (resourceUrl && resourceUrl.startsWith('/')) {
        resourceUrl = new URL(resourceUrl, url).toString()
      }
      if (!isCurrent()) {
        await cleanup({ markStopped: false })
        return
      }

      const answer = await response.text()
      if (!isCurrent()) {
        await cleanup({ markStopped: false })
        return
      }
      const remoteCandidateLines = getCandidateLines(answer)
      console.log('[WHEP] remote SDP candidate count:', remoteCandidateLines.length, remoteCandidateLines)
      await currentPeer.setRemoteDescription({ type: 'answer', sdp: answer })
      if (!isCurrent()) return
      logPeerState(currentPeer, 'after remote answer')
      diagnosticsTimers.push(setTimeout(() => {
        if (isCurrent()) {
          console.log('[WHEP] candidates after 2s:', candidates.length, candidates.map((candidate) => candidate.candidate))
          logPeerState(currentPeer, 'after 2s')
        }
      }, 2000))
      diagnosticsTimers.push(setTimeout(() => {
        if (isCurrent()) {
          console.log('[WHEP] candidates after 10s:', candidates.length, candidates.map((candidate) => candidate.candidate))
          logPeerState(currentPeer, 'after 10s')
          if (!candidates.length && currentPeer.iceConnectionState === 'new') {
            fail(new Error('Browser did not gather local ICE candidates; check Chrome WebRTC UDP/IP policy'))
          }
        }
      }, 10000))

      if (!mediaReady) {
        mediaTimeout = setTimeout(() => {
          if (isCurrent()) fail(new Error('WebRTC media timeout'))
        }, mediaTimeoutMs)
      }
    } catch (error) {
      await cleanup({ markStopped: false })
      fail(error)
    }
  }

  return { start, stop }
}
