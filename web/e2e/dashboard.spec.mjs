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
  await expect(page.locator(KPI('累计营收'))).toContainText('元')
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
  // 复现开关：把底图 GeoJSON 人为延迟 N 毫秒，用来检验这条用例在「geo 迟到」时依然稳。
  //   PART2_SLOW_MAP=1500 npx playwright test -g "点击地图站点"
  // 这条用例曾经偶发失败，根因就是下面那个等待缺失（见下方注释）。
  if (process.env.PART2_SLOW_MAP) {
    await page.route('**/geo/beijing.json', async (route) => {
      await new Promise((r) => setTimeout(r, Number(process.env.PART2_SLOW_MAP)))
      await route.continue()
    })
  }
  await page.goto('/')
  await waitCharts(page, 5)

  // 主页在 1920×1080 下一屏放下，这里只是把地图面板滚进视口，避免点击坐标落在视口外
  await page.locator('.map-panel[aria-label="北京市站点分布"]').scrollIntoViewIfNeeded()

  // 底图是异步加载的：geo 到位前 mapReady=false，图表先退化成经纬度散点（没有 geo）。
  // 不在这一步等，取实例就会拿到"没有 geo 的图表"——那是**硬失败**（evaluate 里 throw，
  // 不会重试）。实测把底图人为延迟 1.5s 时，5 个 canvas 已就绪而 geo 还没到（3/3 轮），
  // 所以这个窗口是真会踩到的，而不是理论上的。
  await page.waitForFunction(
    () => [...document.querySelectorAll('.chart')].some((d) => d.__echarts?.getOption()?.geo),
    null,
    { timeout: 20000 },
  )

  // 触发方式说明：地图面板高 380px，底图受宽高比限制宽度有限，站点像素互相贴近
  // （见 docs/test/evidence/part2-2026-09-15/README.md §5），像素级点击会抖动。
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
  await expect(page.locator(KPI('累计订单'))).toHaveText(/999,999\s*笔/, { timeout: 20_000 })
  expect(calls).toBeGreaterThan(1)
})

test('接口失败：保留上次成功数据并提示，不白屏', async ({ page }) => {
  await page.goto('/')
  await waitCharts(page, 5)
  const before = await page.locator(KPI('累计订单')).textContent()

  await page.route('**/api/**', (route) => route.abort())
  // 顶部状态胶囊标「数据已过期」，页面内提示「数据加载失败…（保留上一次成功数据）」
  await expect(page.locator('.pill-error')).toBeVisible({ timeout: 20_000 })
  await expect(page.locator('.error-banner')).toContainText('保留上一次成功数据')
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
      // 放大 6% 后宽度仍自适应：不能出现横向滚动（横向滚动会藏掉右列面板）
      scrollWidth: document.documentElement.scrollWidth,
      viewportWidth: window.innerWidth,
    }
  })

  // 内容不高于设计框：1920×1080 下无需滚动即可看全（含数据质量面板与事件流）
  expect(layout.contentHeight).toBeLessThanOrEqual(layout.designHeight)
  // 地图是主视区：图高 ≥380 设计 px（修复前只有 300，且底图仅约 260px 宽）
  expect(layout.mapHeight).toBeGreaterThanOrEqual(380)
  // 主页按产品要求整体放大 6%（ScaleFrame zoom=1.06）：宽度仍自适应、无横向滚动，
  // 代价是纵向比一屏高出约 100px，底部事件流需要往下滚一点（原为「一屏放下 + 32px 容差」）。
  expect(layout.scrollWidth).toBeLessThanOrEqual(layout.viewportWidth)
  expect(layout.pageScrollHeight).toBeLessThanOrEqual(layout.viewportHeight + 150)
})

test('四个视角子页统一使用 DataV 大屏件，且渲染无报错', async ({ page }) => {
  const errors = []
  page.on('pageerror', (err) => errors.push(String(err)))
  page.on('console', (msg) => msg.type() === 'error' && errors.push(msg.text()))

  // [路由, 该页图表数]（新 UI 交付后：充电站视角 +预测占用桩、社会视角 +全城预测负荷）
  const pages = [
    ['/#/user', 4],
    ['/#/station', 5],
    ['/#/enterprise', 4],
    ['/#/gov', 4],
  ]
  for (const [hash, charts] of pages) {
    await page.goto(hash)
    await expect(page.locator('.chart canvas')).toHaveCount(charts)
    // 每页至少有 3 个 DataV 边框面板（子页统一观感）
    expect(await page.locator('.dv-border-box-8').count()).toBeGreaterThanOrEqual(3)
  }
  expect(errors).toEqual([])
})

test('两处容易误读的口径在页面上写清楚了', async ({ page }) => {
  // ① 政府页「全城峰值负荷」是**当日**口径：接口 /gov/peak-load 只返回单日 24 点，
  //    窗口内最大要更高（约 10%）。标题与提示必须能自证是哪一天，否则一定被问住。
  await page.goto('/#/gov')
  await expect(page.locator('.chart canvas')).toHaveCount(4)
  await expect(page.locator('.kpi-label:has-text("全城峰值负荷")')).toContainText('当日')
  const hint = await page.locator('.kpi-card:has-text("全城峰值负荷") .kpi-hint').innerText()
  expect(hint).toMatch(/\d{4}-\d{2}-\d{2}/)
  expect(hint).toContain('非窗口峰值')

  // ② 企业页「新增」实为窗口内首单新客（不是注册数），当前批次恒为 0。
  //    名字与说明都要写对，否则那条零线看着像数据坏了。
  await page.goto('/#/enterprise')
  await expect(page.locator('.chart canvas')).toHaveCount(4)
  // canvas 出现 ≠ option 就绪：接口数据回来前 series 还是空的（曾偶发取到 null）
  await expect
    .poll(
      () => page.evaluate(() => [...document.querySelectorAll('.chart')]
        .some((c) => c.__echarts?.getOption()?.series?.some((s) => s.name === '窗口内首单新客'))),
      { timeout: 15_000, message: '企业页「窗口内首单新客」系列未出现' },
    )
    .toBe(true)
  const growth = await page.evaluate(() => {
    const chart = [...document.querySelectorAll('.chart')].find((c) =>
      c.__echarts?.getOption()?.series?.some((s) => s.name === '窗口内首单新客'),
    )
    if (!chart) return null
    const opt = chart.__echarts.getOption()
    return {
      names: opt.series.map((s) => s.name),
      legend: opt.legend?.[0]?.data ?? null,
      points: opt.series[0].data.length,
    }
  })
  expect(growth).not.toBeNull()
  expect(growth.names).toContain('窗口内首单新客')
  expect(growth.names).toContain('日活充电用户')
  expect(growth.legend).toContain('窗口内首单新客')
  expect(growth.points).toBeGreaterThan(0)
  await expect(page.locator('.chart-note').filter({ hasText: '不是注册数' })).toBeVisible()
})
