// 页面级 E2E：补上纯函数测试覆盖不到的三条（P1-1）
//   1) 主页真实渲染 5 个图表，KPI 有值，控制台零报错
//   2) 点击地图上的站点真实跳转 /#/station?station=<id>，且充电站视角选中同一个站
//   3) 5s 轮询确实重绘（篡改接口返回值后页面数值随之变化）
//   4) 接口失败时保留上一次成功数据并提示过期，不白屏
import { test, expect } from '@playwright/test'

const KPI = (label) => `.kpi-card:has-text("${label}") .kpi-value`

async function waitCharts(page, count) {
  await expect(page.locator('.chart canvas')).toHaveCount(count)
}

test('主页：5 个图表渲染 + KPI 有值 + 控制台零报错', async ({ page }) => {
  const errors = []
  page.on('console', (msg) => msg.type() === 'error' && errors.push(msg.text()))
  page.on('pageerror', (err) => errors.push(String(err)))

  await page.goto('/')
  await expect(page.locator('.kpi-card')).toHaveCount(4)
  await waitCharts(page, 5)
  await expect(page.locator(KPI('累计营收'))).toContainText('¥')
  await expect(page.locator(KPI('累计订单'))).not.toHaveText('0')
  await expect(page.locator('text=数据加载失败')).toHaveCount(0)
  expect(errors).toEqual([])
})

test('点击地图站点 → 跳转充电站视角且选中同一站点', async ({ page, request }) => {
  await page.goto('/')
  await waitCharts(page, 5)

  // 主页在 1920×1080 下需要滚动才能看到地图面板（内容高于设计高度），先滚到位再点
  await page.locator('.panel', { hasText: '北京市站点分布' }).scrollIntoViewIfNeeded()

  // 触发方式说明：主页地图面板只有 300px 高，底图受宽高比限制仅约 260px 宽，8 个站点像素
  // 互相贴近（见 docs/test/evidence/part2-2026-09-15/README.md §5），像素级点击会抖动。
  // 因此这里通过 ECharts 实例的事件通道触发 click —— 走的仍是完整链路：
  // chart.on('click') → EChart emit('chart-click') → HomeView.onStationClick → router.push。
  const target = await page.evaluate(() => {
    const el = [...document.querySelectorAll('.chart')].find(
      (div) => div.__echarts?.getOption()?.geo,
    )
    if (!el) throw new Error('找不到地图图表实例（EChart 未挂 __echarts？）')
    const inst = el.__echarts
    const data = inst.getOption().series[0].data
    const index = data.findIndex((d) => d.stationId === 3) // 固定挑 3 号站，断言可预期
    const point = data[index]
    inst.trigger('click', {
      componentType: 'series',
      seriesType: 'scatter',
      seriesIndex: 0,
      dataIndex: index,
      data: point,
      name: point.name,
    })
    return { stationId: point.stationId, name: point.name, pointsWithId: data.filter((d) => d.stationId).length }
  })
  expect(target.pointsWithId).toBe(8) // 每个点位都带 stationId，跳转才有依据
  expect(target.stationId).toBe(3)

  await expect(page).toHaveURL(new RegExp(`/station\\?station=${target.stationId}`))
  const navigatedId = Number(new URL(page.url()).hash.match(/station=(\d+)/)[1])
  expect(navigatedId).toBeGreaterThan(0)
  const stations = await (await request.get('/api/overview/stations')).json()
  const station = stations.data.find((s) => s.stationId === navigatedId)
  expect(station, `跳转的站点 ${navigatedId} 必须在站点列表里`).toBeTruthy()
  // 充电站视角的两条落地证据：站点选择器的值 = URL 里的 id；详情行显示该站名称
  await expect(page.locator('select').first()).toHaveValue(String(navigatedId))
  await expect(page.locator('p').filter({ hasText: '设备故障率' })).toContainText(station.name)
  await expect(page.locator('text=数据加载失败')).toHaveCount(0)
})

test('5s 轮询确实重绘：接口第二次返回不同订单数，页面随之变化', async ({ page }) => {
  let calls = 0
  await page.route('**/api/overview/kpis*', async (route) => {
    calls += 1
    const response = await route.fetch()
    const body = await response.json()
    if (calls >= 2) body.data.totalOrders = 999999
    await route.fulfill({ response, json: body })
  })

  await page.goto('/')
  await expect(page.locator(KPI('累计订单'))).toHaveText('112,422')
  await expect(page.locator(KPI('累计订单'))).toHaveText('999,999', { timeout: 20_000 })
  expect(calls).toBeGreaterThan(1)
})

test('接口失败：保留上次成功数据并提示，不白屏', async ({ page }) => {
  await page.goto('/')
  await waitCharts(page, 5)
  const before = await page.locator(KPI('累计订单')).textContent()

  await page.route('**/api/**', (route) => route.abort())
  // 顶部状态胶囊标「数据已过期」，页面内提示「数据加载失败…（保留上一次成功数据）」
  await expect(page.locator('.pill-error')).toBeVisible({ timeout: 20_000 })
  await expect(page.getByText('页面保留上一次成功数据').first()).toBeVisible()
  // 旧数据仍在
  await expect(page.locator(KPI('累计订单'))).toHaveText(before)
  await expect(page.locator('.kpi-card')).toHaveCount(4)
  await waitCharts(page, 5)
})
