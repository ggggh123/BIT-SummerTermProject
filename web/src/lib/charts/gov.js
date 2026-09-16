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
