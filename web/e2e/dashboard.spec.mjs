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

test('主页营收趋势确实是近 30 日（不是 7 日）', async ({ page }) => {
  await page.goto('/')
  await waitCharts(page, 5)

  // 面板标题与图表实际点数必须一致：此前接口写死 days=7，
  // 于是图上只有 7 根柱子却挂着「近 30 日」的标题。
  const panel = page.locator('.dv-border-box-8:has-text("近 30 日营收趋势")')
  await expect(panel).toHaveCount(1)

  const series = await page.evaluate(() => {
    const chart = [...document.querySelectorAll('.chart')].find(
      (c) => c.__echarts?.getOption()?.series?.[0]?.name === '近 30 日营收',
    )
    if (!chart) return null
    const opt = chart.__echarts.getOption()
    return {
      points: opt.series[0].data.length,
      nonEmpty: opt.series[0].data.filter((v) => v !== null && v !== undefined).length,
      labels: (opt.xAxis?.[0]?.data ?? []).length,
      unit: opt.series[0].unit ?? null,
    }
  })

  expect(series).not.toBeNull()
  expect(series.points).toBe(30)
  expect(series.nonEmpty).toBe(30)
  expect(series.labels).toBe(30)
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
    // 不写死站点号：PRL 清洗后站点集合会变（官方批 7 站，id 不保证连续），取第一个带 stationId 的点
    const index = data.findIndex((d) => d.stationId)
    const point = data[index]
    if (!point) throw new Error('地图数据里没有任何带 stationId 的站点')
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
  expect(target.pointsWithId).toBeGreaterThan(0) // 每个点位都带 stationId，跳转才有依据
  expect(target.stationId).toBeTruthy()

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
  // 不写死订单数：官方 ADS（PRL 清洗后 97,804 单）与本地 Python 物化批（112,422 单）不同，
  // 这里只验证"轮询后页面数值确实跟着接口变化"。
  const initialOrders = (await page.locator(KPI('累计订单')).textContent())?.trim()
  expect(initialOrders).toBeTruthy()
  expect(initialOrders).not.toBe('999,999')
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

test('DataV 大屏件已渲染（老师要求第 5 条）', async ({ page }) => {
  await page.goto('/')
  await waitCharts(page, 5)

  // dv-border-box-8 包住 5 个图表面板 + 数据质量 / 事件流两个面板
  const frames = page.locator('.dv-border-box-8')
  expect(await frames.count()).toBeGreaterThanOrEqual(5)
  const first = await frames.first().boundingBox()
  expect(first.width).toBeGreaterThan(200)
  expect(first.height).toBeGreaterThan(100)

  // 事件流已换成 DataV 滚动榜单，且父容器给了确定高度（否则高度为 0）
  const board = page.locator('.dv-scroll-board')
  await expect(board).toHaveCount(1)
  const boardBox = await board.boundingBox()
  expect(boardBox.height).toBeGreaterThan(100)
})

test('主页版面：1920×1080 一屏放下，地图占据主视区', async ({ page }) => {
  await page.goto('/')
  await waitCharts(page, 5)

  const layout = await page.evaluate(() => {
    const inner = document.querySelector('.scale-inner')
    const charts = [...document.querySelectorAll('.chart')]
    const map = charts.find((c) => c.__echarts?.getOption()?.geo)
    const kpi = document.querySelector('.kpi-card')
    return {
      // scrollHeight 是元素自身 CSS 像素 = 设计像素（transform 缩放不影响布局尺寸）
      contentHeight: inner.scrollHeight,
      designHeight: 1080,
      mapHeight: map?.clientHeight ?? 0,
      kpiWidth: kpi?.clientWidth ?? 0,
      pageScrollHeight: document.documentElement.scrollHeight,
      viewportHeight: window.innerHeight,
    }
  })

  // 内容不高于设计框：1920×1080 下无需滚动即可看全（含数据质量面板与事件流）
  expect(layout.contentHeight).toBeLessThanOrEqual(layout.designHeight)
  // 地图是主视区：图高 ≥380 设计 px（修复前只有 300，且底图仅约 260px 宽）
  expect(layout.mapHeight).toBeGreaterThanOrEqual(380)
  // 整页在 1080 高度内放下（留 32px 容差给顶栏/页脚）
  expect(layout.pageScrollHeight).toBeLessThanOrEqual(layout.viewportHeight + 32)
})

test('四个视角子页统一使用 DataV 大屏件，且渲染无报错', async ({ page }) => {
  const errors = []
  page.on('pageerror', (err) => errors.push(String(err)))
  page.on('console', (msg) => msg.type() === 'error' && errors.push(msg.text()))

  // [路由, 该页图表数]
  const pages = [
    ['/#/user', 4],
    ['/#/station', 4],
    ['/#/enterprise', 4],
    ['/#/gov', 3],
  ]
  for (const [hash, charts] of pages) {
    await page.goto(hash)
    await expect(page.locator('.chart canvas')).toHaveCount(charts)
    // 每页至少有 3 个 DataV 边框面板（子页统一观感）
    expect(await page.locator('.dv-border-box-8').count()).toBeGreaterThanOrEqual(3)
  }
  expect(errors).toEqual([])
})
