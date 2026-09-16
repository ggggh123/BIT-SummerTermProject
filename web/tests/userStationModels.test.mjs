import test from 'node:test'
import assert from 'node:assert/strict'

import {
  buildIdleRankingOption,
  buildPeakHeatmapOption,
  buildPriceCompareOption,
  buildPriceDistanceOption,
} from '../src/lib/charts/user.js'
import {
  buildCoverageOption,
  buildHealthOption,
  buildMixOption,
  buildStationOccupancyOption,
  buildUtilizationOption,
} from '../src/lib/charts/station.js'

import priceCompare from './fixtures/user_price-compare.json' with { type: 'json' }
import priceDistance from './fixtures/user_price-distance.json' with { type: 'json' }
import idleRanking from './fixtures/user_idle-ranking.json' with { type: 'json' }
import peakHeatmap from './fixtures/user_peak-heatmap.json' with { type: 'json' }
import coverage from './fixtures/station_coverage.json' with { type: 'json' }
import stationDetail from './fixtures/station_detail.json' with { type: 'json' }
import forecast24h from './fixtures/forecast_24h.json' with { type: 'json' }

function assertClean(node, path = 'opt') {
  if (typeof node === 'number') return assert.ok(Number.isFinite(node), `${path} 非有限数`)
  if (Array.isArray(node)) return node.forEach((v, i) => assertClean(v, `${path}[${i}]`))
  if (node && typeof node === 'object') {
    for (const [k, v] of Object.entries(node)) {
      assert.notEqual(v, undefined, `${path}.${k} 为 undefined`)
      assertClean(v, `${path}.${k}`)
    }
  }
}

test('价格对比：含全市均价线且按电价升序', () => {
  const opt = buildPriceCompareOption(priceCompare.data)
  assertClean(opt)
  const vals = opt.series[0].data.map((d) => d.value)
  assert.deepEqual(vals, [...vals].sort((a, b) => a - b))
  assert.ok(opt.series[0].markLine.data[0].yAxis > 0)
  assert.equal(opt.series[0].data.length, 8)
})

test('距离-价格散点：8 个点且点大小随空闲桩数变化', () => {
  const opt = buildPriceDistanceOption(priceDistance.data)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 8)
  const sizes = opt.series[0].data.map((d) => d.symbolSize)
  assert.ok(Math.max(...sizes) > Math.min(...sizes), '点大小应有区分度')
})

test('空闲排行：按空闲数升序排列（横向条自下而上）', () => {
  const opt = buildIdleRankingOption(idleRanking.data)
  assertClean(opt)
  const vals = opt.series[0].data.map((d) => d.value)
  assert.deepEqual(vals, [...vals].sort((a, b) => a - b))
})

test('时段热力图：格子数 = 站点数 × 24', () => {
  const opt = buildPeakHeatmapOption(peakHeatmap.data)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, peakHeatmap.data.names.length * 24)
  assert.ok(opt.visualMap.max > 0)
})

test('单站利用率：24 点且不超过 100%', () => {
  // 接口契约：GET /station/{id}/utilization 返回 { stationId, name, points }
  const d = stationDetail.data['1']
  const opt = buildUtilizationOption({ stationId: d.stationId, name: d.name, ...d.utilization })
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 24)
  assert.ok(opt.series[0].data.every((v) => v >= 0 && v <= 100))
  assert.equal(opt.meta.stationId, d.stationId)
})

test('快慢充结构：快慢桩数之和等于总桩数', () => {
  const detail = stationDetail.data['1']
  const opt = buildMixOption(detail.mix)
  assertClean(opt)
  const sum = opt.series[0].data.reduce((s, d) => s + d.value, 0)
  assert.equal(sum, detail.chargerCount)
})

test('设备健康：Top 桩按次数降序且故障率在合理区间', () => {
  const opt = buildHealthOption(stationDetail.data['2'].health)
  assertClean(opt)
  const vals = opt.series[0].data
  assert.deepEqual(vals, [...vals].sort((a, b) => b - a))
  assert.ok(opt.meta.faultRate >= 0 && opt.meta.faultRate <= 100)
})

test('区域覆盖：8 个站点且经纬度在北京范围内', () => {
  const opt = buildCoverageOption(coverage.data)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 8)
  for (const d of opt.series[0].data) {
    assert.ok(d.value[0] > 115.4 && d.value[0] < 117.5, '经度应在北京范围')
    assert.ok(d.value[1] > 39.4 && d.value[1] < 41.1, '纬度应在北京范围')
  }
})

test('单站占用预测：占用+空闲=本站桩数，并给出最忙/最空闲与满载风险时段', () => {
  const points = forecast24h.data.points.filter((p) => p.stationId === 1)
  const chargerCount = stationDetail.data['1'].chargerCount
  const opt = buildStationOccupancyOption(points, chargerCount)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 24)
  assert.equal(opt.meta.points, 24)
  assert.equal(opt.meta.chargerCount, chargerCount)
  for (let i = 0; i < 24; i += 1) {
    assert.equal(
      opt.series[0].data[i].value + opt.series[1].data[i],
      chargerCount,
      `第 ${i + 1} 小时「占用 + 空闲」应等于本站桩数`,
    )
  }
  assert.ok(opt.meta.busiestHour && opt.meta.idlestHour, '应给出最忙与最空闲时段')
  assert.ok(opt.meta.busiestOccupied > 0 && opt.meta.idlestIdle > 0)
  assert.ok(opt.meta.busiestUtilization > 0 && opt.meta.busiestUtilization <= 100)
  assert.ok(opt.meta.predictedEnergyKwh > 0)
  assert.ok(Array.isArray(opt.meta.fullLoadHours))
  assert.ok(opt.series[0].data.every((d) => d.itemStyle.color), '每个点都应有预测拥堵等级配色')
})

test('单站占用预测：无该站点预测点时返回 noForecast（界面显示「暂无预测」而非空图）', () => {
  const opt = buildStationOccupancyOption([], null)
  assert.equal(opt.meta.noForecast, true)
  assert.equal(opt.meta.chargerCount, null)
  assert.deepEqual(opt.series[0].data, [])
  assert.deepEqual(opt.meta.fullLoadHours, [])
})
