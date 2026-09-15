// 主页专用薄适配层：不改动第一阶段复用的 models.js，只按《01》§3.1 把窗口调到近 30 日
import { buildRevenueOption } from '@/lib/models'

export function buildRevenueTrendOption30d(view) {
  const option = buildRevenueOption(view)
  option.series[0].name = '近 30 日营收'
  option.meta = { days: view.revenue7d.length }
  return option
}
