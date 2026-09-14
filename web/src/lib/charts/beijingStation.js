// 主页站点分布：北京行政区底图 + 站点散点（纯函数，零依赖，便于 Node 测试）
export const BEIJING_MAP = 'beijing'

export function buildBeijingStationOption(view) {
  const stations = view?.stations ?? []
  return {
    tooltip: {
      formatter: (p) =>
        `${p.data.name}<br/>桩数 ${p.data.chargerCount}｜空闲 ${p.data.idleCount}<br/>利用率 ${p.data.utilizationRate}%<br/>累计营收 ${(p.data.revenueFen / 100).toLocaleString('zh-CN')} 元`,
    },
    geo: {
      map: BEIJING_MAP,
      roam: true,
      label: { show: true, fontSize: 9, color: '#8ba1c4' },
      itemStyle: { areaColor: '#0d1628', borderColor: '#1b2a48' },
      emphasis: { itemStyle: { areaColor: '#16233c' }, label: { color: '#e6f0ff' } },
    },
    series: [
      {
        name: '站点分布',
        type: 'scatter',
        coordinateSystem: 'geo',
        data: stations.map((s) => ({
          name: s.name,
          value: [s.longitude, s.latitude],
          stationId: s.stationId,
          chargerCount: s.chargerCount,
          idleCount: s.idleCount,
          utilizationRate: s.utilizationRate,
          revenueFen: s.revenueFen,
          symbolSize: Math.max(9, Math.min(30, s.chargerCount / 1.5)),
        })),
        emphasis: { scale: 1.3 },
      },
    ],
    meta: { stationCount: stations.length, map: BEIJING_MAP },
  }
}
