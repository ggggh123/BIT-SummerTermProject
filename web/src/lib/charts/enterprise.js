// 企业视角图表构造器（第二阶段新增；与 dashboard/assets/models.js 同风格：纯函数、无 DOM、无颜色）
// 颜色与主题由 ECharts 主题层统一控制，方便 UI 整体重做。

/** 营收 / 订单 / 电量三指标趋势（支持 7 / 30 / 90 日窗口切片后传入） */
export function buildRevenueTrendOption(points) {
  const rows = [...points].sort((a, b) => (a.date < b.date ? -1 : 1))
  return {
    tooltip: { trigger: 'axis' },
    legend: { data: ['营收', '订单', '电量'] },
    xAxis: { type: 'category', data: rows.map((r) => r.date.slice(5)), name: '日期' },
    yAxis: [
      { type: 'value', name: '元' },
      { type: 'value', name: '笔 / kWh' },
    ],
    series: [
      { name: '营收', type: 'line', smooth: true, areaStyle: {}, data: rows.map((r) => (r.revenueFen ?? 0) / 100) },
      { name: '订单', type: 'bar', yAxisIndex: 1, data: rows.map((r) => r.orderCount ?? 0) },
      { name: '电量', type: 'line', yAxisIndex: 1, smooth: true, data: rows.map((r) => r.energyKwh ?? 0) },
    ],
    meta: { days: rows.length },
  }
}

/** 站点营收排行（横向条形，按营收降序，附客单价与利用率） */
export function buildStationRevenueRankingOption(rows) {
  const sorted = [...rows].sort((a, b) => (b.revenueFen ?? 0) - (a.revenueFen ?? 0))
  return {
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    xAxis: { type: 'value', name: '元' },
    yAxis: { type: 'category', data: sorted.map((r) => r.name), name: '站点' },
    series: [
      {
        name: '营收',
        type: 'bar',
        data: sorted.map((r) => ({
          value: (r.revenueFen ?? 0) / 100,
          orderCount: r.orderCount ?? 0,
          avgTicketFen: r.orderCount ? Math.round((r.revenueFen ?? 0) / r.orderCount) : 0,
          utilizationRate: r.utilizationRate ?? 0,
        })),
      },
    ],
    meta: { stationCount: sorted.length },
  }
}

/** 用户 RFM 分层（玫瑰图：分层用户数，悬停带该层营收） */
export function buildRfmOption(rows) {
  return {
    tooltip: { trigger: 'item' },
    legend: { type: 'scroll', bottom: 0 },
    series: [
      {
        name: 'RFM 分层',
        type: 'pie',
        radius: ['35%', '70%'],
        roseType: 'radius',
        data: rows.map((r) => ({ name: r.segment, value: r.userCount ?? 0, revenueFen: r.revenueFen ?? 0 })),
      },
    ],
    meta: { segments: rows.length },
  }
}

/** 月度经营汇总：表格用数据（排序 + 分转元），不依赖 ECharts */
export function buildMonthlyRows(rows) {
  return [...rows]
    .sort((a, b) => (a.month < b.month ? -1 : 1))
    .map((r) => ({
      month: r.month.slice(5),
      revenueFen: r.revenueFen ?? 0,
      energyKwh: r.energyKwh ?? 0,
      orderCount: r.orderCount ?? 0,
      revenuePerChargerFen: r.revenuePerChargerFen ?? 0,
    }))
}
