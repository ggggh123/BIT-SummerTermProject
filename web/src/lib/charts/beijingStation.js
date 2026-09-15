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
      // 必须显式给布局：只写 left:'center' 时 ECharts 会把 geo 盒子压到最小默认尺寸，
      // 实测底图仅占面板中间一小块、8 个站点重叠成一团（点击也无法区分站点）。
      layoutCenter: ['50%', '52%'],
      layoutSize: '94%',
      scaleLimit: { min: 0.8, max: 6 },
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
          // 点大小=桩数；主页地图面板只有 300px 高，底图按宽高比只能占约 260px 宽，
          // 点太大会互相重叠（8 站挤成一团、无法点击区分），故上限压到 16px。
          symbolSize: Math.max(10, Math.min(16, s.chargerCount / 2.2)),
        })),
        emphasis: { scale: 1.3 },
      },
    ],
    meta: { stationCount: stations.length, map: BEIJING_MAP },
  }
}
