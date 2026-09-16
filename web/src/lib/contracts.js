// 金额、百分比与过期判定复用第一阶段；本页格式化只改显示，不改数据。
export { formatFen, formatPercent, isForecastStale } from '../../../dashboard/assets/contracts.js'

export function formatKwh(value) {
  return typeof value === 'number' && Number.isFinite(value)
    ? `${value.toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })} kWh`
    : '—'
}

export function formatForecastSource(batch) {
  const kind = batch?.isBaseline === true ? '基线预测'
    : batch?.isBaseline === false ? 'ML 模型预测' : '预测（来源未标明）'
  return batch?.modelVersion ? `${kind} / ${batch.modelVersion}` : kind
}
