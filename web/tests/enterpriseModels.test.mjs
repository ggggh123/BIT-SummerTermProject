import test from 'node:test'
import assert from 'node:assert/strict'

import {
  buildMonthlyRows,
  buildRevenueTrendOption,
  buildRfmOption,
  buildStationRevenueRankingOption,
  buildUserGrowthOption,
} from '../src/lib/charts/enterprise.js'

import revenueTrend from './fixtures/enterprise_revenue-trend.json' with { type: 'json' }
import ranking from './fixtures/enterprise_station-ranking.json' with { type: 'json' }
import rfm from './fixtures/enterprise_user-rfm.json' with { type: 'json' }
import monthly from './fixtures/enterprise_monthly.json' with { type: 'json' }
import userGrowth from './fixtures/enterprise_user-growth.json' with { type: 'json' }

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

test('用户增长：口径写进图例，且不为空数据渲染出 NaN', () => {
  const opt = buildUserGrowthOption(userGrowth.data.points)
  assertClean(opt)
  // 「新增」实为窗口内首单新客（不是注册数）。名字写错会在答辩时被当成数据故障，
  // 所以这里把口径钉在测试里。
  assert.deepEqual(opt.series.map((s) => s.name), ['窗口内首单新客', '日活充电用户'])
  assert.deepEqual(opt.legend.data, ['窗口内首单新客', '日活充电用户'])
  assert.equal(opt.series[0].data.length, userGrowth.data.points.length)
  assert.equal(opt.series[1].data.length, userGrowth.data.points.length)
  assert.ok(opt.meta.caliber.includes('不是注册数'))
  // 缺字段时按 0 处理，不能把 undefined 带进图表
  const sparse = buildUserGrowthOption([{ date: '2026-09-14' }])
  assertClean(sparse)
  assert.deepEqual(sparse.series[0].data, [0])
  assert.deepEqual(sparse.series[1].data, [0])
  assert.equal(sparse.meta.zeroNewUserDays, 1)
})
