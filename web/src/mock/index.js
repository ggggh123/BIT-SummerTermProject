// 与真实接口同构的 mock（同一信封 {code,message,data,generatedAt}）
// 由 scripts/gen-mock.mjs 确定性生成：同种子重复生成内容一致，可直接用于演示。
import overviewKpis from './overview_kpis.json'
import overviewStations from './overview_stations.json'
import overviewChargerStatus from './overview_charger-status.json'
import overviewLoad24h from './overview_load-24h.json'
import overviewEvents from './overview_events.json'
import enterpriseRevenueTrend from './enterprise_revenue-trend.json'
import enterpriseStationRanking from './enterprise_station-ranking.json'
import enterpriseUserGrowth from './enterprise_user-growth.json'
import enterpriseUserRfm from './enterprise_user-rfm.json'
import enterpriseMonthly from './enterprise_monthly.json'
import forecast24h from './forecast_24h.json'
import qualitySummary from './quality_summary.json'

const TABLE = {
  'overview/kpis': overviewKpis,
  'overview/stations': overviewStations,
  'overview/charger-status': overviewChargerStatus,
  'overview/load-24h': overviewLoad24h,
  'overview/events': overviewEvents,
  'enterprise/revenue-trend': enterpriseRevenueTrend,
  'enterprise/station-ranking': enterpriseStationRanking,
  'enterprise/user-growth': enterpriseUserGrowth,
  'enterprise/user-rfm': enterpriseUserRfm,
  'enterprise/monthly': enterpriseMonthly,
  'forecast/24h': forecast24h,
  'quality/summary': qualitySummary,
}

export const MOCK_ENDPOINTS = Object.keys(TABLE)

export async function mockFetch(path) {
  const row = TABLE[path]
  if (!row) return null
  // 深拷贝：页面改动不会污染 mock 常量
  return JSON.parse(JSON.stringify(row))
}
