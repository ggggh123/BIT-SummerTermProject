import test from 'node:test'
import assert from 'node:assert/strict'

import {
  buildMonthlyRows,
  buildRevenueTrendOption,
  buildRfmOption,
  buildStationRevenueRankingOption,
} from '../src/lib/charts/enterprise.js'

import revenueTrend from '../src/mock/enterprise_revenue-trend.json' with { type: 'json' }
import ranking from '../src/mock/enterprise_station-ranking.json' with { type: 'json' }
import rfm from '../src/mock/enterprise_user-rfm.json' with { type: 'json' }
import monthly from '../src/mock/enterprise_monthly.json' with { type: 'json' }
import userGrowth from '../src/mock/enterprise_user-growth.json' with { type: 'json' }

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

test('营收趋势：三条 series 且长度与窗口一致', () => {
  const opt = buildRevenueTrendOption(revenueTrend.data.points)
  assertClean(opt)
  assert.equal(opt.series.length, 3)
  assert.deepEqual(opt.series.map((s) => s.name), ['营收', '订单', '电量'])
  for (const s of opt.series) assert.equal(s.data.length, revenueTrend.data.points.length)
  // 订单与电量列必须真实非零，否则趋势图会退化成两条零线
  assert.ok(opt.series[1].data.every((v) => v > 0), '订单序列不应全为 0')
  assert.ok(opt.series[2].data.every((v) => v > 0), '电量序列不应全为 0')
})

test('营收趋势支持 7 / 30 窗口切片', () => {
  const all = revenueTrend.data.points
  assert.equal(all.length, 90)
  assert.equal(buildRevenueTrendOption(all.slice(-7)).series[0].data.length, 7)
  assert.equal(buildRevenueTrendOption(all.slice(-30)).series[0].data.length, 30)
})

test('站点营收排行：按营收降序且客单价非负', () => {
  const opt = buildStationRevenueRankingOption(ranking.data)
  assertClean(opt)
  const values = opt.series[0].data.map((d) => d.value)
  assert.deepEqual(values, [...values].sort((a, b) => b - a))
  assert.ok(opt.series[0].data.every((d) => d.avgTicketFen >= 0))
})

test('RFM：8 个分层且用户数均为正', () => {
  const opt = buildRfmOption(rfm.data)
  assertClean(opt)
  assert.equal(opt.series[0].data.length, 8)
  assert.ok(opt.series[0].data.every((d) => d.value > 0))
})

test('月度汇总：按月升序且金额换算正确', () => {
  const rows = buildMonthlyRows(monthly.data)
  assert.equal(rows.length, 4)
  assert.deepEqual(rows.map((r) => r.month), ['06', '07', '08', '09'])
  for (const [i, r] of rows.entries()) assert.equal(r.revenueFen, monthly.data[i].revenueFen)
})

test('用户增长：新增与日活均为非负数', () => {
  const points = userGrowth.data.points
  assert.equal(points.length, 30)
  assert.ok(points.every((p) => p.newUsers >= 0 && p.activeUsers >= 0))
})
