// 充电站视角图表构造器（「站点分布与配置是否合理」）

/** 单站利用率时段曲线（接口返回的就是 {stationId,name,points} 这一层） */
export function buildUtilizationOption(util) {
  const points = [...util.points].sort((a, b) => (a.observedAt < b.observedAt ? -1 : 1))
  return {
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'category', data: points.map((p) => p.observedAt.slice(11, 16)), name: '时刻' },
    yAxis: { type: 'value', name: '%', max: 100 },
    series: [
      { name: '利用率', type: 'line', smooth: true, areaStyle: {}, data: points.map((p) => p.utilizationRate) },
    ],
    meta: { stationId: util.stationId, name: util.name, points: points.length },
  }
}

/** 快慢充结构（饼图），并给出快充订单占比 */
export function buildMixOption(mix) {
  return {
    tooltip: { trigger: 'item' },
    legend: { bottom: 0 },
    series: [
      {
        name: '桩结构',
        type: 'pie',
        radius: ['40%', '68%'],
        data: [
          { name: `快充 ${mix.fastPowerKw}kW`, value: mix.fastCount },
          { name: `慢充 ${mix.slowPowerKw}kW`, value: mix.slowCount },
        ],
      },
    ],
    meta: { total: mix.fastCount + mix.slowCount, fastOrderShare: mix.fastOrderShare },
  }
}

/** 设备健康：Top 桩累计充电次数（附故障率） */
export function buildHealthOption(health) {
  const rows = [...health.topChargers].sort((a, b) => b.chargeCount - a.chargeCount)
  return {
    tooltip: {
      trigger: 'axis',
      formatter: (ps) => {
        const r = rows.find((x) => x.code === ps[0].name)
        return `${r.code}<br/>累计充电 ${r.chargeCount} 次<br/>累计时长 ${(r.totalDurationSec / 3600).toFixed(1)} h<br/>${r.faultFlag ? '存在故障记录' : '无故障记录'}`
      },
    },
    xAxis: { type: 'value', name: '次' },
    yAxis: { type: 'category', data: rows.map((r) => r.code), inverse: true },
    series: [{ name: '累计充电次数', type: 'bar', data: rows.map((r) => r.chargeCount) }],
    meta: { faultRate: health.faultRate, faultCount: health.faultCount, chargers: rows.length },
  }
}

// 拥堵等级色标：等级取自接口 ads_forecast_24h.congestion_level，前端只着色、不重算
const CONGESTION_COLOR = { low: '#65dec0', medium: '#d8b27a', high: '#e78d76' }
const CONGESTION_LABEL = { low: '低拥堵', medium: '中拥堵', high: '高拥堵' }

/**
 * 单站未来 1–24h 预测负荷曲线（点色=预测拥堵等级），
 * 并叠加由快慢充结构推出的站点额定容量参考线：接近容量即为预测满载风险。
 */
export function buildStationForecastOption(points = [], mix = null) {
  const rows = [...points].sort((a, b) => a.horizonH - b.horizonH)
  if (!rows.length) {
    return {
      xAxis: { type: 'category', data: [], name: '时刻' },
      yAxis: { type: 'value', name: 'kW' },
      series: [{ name: '未来预测负荷', type: 'line', data: [] }],
      meta: { points: 0, peakHorizon: null, peakHour: null, peakLoadKw: null, ratedPowerKw: null, noForecast: true },
    }
  }
  const peak = rows.reduce((acc, r, i) => (r.predictedLoadKw > rows[acc].predictedLoadKw ? i : acc), 0)
  const rated = mix ? mix.fastCount * mix.fastPowerKw + mix.slowCount * mix.slowPowerKw : null
  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params) => {
        const row = rows[params?.[0]?.dataIndex]
        if (!row) return ''
        const level = CONGESTION_LABEL[row.congestionLevel] ?? row.congestionLevel
        return `${row.forecastAt.slice(11, 16)}（第 ${row.horizonH} 小时）<br/>预测负荷 ${row.predictedLoadKw} kW` +
          `<br/>预测空闲 ${row.predictedIdleCount} 桩 · ${level}`
      },
    },
    xAxis: { type: 'category', data: rows.map((r) => r.forecastAt.slice(11, 16)), name: '时刻' },
    yAxis: { type: 'value', name: 'kW' },
    series: [
      {
        name: '未来预测负荷',
        type: 'line',
        smooth: true,
        areaStyle: {},
        data: rows.map((r) => ({
          value: r.predictedLoadKw,
          itemStyle: { color: CONGESTION_COLOR[r.congestionLevel] ?? '#78acd5' },
        })),
        markPoint: {
          symbolSize: 46,
          data: [{ coord: [peak, rows[peak].predictedLoadKw], name: '峰值', value: Math.round(rows[peak].predictedLoadKw) }],
        },
        ...(rated
          ? {
              markLine: {
                symbol: 'none',
                data: [{ yAxis: rated, label: { formatter: `额定容量 ${Math.round(rated)} kW` } }],
              },
            }
          : {}),
      },
    ],
    meta: {
      points: rows.length,
      peakHorizon: rows[peak].horizonH,
      peakHour: rows[peak].forecastAt.slice(11, 16),
      peakLoadKw: rows[peak].predictedLoadKw,
      ratedPowerKw: rated,
      noForecast: false,
    },
  }
}

/** 区域覆盖与选址：站点分布散点（点大小=桩数，悬停看服务半径） */
export function buildCoverageOption(rows) {
  return {
    tooltip: {
      formatter: (p) =>
        `${p.data.name}（${p.data.district}）<br/>桩数 ${p.data.chargerCount}<br/>服务半径约 ${p.data.radius} km`,
    },
    xAxis: { type: 'value', name: '经度', min: 'dataMin', max: 'dataMax' },
    yAxis: { type: 'value', name: '纬度', min: 'dataMin', max: 'dataMax' },
    series: [
      {
        name: '站点覆盖',
        type: 'scatter',
        data: rows.map((r) => ({
          name: r.name,
          district: r.district,
          chargerCount: r.chargerCount,
          radius: r.serviceRadiusKm,
          value: [r.longitude, r.latitude],
          symbolSize: Math.max(10, Math.min(34, r.chargerCount / 1.6)),
        })),
      },
    ],
    meta: { stationCount: rows.length },
  }
}
