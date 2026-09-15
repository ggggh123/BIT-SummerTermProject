import test from 'node:test'
import assert from 'node:assert/strict'

import { buildHomeViewModel } from '../src/lib/viewModel.js'

// 后端（CSV → Spark → SQLite → JSON）常把主键返回成字符串，
// 这里锁定「前端必须自己转型」这一契约，避免站点选择器与图表过滤静默失配。
test('viewModel：stationId 为字符串时被转成数字', () => {
  const view = buildHomeViewModel({
    kpis: {},
    stations: [{ stationId: '7', name: '测试站', longitude: 116.4, latitude: 39.9, chargerCount: 10, idleCount: 3, utilizationRate: 55.5, revenueFen: 12345, forecastEnabled: 'true' }],
    chargerStatus: {},
    ranking: [],
    revenueTrend: [],
    load24h: [{ stationId: '7', observedAt: '2026-09-14T08:00:00+08:00', loadKw: 100 }],
    forecast24h: [],
    events: [],
  })
  assert.equal(view.stations[0].stationId, 7)
  assert.equal(typeof view.stations[0].stationId, 'number')
  assert.equal(view.stations[0].forecastEnabled, true)
  // 站点过滤依赖严格相等，转型后应能命中
  assert.equal(view.stations.filter((s) => s.stationId === 7).length, 1)
})

test('viewModel：ranking 与趋势字段齐全且为数字', () => {
  const view = buildHomeViewModel({
    kpis: {},
    stations: [],
    chargerStatus: {},
    ranking: [{ stationId: '2', name: 'A 站', utilizationRate: '61.2', revenueFen: '98765', idleCount: '4', chargerCount: '20', orderCount: '321' }],
    revenueTrend: [{ date: '2026-09-13', revenueFen: '123456' }],
    load24h: [],
    forecast24h: [],
    events: [],
  })
  assert.equal(view.stationRanking[0].utilizationRate, 61.2)
  assert.equal(view.stationRanking[0].revenueFen, 98765)
  assert.equal(view.revenue7d[0].revenueFen, 123456)
})
