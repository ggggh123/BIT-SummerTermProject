// 用户视角图表构造器（「去哪充电划算、不排队」）

/** 各站电价对比（元/度），叠加全市均价参考线 */
export function buildPriceCompareOption(data) {
  const rows = [...data.stations].sort((a, b) => a.priceFenPerKwh - b.priceFenPerKwh)
  const avg = data.cityAvgFenPerKwh / 100
  return {
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'category', data: rows.map((r) => r.name), axisLabel: { rotate: 30, fontSize: 10 } },
    yAxis: { type: 'value', name: '元/度' },
    series: [
      {
        name: '电价',
        type: 'bar',
        data: rows.map((r) => ({ value: r.priceFenPerKwh / 100, stationId: r.stationId })),
        markLine: {
          symbol: 'none',
          data: [{ yAxis: avg, label: { formatter: `全市均价 ${avg.toFixed(2)}` } }],
        },
      },
    ],
    meta: { cityAvgYuan: avg, stationCount: rows.length },
  }
}

/** 距离-价格散点：以天安门为参考点，找「近且便宜」的站（点大小=空闲桩数） */
export function buildPriceDistanceOption(rows) {
  const sorted = rows.filter(r => r.distanceKm != null && Number.isFinite(Number(r.distanceKm))).sort((a, b) => a.distanceKm - b.distanceKm)
  return {
    tooltip: {
      formatter: (p) =>
        `${p.data.name}<br/>距离 ${p.data.distanceKm} km<br/>电价 ${(p.data.value[1]).toFixed(2)} 元/度<br/>空闲桩 ${p.data.idleCount}`,
    },
    xAxis: { type: 'value', name: '距市中心 (km)' },
    yAxis: { type: 'value', name: '元/度' },
    series: [
      {
        name: '站点',
        type: 'scatter',
        data: sorted.map((r) => ({
          name: r.name,
          value: [r.distanceKm, r.priceFenPerKwh / 100],
          distanceKm: r.distanceKm,
          idleCount: r.idleCount,
          stationId: r.stationId,
          symbolSize: Math.max(8, Math.min(28, r.idleCount)),
        })),
      },
    ],
    meta: { stationCount: sorted.length, unlocatedCount: rows.length - sorted.length },
  }
}

/** 各站当前空闲桩排行（横向条形） */
export function buildIdleRankingOption(rows) {
  const sorted = [...rows].sort((a, b) => a.idleCount - b.idleCount) // 横向条从下往上
  return {
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    xAxis: { type: 'value', name: '个' },
    yAxis: { type: 'category', data: sorted.map((r) => r.name), name: '站点' },
    series: [
      {
        name: '空闲桩',
        type: 'bar',
        data: sorted.map((r) => ({ value: r.idleCount, idleRate: r.idleRate, chargerCount: r.chargerCount })),
      },
    ],
    meta: { stationCount: sorted.length },
  }
}

/** 站点 × 24h 繁忙热力图（引导错峰） */
export function buildPeakHeatmapOption(data) {
  return {
    tooltip: { formatter: (p) => `${data.names[p.value[1]]}<br/>${data.hours[p.value[0]]} 占用 ${p.value[2]} 桩` },
    grid: { left: 90, bottom: 40, top: 12 },
    xAxis: { type: 'category', data: data.hours, splitArea: { show: true } },
    yAxis: { type: 'category', data: data.names, splitArea: { show: true } },
    visualMap: { min: 0, max: Math.max(1, ...data.values.map((v) => v[2])), calculable: true, orient: 'vertical', right: 0, top: 'center' },
    series: [{ name: '占用桩数', type: 'heatmap', data: data.values, label: { show: false } }],
    meta: { cells: data.values.length },
  }
}
