// 只接收最后一次选择对应的响应，防止 A -> B 时 A 的慢响应覆盖 B。
export function createLatestRequest() {
  let revision = 0
  return {
    begin() { const current = ++revision; return () => current === revision },
    invalidate() { revision += 1 },
  }
}
