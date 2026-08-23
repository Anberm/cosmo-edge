const SIGNAL_EXIT_CODES = {
  SIGINT: 130,
  SIGTERM: 143,
};

export class ShutdownSignalError extends Error {
  constructor(signalName) {
    super(`received ${signalName}; shutting down`);
    this.name = 'ShutdownSignalError';
    this.signalName = signalName;
    this.exitCode = SIGNAL_EXIT_CODES[signalName] ?? 1;
  }
}

export function installShutdownSignalHandlers(controller, {
  processRef = process,
  onSignal = null,
} = {}) {
  if (!controller?.signal || typeof controller.abort !== 'function') {
    throw new TypeError('an AbortController is required');
  }

  let handled = false;
  const handle = (signalName) => {
    if (handled) return;
    handled = true;
    const error = new ShutdownSignalError(signalName);
    try {
      onSignal?.(error);
    } finally {
      if (!controller.signal.aborted) controller.abort(error);
    }
  };
  const onSigint = () => handle('SIGINT');
  const onSigterm = () => handle('SIGTERM');

  processRef.on('SIGINT', onSigint);
  processRef.on('SIGTERM', onSigterm);

  return () => {
    processRef.off('SIGINT', onSigint);
    processRef.off('SIGTERM', onSigterm);
  };
}

export function throwIfAborted(signal) {
  if (!signal?.aborted) return;
  if (signal.reason instanceof Error) throw signal.reason;
  throw new Error('operation aborted');
}

export function sleepWithSignal(ms, signal) {
  throwIfAborted(signal);
  if (ms <= 0) return Promise.resolve();

  return new Promise((resolve, reject) => {
    const onAbort = () => {
      clearTimeout(timer);
      signal?.removeEventListener('abort', onAbort);
      try {
        throwIfAborted(signal);
      } catch (error) {
        reject(error);
      }
    };
    const timer = setTimeout(() => {
      signal?.removeEventListener('abort', onAbort);
      resolve();
    }, ms);
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}
