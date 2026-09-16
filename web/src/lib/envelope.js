// 4041 是“未发布预测”，不是整页故障；仅允许预测端点按契约降级。
export function readEnvelope(body, path) {
  if (!body || typeof body !== 'object' || !Number.isInteger(body.code)) throw new Error(`${path}: 响应缺少有效 code`)
  if (body.code === 4041 && ['forecast/24h', 'forecast/recommend', 'forecast/metrics'].includes(path)) {
    return { data: path === 'forecast/recommend' ? [] : { points: [], horizons: [], available: false, isBaseline: null }, generatedAt: body.generatedAt ?? '' }
  }
  if (body.code !== 0) throw new Error(`${path}: 接口返回错误 code=${body.code} ${body.message ?? ''}`)
  if (body.data === undefined || body.data === null) throw new Error(`${path}: 响应缺少 data`)
  return { data: body.data, generatedAt: body.generatedAt ?? '' }
}
