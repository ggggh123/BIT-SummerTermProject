import axios from 'axios'
import { mockFetch } from '@/mock'
import { connectionState, dataSource, generatedAt, lastError } from '@/api/state'

// 默认走同构 mock，接口就绪后设 VITE_USE_MOCK=false 即切换（组件代码不动）
const USE_MOCK = (import.meta.env.VITE_USE_MOCK ?? 'true') !== 'false'
const http = axios.create({
  baseURL: import.meta.env.VITE_API_BASE ?? '/api',
  timeout: 15000,
})

function assertEnvelope(body, path) {
  if (!body || typeof body !== 'object') throw new Error(`${path}: 响应不是 JSON 对象`)
  if (!Number.isInteger(body.code)) throw new Error(`${path}: 缺少整数 code 字段`)
  if (body.code !== 0) throw new Error(`${path}: 接口返回错误 code=${body.code} ${body.message ?? ''}`)
  if (body.data === undefined || body.data === null) throw new Error(`${path}: 响应缺少 data 字段`)
}

/** 拉取单个接口，返回 { data, generatedAt }；失败时抛错（调用方决定是否保留旧数据） */
export async function fetchEnvelope(path, params = {}) {
  try {
    let result
    if (USE_MOCK) {
      const row = await mockFetch(path, params)
      if (!row) throw new Error(`mock 中没有定义接口 ${path}`)
      result = row
    } else {
      const res = await http.get(`/${path}`, { params })
      assertEnvelope(res.data, path)
      result = { data: res.data.data, generatedAt: res.data.generatedAt ?? '' }
    }
    dataSource.value = USE_MOCK ? 'mock' : 'api'
    generatedAt.value = result.generatedAt || generatedAt.value
    connectionState.value = USE_MOCK ? 'mock' : 'live'
    lastError.value = ''
    return result
  } catch (err) {
    lastError.value = err?.message ?? String(err)
    connectionState.value = 'error'
    throw err
  }
}

/** 并发拉取一组接口；任一失败即整体失败（页面保留上一次成功数据） */
export async function fetchGroup(spec) {
  const names = Object.keys(spec)
  const results = await Promise.all(names.map((n) => fetchEnvelope(spec[n][0], spec[n][1] ?? {})))
  return Object.fromEntries(names.map((n, i) => [n, results[i].data]))
}
