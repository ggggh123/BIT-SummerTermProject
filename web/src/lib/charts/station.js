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
const FULL_LOAD_RATIO = 0.9

/**
 * 单站未来 1–24h **桩位占用**预测：占用/空闲堆叠柱（占用按预测拥堵等级着色），
 * 并给出最忙 / 最空闲时段、利用率 ≥90% 的满载风险小时与折算预测电量。
 *
 * 与政府视角「全城负荷曲线（kW）」刻意不同口径：这里回答运营者的问题——
 * 「本站什么时候会排队、什么时候有富余」，不重复展示城市级负荷。
 */
export function buildStationOccupancyOption(points = [], chargerCount = null) {
  const rows = [...points].sort((a, b) => a.horizonH - b.horizonH)
  const hour = (row) => row.forecastAt.slice(11, 16)
  if (!rows.length) {
    return {
      xAxis: { type: 'category', data: [], name: '时刻' },
      yAxis: { type: 'value', name: '桩（个）' },
      series: [{ name: '预测占用桩', type: 'bar', data: [] }],
      meta: {
        points: 0, noForecast: true, chargerCount: chargerCount ?? null,
        busiestHour: null, busiestOccupied: null, busiestUtilization: null,
        idlestHour: null, idlestIdle: null, fullLoadHours: [], predictedEnergyKwh: null,
      },
    }
  }
  const occupied = (row) => Number(row.predictedBusyCount) || 0
  const idleCount = (row) => Number(row.predictedIdleCount) || 0
  const utilization = (row) =>
    chargerCount ? Number(((occupied(row) / chargerCount) * 100).toFixed(1)) : null
  const busiest = rows.reduce((acc, row, i) => (occupied(row) > occupied(rows[acc]) ? i : acc), 0)
  const idlest = rows.reduce((acc, row, i) => (idleCount(row) > idleCount(rows[acc]) ? i : acc), 0)
  const fullLoad = rows.filter((row) => chargerCount && occupied(row) / chargerCount >= FULL_LOAD_RATIO)
  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      formatter: (params) => {
        const row = rows[params?.[0]?.dataIndex]
        if (!row) return ''
        const level = CONGESTION_LABEL[row.congestionLevel] ?? row.congestionLevel
        const rate = utilization(row)
        return `${hour(row)}（第 ${row.horizonH} 小时）<br/>预测占用 ${occupied(row)} 桩 · 空闲 ${idleCount(row)} 桩` +
          `<br/>拥堵等级 ${level}${rate != null ? ` · 利用率 ${rate}%` : ''}`
      },
    },
    legend: { data: ['预测占用桩', '预测空闲桩'], right: 0, top: 0 },
    grid: { left: 48, right: 20, top: 34, bottom: 26 },
    xAxis: { type: 'category', data: rows.map(hour), name: '时刻' },
    yAxis: { type: 'value', name: '桩（个）' },
    series: [
      {
        name: '预测占用桩',
        type: 'bar',
        stack: 'stalls',
        barMaxWidth: 14,
        data: rows.map((row) => ({
          value: occupied(row),
          itemStyle: { color: CONGESTION_COLOR[row.congestionLevel] ?? '#78acd5' },
        })),
        markPoint: {
          symbolSize: 42,
          data: [{ coord: [busiest, occupied(rows[busiest])], name: '最忙', value: occupied(rows[busiest]) }],
        },
      },
      {
        name: '预测空闲桩',
        type: 'bar',
        stack: 'stalls',
        barMaxWidth: 14,
        itemStyle: { color: '#2f4f5c' },
        data: rows.map(idleCount),
      },
    ],
    meta: {
      points: rows.length,
      noForecast: false,
      chargerCount: chargerCount ?? null,
      busiestHour: hour(rows[busiest]),
      busiestOccupied: occupied(rows[busiest]),
      busiestUtilization: utilization(rows[busiest]),
      idlestHour: hour(rows[idlest]),
      idlestIdle: idleCount(rows[idlest]),
      fullLoadHours: fullLoad.map(hour),
      // 每个点是以小时为单位的平均负荷（kW），× 1h 即为该小时电量（kWh）
      predictedEnergyKwh: Number(rows.reduce((sum, row) => sum + (Number(row.predictedLoadKw) || 0), 0).toFixed(1)),
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
