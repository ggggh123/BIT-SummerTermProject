import test from 'node:test'
import assert from 'node:assert/strict'
import { buildHomeViewModel } from '../src/lib/viewModel.js'
import { buildBeijingStationOption } from '../src/lib/charts/beijingStation.js'
import { buildPeakHeatmapOption, buildPriceDistanceOption } from '../src/lib/charts/user.js'
import { buildPeakLoadOption } from '../src/lib/charts/gov.js'
import { withEnergyTheme, compactNumber } from '../src/lib/chartTheme.js'
import { readEnvelope } from '../src/lib/envelope.js'
import { createLatestRequest } from '../src/lib/latestRequest.js'
import { coordinate } from '../src/lib/coordinates.js'
import { formatKwh, formatForecastSource } from '../src/lib/contracts.js'

test('展示电量分组且保留两位小数，缺失值不能伪造为零', () => {
  assert.equal(formatKwh(3430028.4), '3,430,028.40 kWh')
  assert.equal(formatKwh(0), '0.00 kWh')
  for (const value of [null, undefined, NaN, Infinity, '123']) assert.equal(formatKwh(value), '—')
})
test('预测来源按批次明确区分 ML、基线和未标明，保留模型版本', () => {
  assert.equal(formatForecastSource({ isBaseline: false, modelVersion: 'gbt-v2' }), 'ML 模型预测 / gbt-v2')
  assert.equal(formatForecastSource({ isBaseline: true }), '基线预测')
  assert.equal(formatForecastSource({}), '预测（来源未标明）')
})

test('Q9 缺坐标站点保留在选择列表，只从地图排除，不伪造为 0', () => {
  const view = buildHomeViewModel({ stations: [
    { stationId: '1', longitude: '116.4', latitude: '39.9' },
    { stationId: '2', longitude: null, latitude: '39.9' },
    { stationId: '3', longitude: '', latitude: undefined },
  ] })
  assert.equal(view.stations.length, 3)
  assert.equal(view.stations[1].longitude, null)
  const option = buildBeijingStationOption(view)
  assert.equal(option.meta.unlocatedCount, 2)
  assert.deepEqual(option.series[0].data.map(d => d.stationId), [1])
})
test('坐标拒绝空值、布尔、非有限值和越界，数字字符串仍兼容', () => {
  for (const v of [null, undefined, '', ' ', false, true, Infinity, 'bad', 200]) assert.equal(coordinate(v, 180), null)
  assert.equal(coordinate('116.3', 180), 116.3)
})
test('距离为空的站点不画到零距离，其他图表仍可统计该站', () => {
  const rows = [{ stationId: 1, name: '有效站', distanceKm: 5, priceFenPerKwh: 160, idleCount: 3 }, { stationId: 2, distanceKm: null }]
  const option = buildPriceDistanceOption(rows)
  assert.equal(option.series[0].data.length, 1)
  assert.equal(option.meta.unlocatedCount, 1)
  assert.equal(rows.length, 2)
})
test('地图提示转义名称并保留服务半径', () => {
  const option = buildBeijingStationOption({ stations: [{ stationId: 1, name: '<b>站点</b>', longitude: 116.4, latitude: 39.9, serviceRadiusKm: 3 }] })
  const text = option.tooltip.formatter({ data: option.series[0].data[0] })
  assert.ok(text.includes('&lt;b&gt;'))
  assert.ok(text.includes('服务半径约 3 km'))
})
test('统一图表主题不改变原始值或原构造器，坐标网格不再使用白色', () => {
  const source = { xAxis: { type: 'category', data: ['08-01'] }, yAxis: { type: 'value' }, series: [{ name: '电量', type: 'bar', data: [123.45] }] }
  const before = JSON.stringify(source)
  const option = withEnergyTheme(source)
  assert.deepEqual(option.series[0].data, [123.45])
  assert.equal(JSON.stringify(source), before)
  assert.equal(option.grid.containLabel, true)
  assert.equal(option.yAxis[0].splitLine.lineStyle.color, '#24404a')
})
test('零值饼图不重写数据，减弱动态效果设置被遵守', () => {
  const option = withEnergyTheme({ series: [{ type: 'pie', data: [{ name: '空闲', value: 0 }] }] }, { reducedMotion: true })
  assert.equal(option.animation, false)
  assert.equal(option.series[0].data[0].value, 0)
  assert.equal(option.series[0].label.show, false)
})
test('环形图数字与总量整体锚定圆心，不用固定下移的第二行文本', () => {
  for (const count of [0, 36, 251, 287, 4985]) {
    const source = { series: [{ type: 'pie', data: [{ name: '总计', value: count }] }] }
    const option = withEnergyTheme(source)
    const group = option.graphic[0], text = group.children[0]
    assert.deepEqual([group.left, group.top], option.series[0].center)
    assert.equal(group.bounding, 'raw')
    assert.equal(group.children.length, 1)
    assert.equal(text.style.verticalAlign, 'middle')
    assert.equal(text.style.align, 'center')
    assert.equal(text.style.text, `{value|${count}}\n{caption|总量}`)
    assert.equal(text.style.rich.value.lineHeight + text.style.rich.caption.lineHeight, 42)
    assert.equal(text.top, undefined)
    assert.equal(text.silent, true)
    assert.ok(text.z > 2)
    assert.deepEqual(option.series[0].data, source.series[0].data)
  }
})
test('空热力图和无当日负荷不会生成 Infinity、伪造峰值', () => {
  assert.equal(buildPeakHeatmapOption({ hours: [], names: [], values: [] }).visualMap.max, 1)
  const peak = buildPeakLoadOption({ dt: null, points: [] })
  assert.equal(peak.meta.peakLoadKw, null)
  assert.deepEqual(peak.series[0].data, [])
})
test('坐标轴数字缩写仅作用于显示，不对数值作四舍五入回写', () => {
  assert.equal(compactNumber(126000), '12.6万')
  assert.equal(compactNumber(300), '300')
  assert.equal(compactNumber(NaN), '—')
})
test('4041 无预测降级为空曲线/空推荐，非预测接口不能吞掉错误', () => {
  const body = { code: 4041, message: 'no active forecast', data: null }
  assert.deepEqual(readEnvelope(body, 'forecast/24h').data.points, [])
  assert.deepEqual(readEnvelope(body, 'forecast/recommend').data, [])
  assert.throws(() => readEnvelope(body, 'overview/kpis'), /4041/)
})
test('非正常信封仍抛错，不把网络和业务错误伪装成零值', () => {
  for (const body of [null, {}, { code: 0, data: null }, { code: 5000, data: {} }]) assert.throws(() => readEnvelope(body, 'overview/kpis'))
  assert.deepEqual(readEnvelope({ code: 0, data: { orders: 0 } }, 'overview/kpis').data, { orders: 0 })
})
test('快速切换站点：只有最新响应能应用；离开页面可作废在途响应', () => {
  const gate = createLatestRequest(), first = gate.begin(), second = gate.begin()
  assert.equal(first(), false)
  assert.equal(second(), true)
  gate.invalidate()
  assert.equal(second(), false)
})
