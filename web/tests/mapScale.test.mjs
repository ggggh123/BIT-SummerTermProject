import test from 'node:test'
import assert from 'node:assert/strict'

import { BEIJING_MAP, buildBeijingStationOption } from '../src/lib/charts/beijingStation.js'
import { computeScale } from '../src/lib/scale.js'
import stations from '../src/mock/overview_stations.json' with { type: 'json' }

const view = { stations: stations.data }

test('北京地图站点：使用注册的 beijing 底图，点位等于站点数', () => {
  const opt = buildBeijingStationOption(view)
  assert.equal(opt.geo.map, BEIJING_MAP)
  assert.equal(opt.series[0].coordinateSystem, 'geo')
  assert.equal(opt.series[0].data.length, 8)
  assert.equal(opt.meta.stationCount, 8)
})

test('地图点位：经纬度在北京范围内，点大小随桩数变化', () => {
  const opt = buildBeijingStationOption(view)
  const data = opt.series[0].data
  for (const d of data) {
    assert.ok(d.value[0] > 115.4 && d.value[0] < 117.5, '经度应在北京范围')
    assert.ok(d.value[1] > 39.4 && d.value[1] < 41.1, '纬度应在北京范围')
    assert.ok(Number.isFinite(d.symbolSize) && d.symbolSize >= 9)
  }
  const sizes = data.map((d) => d.symbolSize)
  assert.ok(Math.max(...sizes) > Math.min(...sizes))
})

test('地图点位：带 stationId 以支持点击跳转充电站视角', () => {
  const opt = buildBeijingStationOption(view)
  assert.ok(opt.series[0].data.every((d) => Number.isInteger(d.stationId)))
})

test('buildBeijingStationOption 对空数据不报错', () => {
  const opt = buildBeijingStationOption({})
  assert.equal(opt.series[0].data.length, 0)
  assert.equal(opt.meta.stationCount, 0)
})

test('computeScale：按较小的一边等比缩放', () => {
  assert.equal(computeScale(1920, 1080), 1)
  assert.equal(computeScale(960, 540), 0.5)
  assert.equal(computeScale(3840, 1080), 1) // 高度受限
  assert.equal(computeScale(1920, 2160), 1) // 宽度受限
  assert.equal(computeScale(1440, 900), 0.75) // 宽 0.75，高 0.833 → 取 0.75
})

test('computeScale：非法输入回退为 1', () => {
  assert.equal(computeScale(0, 0), 1)
  assert.equal(computeScale(undefined, 1080), 1)
  assert.equal(computeScale(1920, NaN), 1)
  assert.equal(computeScale(1920, 1080, 0, 0), 1)
})
