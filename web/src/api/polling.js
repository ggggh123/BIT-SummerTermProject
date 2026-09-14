// 轮询调度器（《01》§5.2：KPI/事件流 5s，趋势类 60s；失败保留上次成功数据）
// 失败时不抛错、不清空页面数据，只让 client.js 标记 connectionState，由顶部徽章提示过期。
export function startPolling(fn, intervalMs, { immediate = true } = {}) {
  let stopped = false
  const tick = () => {
    if (stopped) return
    try {
      const result = fn()
      if (result && typeof result.then === 'function') result.catch(() => {})
    } catch {
      // 单次失败不影响后续调度，页面数据保持上一次成功值
    }
  }
  if (immediate) tick()
  const timer = setInterval(tick, intervalMs)
  return () => {
    stopped = true
    clearInterval(timer)
  }
}
