import axios from 'axios'
import { mockFetch } from '@/mock'
import { connectionState, dataSource, generatedAt, lastError } from '@/api/state'
import { readEnvelope } from '@/lib/envelope'

// 默认走同构 mock，接口就绪后设 VITE_USE_MOCK=false 即切换（组件代码不动）
const USE_MOCK = (import.meta.env.VITE_USE_MOCK ?? 'true') !== 'false'
const http = axios.create({
  baseURL: import.meta.env.VITE_API_BASE ?? '/api',
  timeout: 15000,
})

let viewEpoch = 0
const failures = new Map()
export function beginApiView() {
  viewEpoch += 1
  failures.clear()
  lastError.value = ''
  generatedAt.value = ''
  dataSource.value = USE_MOCK ? 'mock' : 'api'
  connectionState.value = USE_MOCK ? 'mock' : 'stale'
}

/** 拉取单个接口，返回 { data, generatedAt }；失败时抛错（调用方决定是否保留旧数据） */
export async function fetchEnvelope(path, params = {}) {
  const epoch = viewEpoch
  try {
    let result
    if (USE_MOCK) {
      const row = await mockFetch(path, params)
      if (!row) throw new Error(`mock 中没有定义接口 ${path}`)
      result = row
    } else {
      const res = await http.get(`/${path}`, { params })
      result = readEnvelope(res.data, path)
    }
    if (epoch === viewEpoch) {
      failures.delete(path)
      dataSource.value = USE_MOCK ? 'mock' : 'api'
      generatedAt.value = result.generatedAt || generatedAt.value
      connectionState.value = failures.size ? 'error' : USE_MOCK ? 'mock' : 'live'
      lastError.value = [...failures.values()].join('；')
    }
    return result
  } catch (err) {
    if (epoch === viewEpoch) {
      failures.set(path, err?.message ?? String(err))
      lastError.value = [...failures.values()].join('；')
      connectionState.value = 'error'
    }
    throw err
  }
}

/** 并发拉取一组接口；任一失败即整体失败（页面保留上一次成功数据） */
export async function fetchGroup(spec) {
  const names = Object.keys(spec)
  const results = await Promise.all(names.map((n) => fetchEnvelope(spec[n][0], spec[n][1] ?? {})))
  return Object.fromEntries(names.map((n, i) => [n, results[i].data]))
}
