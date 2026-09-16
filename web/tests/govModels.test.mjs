import test from 'node:test'
import assert from 'node:assert/strict'

import {
  buildCarbonSummary,
  buildCityForecastOption,
  buildDistrictCoverageOption,
  buildPeakLoadOption,
  buildServiceStatsRows,
  buildUtilizationFairnessOption,
} from '../src/lib/charts/gov.js'

import coverage from './fixtures/gov_coverage.json' with { type: 'json' }
import serviceStats from './fixtures/gov_service-stats.json' with { type: 'json' }
import carbon from './fixtures/gov_carbon.json' with { type: 'json' }
import peakLoad from './fixtures/gov_peak-load.json' with { type: 'json' }
import utilization from './fixtures/gov_utilization.json' with { type: 'json' }
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

test('行政区覆盖：双轴两序列，按桩数降序', () => {
  const opt = buildDistrictCoverageOption(coverage.data)
  assertClean(opt)
  assert.equal(opt.series.length, 2)
  const vals = opt.series[0].data
  assert.deepEqual(vals, [...vals].sort((a, b) => b - a))
  assert.equal(opt.meta.districts, coverage.data.length)
})

test('区域服务指标：按订单降序并算出人均单量', () => {
  const rows = buildServiceStatsRows(serviceStats.data)
  assert.ok(rows.length >= 3)
  const orders = rows.map((r) => r.orderCount)
  assert.deepEqual(orders, [...orders].sort((a, b) => b - a))
  for (const r of rows) assert.ok(r.ordersPerUser > 0)
})

test('碳减排：电量×因子与给定减排量自洽', () => {
  const c = buildCarbonSummary(carbon.data)
  assertClean(c)
  assert.equal(c.consistent, true, '减排量应等于 电量(MWh) × 因子')
  assert.ok(c.co2SavedTon > 0 && c.factorTonPerMwh > 0)
  assert.ok(c.note.includes('tCO'))
})

test('全城负荷：24 点且峰值元数据与最大值一致', () => {
  const opt = buildPeakLoadOption(peakLoad.data)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 24)
  const max = Math.max(...opt.series[0].data)
  assert.equal(opt.meta.peakLoadKw, max)
  assert.ok(opt.series[0].markPoint.data.length === 1)
  // 接口按「单日」返回，界面必须能说出这是哪一天——否则 KPI 的
  //「全城峰值负荷」会被读成窗口口径（两者相差约 10%）
  assert.equal(opt.meta.date, peakLoad.data.dt)
})

test('利用率公平性：按利用率升序并带全市均值参考线', () => {
  const opt = buildUtilizationFairnessOption(utilization.data)
  assertClean(opt)
  const vals = opt.series[0].data.map((d) => d.value)
  assert.deepEqual(vals, [...vals].sort((a, b) => a - b))
  assert.ok(opt.series[0].markLine.data[0].xAxis > 0)
  assert.ok(vals.every((v) => v >= 0 && v <= 100))
})

test('全城预测：按小时对各站求和，峰值与高峰预警自洽', () => {
  const opt = buildCityForecastOption(forecast24h.data.points)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 24)
  const firstHour = forecast24h.data.points.filter((p) => p.horizonH === 1)
  const expected = Number(firstHour.reduce((s, p) => s + p.predictedLoadKw, 0).toFixed(1))
  assert.ok(Math.abs(opt.series[0].data[0] - expected) < 0.05, '第 1 小时应为各站预测负荷之和')
  assert.equal(opt.meta.peakLoadKw, Math.max(...opt.series[0].data))
  assert.ok(opt.meta.warnHours.includes(opt.meta.peakHour), '峰值小时必须落在预警时段内')
  assert.ok(opt.meta.predictedEnergyKwh > 0 && opt.meta.stations === firstHour.length)
})

test('全城预测：无预测点时给出空图与空元数据（界面显示「暂无预测」）', () => {
  const opt = buildCityForecastOption([])
  assert.equal(opt.meta.hours, 0)
  assert.equal(opt.meta.peakHour, null)
  assert.deepEqual(opt.series[0].data, [])
})
