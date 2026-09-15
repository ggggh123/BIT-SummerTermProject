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
import userPriceCompare from './user_price-compare.json'
import userPriceDistance from './user_price-distance.json'
import userIdleRanking from './user_idle-ranking.json'
import userPeakHeatmap from './user_peak-heatmap.json'
import stationCoverage from './station_coverage.json'
import stationDetail from './station_detail.json'
import govCoverage from './gov_coverage.json'
import govServiceStats from './gov_service-stats.json'
import govCarbon from './gov_carbon.json'
import govPeakLoad from './gov_peak-load.json'
import govUtilization from './gov_utilization.json'

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
  'user/price-compare': userPriceCompare,
  'user/price-distance': userPriceDistance,
  'user/idle-ranking': userIdleRanking,
  'user/peak-heatmap': userPeakHeatmap,
  'station/coverage': stationCoverage,
  'gov/coverage': govCoverage,
  'gov/service-stats': govServiceStats,
  'gov/carbon': govCarbon,
  'gov/peak-load': govPeakLoad,
  'gov/utilization': govUtilization,
}

export const MOCK_ENDPOINTS = Object.keys(TABLE)

export async function mockFetch(path) {
  // station/{id}/utilization|mix|health 由 station_detail.json 按站点切片
  const m = path.match(/^station\/(\d+)\/(utilization|mix|health)$/)
  if (m) {
    const row = stationDetail.data?.[m[1]]
    if (!row) return null
    return JSON.parse(JSON.stringify({ ...SOURCE_WRAP, data: { stationId: row.stationId, name: row.name, ...row[m[2]] } }))
  }
  const row = TABLE[path]
  if (!row) return null
  // 深拷贝：页面改动不会污染 mock 常量
  return JSON.parse(JSON.stringify(row))
}

// 与 station_detail.json 相同的信封（生成时间一致）
const SOURCE_WRAP = { code: 0, message: 'ok', generatedAt: stationDetail.generatedAt }
