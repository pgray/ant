import { Worker, parentPort, workerData, isMainThread, threadId, getEnvironmentData, BroadcastChannel } from 'node:worker_threads';

const { mode } = workerData;

if (mode === 'echo') {
  parentPort.postMessage({ isMainThread, threadIdPositive: threadId > 0, data: workerData.n, env: getEnvironmentData('k') });
  parentPort.on('message', m => {
    if (m === 'quit') parentPort.close();
    else parentPort.postMessage({ got: m });
  });
} else if (mode === 'throw') {
  throw new TypeError('worker boom');
} else if (mode === 'reject') {
  Promise.reject(new Error('rejected in worker'));
} else if (mode === 'exit') {
  process.on('exit', code => parentPort.postMessage(`exit:${code}`));
  process.exit(workerData.code);
} else if (mode === 'exit-code') {
  process.exitCode = 3;
} else if (mode === 'spin') {
  parentPort.postMessage('spinning');
  for (;;) {}
} else if (mode === 'idle') {
  parentPort.postMessage('idle');
  setInterval(() => {}, 1000);
} else if (mode === 'wait') {
  parentPort.postMessage('waiting');
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0);
} else if (mode === 'env') {
  parentPort.postMessage({ only: process.env.ONLY ?? null, parent: process.env.WT_TEST_PARENT ?? null });
} else if (mode === 'atomics') {
  const ia = new Int32Array(workerData.sab);
  Atomics.wait(ia, 0, 0);
  parentPort.postMessage(Atomics.add(ia, 0, 1));
} else if (mode === 'port') {
  workerData.port.once('message', m => { workerData.port.postMessage(`echo:${m}`); workerData.port.close(); });
} else if (mode === 'nested') {
  if (workerData.depth === 0) parentPort.postMessage({ leaf: true });
  else {
    const w = new Worker(new URL(import.meta.url), { workerData: { mode: 'nested', depth: workerData.depth - 1 } });
    w.once('message', m => parentPort.postMessage({ depth: workerData.depth, child: m }));
  }
} else if (mode === 'broadcast') {
  const bc = new BroadcastChannel('wt-test');
  bc.onmessage = e => { bc.postMessage(`w${workerData.i}:${e.data}`); bc.close(); };
  parentPort.postMessage('ready');
} else if (mode === 'sum') {
  parentPort.postMessage(new Uint8Array(workerData.ab).reduce((a, b) => a + b, 0));
} else if (mode === 'compute') {
  let s = 0;
  for (let i = 0; i < workerData.n; i++) s += i;
  parentPort.postMessage(s);
}
