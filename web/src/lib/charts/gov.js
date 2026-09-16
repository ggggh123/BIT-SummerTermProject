// 政府视角图表构造器（「设施是否服务好民生」）

/** 行政区覆盖密度：桩数（柱）+ 每万人桩数（线，双轴） */
export function buildDistrictCoverageOption(rows) {
  const sorted = [...rows].sort((a, b) => b.chargerCount - a.chargerCount)
  return {
    tooltip: { trigger: 'axis' },
    legend: { data: ['充电桩数', '每万人桩数'] },
    xAxis: { type: 'category', data: sorted.map((r) => r.district), axisLabel: { fontSize: 11 } },
    yAxis: [
      { type: 'value', name: '个' },
      { type: 'value', name: '个/万人' },
    ],
    series: [
      { name: '充电桩数', type: 'bar', data: sorted.map((r) => r.chargerCount) },
      { name: '每万人桩数', type: 'line', yAxisIndex: 1, smooth: true, data: sorted.map((r) => r.chargersPer10k) },
    ],
    meta: { districts: sorted.length },
  }
}

/** 区域服务指标表（纯表格数据） */
export function buildServiceStatsRows(rows) {
  return [...rows]
    .sort((a, b) => b.orderCount - a.orderCount)
    .map((r) => ({
      district: r.district,
      orderCount: r.orderCount,
      servedUserCnt: r.servedUserCnt,
      avgWaitMin: r.avgWaitMin,
      ordersPerUser: Number((r.orderCount / Math.max(1, r.servedUserCnt)).toFixed(1)),
    }))
}

/** 碳减排换算（口径自洽：减排量 = 电量 MWh × 因子） */
export function buildCarbonSummary(carbon) {
  const mwh = carbon.totalEnergyKwh / 1000
  return {
    totalEnergyKwh: carbon.totalEnergyKwh,
    co2SavedTon: carbon.co2SavedTon,
    factorTonPerMwh: carbon.factorTonPerMwh,
    recomputedTon: Number((mwh * carbon.factorTonPerMwh).toFixed(1)),
    consistent: Math.abs(mwh * carbon.factorTonPerMwh - carbon.co2SavedTon) < 0.5,
    note: carbon.factorNote,
    equivalentTrees: carbon.equivalentTrees,
  }
}

/** 全城高峰负荷曲线（辅助电网调度），标注峰值时段 */
export function buildPeakLoadOption(data) {
  const points = [...data.points]
  if (!points.length) return {
    xAxis: { type: 'category', data: [], name: '时刻' }, yAxis: { type: 'value', name: 'kW' },
    series: [{ name: '全城负荷', type: 'line', data: [] }],
    meta: { date: data.dt ?? null, peakHour: null, peakLoadKw: null },
  }
  const peak = points.reduce((acc, p, i) => (p.loadKw > points[acc].loadKw ? i : acc), 0)
  return {
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'category', data: points.map((p) => p.hour), name: '时刻' },
    yAxis: { type: 'value', name: 'kW' },
    series: [
      {
        name: '全城负荷',
        type: 'line',
        smooth: true,
        areaStyle: {},
        data: points.map((p) => p.loadKw),
        markPoint: { data: [{ coord: [peak, points[peak].loadKw], name: '峰值', value: points[peak].loadKw }] },
      },
    ],
    // meta.date 必须带上：接口 /gov/peak-load 只返回**单日** 24 点，
    // 大屏 KPI 是「当日峰值」而不是窗口峰值，界面要把这个口径显式说出来。
    meta: {
      date: data.dt ?? null,
      peakHour: points[peak].hour,
      peakLoadKw: points[peak].loadKw,
    },
  }
}

/**
 * 全城未来 24h 预测负荷（各启用站按 horizon 求和）与高峰预警。
 * 只做求和与排序，不改变任何单站预测值；预警时段按「预测负荷 ≥ 0.9 × 峰值」判定。
 */
export function buildCityForecastOption(points = []) {
  const byHorizon = new Map()
  for (const point of points) {
    const item = byHorizon.get(point.horizonH) ?? {
      horizonH: point.horizonH,
      hour: point.forecastAt.slice(11, 16),
      loadKw: 0,
      idleCount: 0,
      stations: 0,
      highRisk: 0,
    }
    item.loadKw = Number((item.loadKw + point.predictedLoadKw).toFixed(1))
    item.idleCount += point.predictedIdleCount
    item.stations += 1
    if (point.congestionLevel === 'high') item.highRisk += 1
    byHorizon.set(point.horizonH, item)
  }
  const rows = [...byHorizon.values()].sort((a, b) => a.horizonH - b.horizonH)
  if (!rows.length) {
    return {
      xAxis: { type: 'category', data: [], name: '时刻' },
      yAxis: { type: 'value', name: 'kW' },
      series: [{ name: '全城预测负荷', type: 'line', data: [] }],
      meta: { hours: 0, peakHour: null, peakLoadKw: null, predictedEnergyKwh: null, warnHours: [], stations: 0 },
    }
  }
  const peak = rows.reduce((acc, r, i) => (r.loadKw > rows[acc].loadKw ? i : acc), 0)
  const threshold = rows[peak].loadKw * 0.9
  const warnHours = rows.filter((r) => r.loadKw >= threshold).map((r) => r.hour)
  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params) => {
        const row = rows[params?.[0]?.dataIndex]
        if (!row) return ''
        return `${row.hour}（第 ${row.horizonH} 小时）<br/>全城预测负荷 ${row.loadKw} kW` +
          `<br/>预测空闲合计 ${row.idleCount} 桩 · 高拥堵站 ${row.highRisk} 个`
      },
    },
    xAxis: { type: 'category', data: rows.map((r) => r.hour), name: '时刻' },
    yAxis: { type: 'value', name: 'kW' },
    series: [
      {
        name: '全城预测负荷',
        type: 'line',
        smooth: true,
        areaStyle: {},
        data: rows.map((r) => r.loadKw),
        markPoint: {
          symbolSize: 46,
          data: [{ coord: [peak, rows[peak].loadKw], name: '预测峰值', value: Math.round(rows[peak].loadKw) }],
        },
      },
    ],
    meta: {
      hours: rows.length,
      stations: rows[0].stations,
      peakHorizon: rows[peak].horizonH,
      peakHour: rows[peak].hour,
      peakLoadKw: rows[peak].loadKw,
      // 每个点是以小时为单位的平均负荷（kW），× 1h 即为该小时电量（kWh）
      predictedEnergyKwh: Number(rows.reduce((sum, r) => sum + r.loadKw, 0).toFixed(1)),
      warnHours,
    },
  }
}

/** 设施利用率公平性：各区利用率对比，识别「建而不用」 */
export function buildUtilizationFairnessOption(rows) {
  const sorted = [...rows].sort((a, b) => a.utilizationRate - b.utilizationRate)
  return {
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'value', name: '%', max: 100 },
    yAxis: { type: 'category', data: sorted.map((r) => r.district), name: '行政区' },
    series: [
      {
        name: '利用率',
        type: 'bar',
        data: sorted.map((r) => ({ value: r.utilizationRate, chargerCount: r.chargerCount, stationCount: r.stationCount })),
        markLine: {
          symbol: 'none',
          data: [
            {
              xAxis: Number((sorted.reduce((s, r) => s + r.utilizationRate, 0) / sorted.length).toFixed(1)),
              label: { formatter: '全市均值' },
            },
          ],
        },
      },
    ],
    meta: { districts: sorted.length },
  }
}
