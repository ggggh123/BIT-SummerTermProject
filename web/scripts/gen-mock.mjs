// 确定性 mock 生成器：node scripts/gen-mock.mjs
// 输出 web/src/mock/*.json，全部包在与真实接口一致的信封里。
// 规模与口径对齐第二阶段设计：8 站、北京市 5 个行政区、金额整数分、+08:00 ISO 8601。
import { writeFileSync, mkdirSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const OUT = join(dirname(fileURLToPath(import.meta.url)), '..', 'src', 'mock')
mkdirSync(OUT, { recursive: true })

// ---- 确定性伪随机（LCG，固定种子）----
let seed = 20260914
const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648)
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1))
const rf = (a, b) => a + rnd() * (b - a)

const DEMO_DAY = '2026-09-14'
const pad = (n) => String(n).padStart(2, '0')
const iso = (d, h, m = 0) => `${d}T${pad(h)}:${pad(m)}:00+08:00`

const STATIONS = [
  ['朝阳国贸中心充电站', '朝阳区', 39.9085, 116.4612],
  ['海淀中关村科技园充电站', '海淀区', 39.9836, 116.3164],
  ['丰台丽泽商务区充电站', '丰台区', 39.8586, 116.3245],
  ['通州运河商务区充电站', '通州区', 39.9026, 116.6584],
  ['大兴亦庄经开区充电站', '大兴区', 39.7956, 116.5064],
  ['朝阳望京SOHO充电站', '朝阳区', 40.0009, 116.4707],
  ['海淀西二旗软件园充电站', '海淀区', 40.0509, 116.3034],
  ['丰台科技园东区充电站', '丰台区', 39.8339, 116.2946],
]

const SOURCE = { code: 0, message: 'ok' }

const stations = STATIONS.map(([name, district, latitude, longitude], i) => {
  const chargerCount = ri(24, 48) | 0
  const idleCount = ri(3, Math.max(4, Math.floor(chargerCount / 3)))
  return {
    stationId: i + 1,
    name,
    district,
    latitude,
    longitude,
    chargerCount,
    idleCount,
    utilizationRate: Number(rf(32, 88).toFixed(1)),
    revenueFen: ri(2_600_000, 9_800_000),
    priceFenPerKwh: ri(80, 160),
    orderCount: ri(9000, 26000),
    // 老师要求：无预测结果时大屏显示「暂无预测」，所以刻意留 2 个站未启用
    forecastEnabled: i !== 2 && i !== 7,
  }
})

const totalChargers = stations.reduce((s, x) => s + x.chargerCount, 0)
const totalIdle = stations.reduce((s, x) => s + x.idleCount, 0)
const fault = 6
const restarting = 4
const charging = ri(80, 140)
const reserved = ri(20, 50)
const chargerStatus = {
  idle: totalChargers - charging - reserved - fault - restarting,
  reserved,
  charging,
  fault,
  restarting,
}

const dayOffset = (base, n) => {
  const [y, m, d] = base.split('-').map(Number)
  const dt = new Date(Date.UTC(y, m - 1, d))
  dt.setUTCDate(dt.getUTCDate() + n)
  return `${dt.getUTCFullYear()}-${pad(dt.getUTCMonth() + 1)}-${pad(dt.getUTCDate())}`
}

// 90 天趋势（主页取最后 7 天，企业页可按 7/30 切窗口）
const revenueTrend = Array.from({ length: 90 }, (_, i) => {
  const date = dayOffset(DEMO_DAY, i - 89)
  const weekend = [0, 6].includes(new Date(`${date}T00:00:00Z`).getUTCDay())
  const orderCount = Math.round(ri(1_050, 1_680) * (weekend ? 1.12 : 1))
  const energyKwh = Number((orderCount * rf(28, 34)).toFixed(1))
  return {
    date,
    orderCount,
    energyKwh,
    revenueFen: Math.round(energyKwh * ri(110, 128) * (weekend ? 1.05 : 1)),
  }
})

// KPI 直接由趋势逐日汇总，保证「总览 = 明细合计」可对账（财报口径一致）
const kpis = {
  totalRevenueFen: revenueTrend.reduce((s, r) => s + r.revenueFen, 0),
  totalEnergyKwh: Number(revenueTrend.reduce((s, r) => s + r.energyKwh, 0).toFixed(1)),
  totalOrders: revenueTrend.reduce((s, r) => s + r.orderCount, 0),
  chargerCount: totalChargers,
  idleCount: chargerStatus.idle,
  onlineRate: Number((((totalChargers - fault) / totalChargers) * 100).toFixed(1)),
  windowDays: revenueTrend.length,
  generatedFrom: 'ADS（ads_revenue_overview / ads_charger_health）',
}

const hourlyShape = [0.32, 0.24, 0.2, 0.19, 0.22, 0.3, 0.46, 0.63, 0.78, 0.83, 0.8, 0.76,
  0.72, 0.74, 0.79, 0.84, 0.9, 0.97, 1.0, 0.94, 0.82, 0.66, 0.5, 0.4]

const actualLoad24h = stations.flatMap((s) =>
  hourlyShape.map((factor, h) => ({
    stationId: s.stationId,
    observedAt: iso(dayOffset(DEMO_DAY, -1), h),
    loadKw: Number((s.chargerCount * 54 * factor * rf(0.9, 1.08)).toFixed(1)),
  })),
)

const forecast24h = stations
  .filter((s) => s.forecastEnabled)
  .flatMap((s) => {
    const rows = Array.from({ length: 24 }, (_, h) => ({
      stationId: s.stationId,
      forecastAt: iso(DEMO_DAY, h),
      horizonH: h + 1,
      predictedLoadKw: Number((s.chargerCount * 54 * hourlyShape[h] * rf(0.88, 1.05)).toFixed(1)),
      predictedBusyCount: Math.min(s.chargerCount, Math.round(s.chargerCount * 0.5 * hourlyShape[h] * 1.6)),
      predictedIdleCount: 0,
      congestionLevel: 'low',
      isPeak: false,
    }))
    rows.forEach((r) => {
      r.predictedIdleCount = Math.max(0, s.chargerCount - r.predictedBusyCount)
      const occupancy = r.predictedBusyCount / s.chargerCount
      r.congestionLevel = occupancy >= 0.8 ? 'high' : occupancy >= 0.55 ? 'medium' : 'low'
    })
    const best = rows.reduce((acc, r, i) => (r.predictedLoadKw > rows[acc].predictedLoadKw ? i : acc), 0)
    rows[best].isPeak = true
    rows[Math.min(23, best + 1)].isPeak = true
    return rows
  })

const events = [
  ['order_completed', '订单完成：京A·3F2K9 在朝阳国贸中心充电站结算 68.40 元'],
  ['charging_started', '开始充电：海淀中关村科技园充电站 12 号桩'],
  ['fault', '设备告警：丰台科技园东区充电站 7 号桩通信中断'],
  ['recovered', '设备恢复：丰台科技园东区充电站 7 号桩已恢复空闲'],
  ['order_completed', '订单完成：京N·88T21 在通州运河商务区充电站结算 41.20 元'],
  ['charging_started', '开始充电：大兴亦庄经开区充电站 31 号桩'],
  ['forecast_published', '预测发布：未来 24 小时负荷预测已更新（Spark MLlib）'],
  ['restarting', '远程重启：朝阳望京SOHO充电站 5 号桩'],
  ['order_completed', '订单完成：京Q·7M4L2 在海淀西二旗软件园充电站结算 96.80 元'],
  ['order_cancelled', '订单取消：丰台丽泽商务区充电站 3 号桩预约超时释放'],
]
  .map(([eventType, message], i) => ({
    eventType,
    message,
    createdAt: iso(DEMO_DAY, 9, 58 - i * 3),
  }))

const qualityIssues = [
  ['R01', '缺失值', 640], ['R02', '重复记录', 1_284], ['R03', '异常值', 385],
  ['R04', '时间格式混杂', 1_902], ['R05', '逻辑矛盾', 372], ['R06', '金额口径错误', 640],
  ['R07', '孤儿引用', 358], ['R08', '非法字段值', 364], ['R09', '经纬度越界', 2],
  ['R10', '文本脏数据', 611],
].map(([rule, type, injected]) => ({
  rule,
  type,
  injected,
  detected: injected,
  handled: injected,
  recall: 1.0,
}))

const quality = {
  runId: 'q-20260914-01',
  tables: [
    { name: 'ods_orders', rowsBefore: 128_640, rowsAfter: 126_421 },
    { name: 'ods_telemetry', rowsBefore: 1_048_576, rowsAfter: 1_041_902 },
    { name: 'ods_users', rowsBefore: 5_000, rowsAfter: 4_988 },
    { name: 'ods_stations', rowsBefore: 8, rowsAfter: 8 },
    { name: 'ods_chargers', rowsBefore: 296, rowsAfter: 294 },
  ],
  issues: qualityIssues,
}

// ---- 企业视角：用户增长 / RFM / 月度汇总 ----
const userGrowth = Array.from({ length: 30 }, (_, i) => {
  const date = dayOffset(DEMO_DAY, i - 29)
  const weekend = [0, 6].includes(new Date(`${date}T00:00:00Z`).getUTCDay())
  return { date, newUsers: ri(18, 64), activeUsers: Math.round(ri(520, 900) * (weekend ? 1.15 : 1)) }
})

const RFM_SEGMENTS = [
  '重要价值客户', '重要保持客户', '重要发展客户', '重要挽留客户',
  '一般价值客户', '一般保持客户', '一般发展客户', '一般挽留客户',
]
const userRfm = RFM_SEGMENTS.map((segment, i) => ({
  segment,
  userCount: Math.round(ri(120, 900) * (1 - i * 0.06)),
  revenueFen: ri(1_200_000, 9_600_000),
}))

const monthly = ['2026-06', '2026-07', '2026-08', '2026-09'].map((month) => {
  const orderCount = ri(34_000, 46_000)
  const energyKwh = Number((orderCount * rf(28, 34)).toFixed(1))
  return {
    month,
    orderCount,
    energyKwh,
    revenueFen: Math.round(energyKwh * ri(110, 128)),
    revenuePerChargerFen: Math.round((energyKwh * 118) / 279),
  }
})

const wrap = (data) => ({ ...SOURCE, data, generatedAt: iso(DEMO_DAY, 10, 5) })

const files = {
  'overview_kpis.json': wrap(kpis),
  'overview_stations.json': wrap(stations),
  'overview_charger-status.json': wrap(chargerStatus),
  'overview_load-24h.json': wrap({ points: actualLoad24h }),
  'overview_events.json': wrap(events),
  'enterprise_revenue-trend.json': wrap({ days: 7, points: revenueTrend }),
  'enterprise_station-ranking.json': wrap(
    stations.map(({ stationId, name, utilizationRate, revenueFen, idleCount, chargerCount, orderCount }) => ({
      stationId, name, utilizationRate, revenueFen, idleCount, chargerCount, orderCount,
    })),
  ),
  'forecast_24h.json': wrap({
    runId: 'f-20260914-01',
    modelVersion: 'gbt-3.4.1',
    activatedAt: iso(DEMO_DAY, 10, 0),
    points: forecast24h,
  }),
  'quality_summary.json': wrap(quality),
  'enterprise_user-growth.json': wrap({ days: 30, points: userGrowth }),
  'enterprise_user-rfm.json': wrap(userRfm),
  'enterprise_monthly.json': wrap(monthly),
}

for (const [name, body] of Object.entries(files)) {
  writeFileSync(join(OUT, name), JSON.stringify(body, null, 2) + '\n', 'utf8')
}
console.log(`已生成 ${Object.keys(files).length} 个 mock 文件 → ${OUT}`)
console.log(`规模：${stations.length} 站 / ${totalChargers} 桩 / 负荷点 ${actualLoad24h.length} / 预测点 ${forecast24h.length}`)
