// 复用边界的护栏：确认 REST 形状的接口数据能直接喂给第一阶段 dashboard/assets/models.js
// 运行：node --test web/tests/viewModel.test.mjs
import test from 'node:test'
import assert from 'node:assert/strict'

import { buildHomeViewModel } from '../src/lib/viewModel.js'
import {
  buildLoadForecastOption,
  buildRankingOption,
  buildRevenueOption,
  buildStationOption,
  buildStatusOption,
} from '../src/lib/models.js'

import kpis from '../src/mock/overview_kpis.json' with { type: 'json' }
import stations from '../src/mock/overview_stations.json' with { type: 'json' }
import chargerStatus from '../src/mock/overview_charger-status.json' with { type: 'json' }
import load24h from '../src/mock/overview_load-24h.json' with { type: 'json' }
import revenueTrend from '../src/mock/enterprise_revenue-trend.json' with { type: 'json' }
import ranking from '../src/mock/enterprise_station-ranking.json' with { type: 'json' }
import forecast24h from '../src/mock/forecast_24h.json' with { type: 'json' }

const view = buildHomeViewModel({
  kpis: kpis.data,
  stations: stations.data,
  chargerStatus: chargerStatus.data,
  ranking: ranking.data,
  revenueTrend: revenueTrend.data.points,
  load24h: load24h.data.points,
  forecast24h: forecast24h.data.points,
  events: [],
  quality: null,
})

/** 递归检查 option 里没有 undefined / NaN —— 与第一阶段 models 测试同一口径 */
function assertCleanNumbers(node, path = 'option') {
  if (typeof node === 'number') {
    assert.ok(Number.isFinite(node), `${path} 出现非有限数字: ${node}`)
    return
  }
  if (Array.isArray(node)) {
    node.forEach((v, i) => assertCleanNumbers(v, `${path}[${i}]`))
    return
  }
  if (node && typeof node === 'object') {
    for (const [k, v] of Object.entries(node)) {
      assert.notEqual(v, undefined, `${path}.${k} 为 undefined`)
      assertCleanNumbers(v, `${path}.${k}`)
    }
  }
}

test('mock 接口数据被映射成 models.js 期望的字段', () => {
  assert.equal(view.stations.length, 8)
  assert.equal(view.revenue7d.length, 7)
  assert.equal(view.stationRanking.length, 8)
  assert.equal(view.actualLoad24h.length, 8 * 24)
  assert.equal(view.forecast24h.length, 6 * 24)
  assert.deepEqual(Object.keys(view.chargerStatus).sort(), ['charging', 'fault', 'idle', 'reserved', 'restarting'])
})

test('5 个图表 option 全部可生成且不含 undefined/NaN', () => {
  const options = {
    revenue: buildRevenueOption(view),
    status: buildStatusOption(view),
    ranking: buildRankingOption(view),
    station: buildStationOption(view),
    load: buildLoadForecastOption(view, view.stations[0].stationId),
  }
  for (const [name, option] of Object.entries(options)) assertCleanNumbers(option, name)
  assert.equal(options.revenue.xAxis.data.length, 7)
  assert.equal(options.station.series[0].data.length, 8)
})

test('未启用预测的站点显示「暂无预测」，且只画实际负荷一条线', () => {
  const disabled = view.stations.find((s) => !s.forecastEnabled)
  assert.ok(disabled, 'mock 中应保留未启用预测的站点用于演示降级')
  const option = buildLoadForecastOption(view, disabled.stationId)
  assert.equal(option.noForecast, true)
  assert.equal(option.message, '暂无预测')
  assert.equal(option.series.length, 1)
})

test('启用预测的站点画出实际+预测双曲线，预测段长度为 24', () => {
  const enabled = view.stations.find((s) => s.forecastEnabled)
  const option = buildLoadForecastOption(view, enabled.stationId)
  assert.equal(option.noForecast, false)
  assert.equal(option.series.length, 2)
  assert.equal(option.series[1].data.length, 24 + 24)
  assert.equal(option.series[1].data.slice(0, 24).filter((v) => v !== null).length, 0)
})

test('桩状态分布合计等于总桩数（口径自洽）', () => {
  const sum = Object.values(view.chargerStatus).reduce((a, b) => a + b, 0)
  assert.equal(sum, kpis.data.chargerCount)
})
