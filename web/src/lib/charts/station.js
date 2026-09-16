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
