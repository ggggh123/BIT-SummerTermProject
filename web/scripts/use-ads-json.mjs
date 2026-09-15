// 把数据管道产出的真实 ADS 结果（pipelines/part2/handoff/ads/json/*.json）
// 切换到前端 mock 位，用于「SparkSQL → JSON → 大屏」贯通验证与离线演示。
//
//   node scripts/use-ads-json.mjs          # 切到真实 ADS 产物
//   node scripts/use-ads-json.mjs --restore # 恢复合成 mock（npm run mock 也可）
import { copyFileSync, existsSync, mkdirSync } from 'node:fs'
import { execFileSync } from 'node:child_process'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = dirname(fileURLToPath(import.meta.url))
const WEB = join(HERE, '..')
const SRC = join(WEB, '..', 'pipelines', 'part2', 'handoff', 'ads', 'json')
const MOCK = join(WEB, 'src', 'mock')

// 管道产物与 mock 位同名，直接覆盖即可（其余接口继续用合成 mock）
const FILES = [
  'overview_kpis.json',
  'overview_stations.json',
  'overview_charger-status.json',
  'enterprise_revenue-trend.json',
  'enterprise_station-ranking.json',
  'enterprise_user-growth.json',
  'enterprise_user-rfm.json',
  'enterprise_monthly.json',
  'user_price-compare.json',
  'user_price-distance.json',
  'user_idle-ranking.json',
  'user_peak-heatmap.json',
  'station_coverage.json',
  'station_detail.json',
  'gov_coverage.json',
  'gov_service-stats.json',
  'gov_carbon.json',
  'gov_peak-load.json',
  'gov_utilization.json',
]

if (process.argv.includes('--restore')) {
  execFileSync(process.execPath, [join(HERE, 'gen-mock.mjs')], { stdio: 'inherit' })
  console.log('已恢复合成 mock')
  process.exit(0)
}

if (!existsSync(SRC)) {
  console.error(`找不到 ADS 产物目录：${SRC}\n请先在虚拟机上跑 pipelines/part2/run_all.sh`)
  process.exit(1)
}

mkdirSync(MOCK, { recursive: true })
let copied = 0
for (const f of FILES) {
  const from = join(SRC, f)
  if (!existsSync(from)) {
    console.warn(`  跳过（管道未产出）：${f}`)
    continue
  }
  copyFileSync(from, join(MOCK, f))
  copied += 1
  console.log(`  ${f} ← ADS 产物`)
}
console.log(`\n已切换 ${copied} 个接口到真实 ADS 数据（其余仍为合成 mock）`)
console.log('提示：页面右上角仍显示「演示数据」；接上 Flask 后设 VITE_USE_MOCK=false 即走接口')
