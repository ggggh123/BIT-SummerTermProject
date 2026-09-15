// 主页专用薄适配层：不改动第一阶段复用的 models.js，只按《01》§3.1 把窗口调到近 30 日
// 这里用相对路径而不是 '@' 别名：本文件要能被 node --test 直接 import（见 tests/viewModel.test.mjs），
// 而 Node 不认识 Vite 的别名。
import { buildRevenueOption } from '../models.js'

export function buildRevenueTrendOption30d(view) {
  const option = buildRevenueOption(view)
  option.series[0].name = '近 30 日营收'
  option.meta = { days: view.revenue7d.length }
  return option
}
