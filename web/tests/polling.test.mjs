import test from 'node:test'
import assert from 'node:assert/strict'

import { startPolling } from '../src/api/polling.js'

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

test('startPolling：立即执行一次，之后按间隔重复', async () => {
  let calls = 0
  const stop = startPolling(() => { calls += 1 }, 20)
  try {
    assert.equal(calls, 1, '应立即执行一次')
    await sleep(120)
    assert.ok(calls >= 3, `应按间隔重复执行（实际 ${calls} 次）`)
  } finally {
    stop()
  }
})

test('startPolling：stop 之后不再执行', async () => {
  let calls = 0
  const stop = startPolling(() => { calls += 1 }, 20)
  try {
    await sleep(60)
    stop()
    const snapshot = calls
    await sleep(60)
    assert.equal(calls, snapshot, 'stop 后不应再有调用')
  } finally {
    stop()
  }
})

test('startPolling：同步抛错或 Promise reject 都不中断调度', async () => {
  let calls = 0
  const stop = startPolling(() => {
    calls += 1
    if (calls % 2 === 0) throw new Error('boom')
    return Promise.reject(new Error('async boom'))
  }, 20)
  try {
    await sleep(120)
    assert.ok(calls >= 3, `异常不应中断轮询（实际 ${calls} 次）`)
  } finally {
    stop()
  }
})

test('startPolling：immediate=false 时不立即执行', async () => {
  let calls = 0
  const stop = startPolling(() => { calls += 1 }, 20, { immediate: false })
  try {
    assert.equal(calls, 0, '应等到第一个间隔才执行')
    await sleep(120)
    assert.ok(calls >= 2, `应按间隔开始执行（实际 ${calls} 次）`)
  } finally {
    stop()
  }
})
