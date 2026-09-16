// 北京行政区离线底图 + 真实 ADS 站点坐标；不生成装饰性假站点。
import { hasCoordinates, coordinate, escapeHtml } from '../coordinates.js'
export const BEIJING_MAP = 'beijing'

export function buildBeijingStationOption(view, selectedId = null) {
  const stations = view?.stations ?? []
  const located = stations.filter(hasCoordinates)
  return {
    tooltip: {
      trigger: 'item',
      formatter: (p) => p.data?.stationId === undefined ? escapeHtml(p.name) :
        `<b>${escapeHtml(p.data.name)}</b><br/>设备 ${p.data.chargerCount} 台 · 空闲 ${p.data.idleCount} 台<br/>利用率 ${p.data.utilizationRate}%<br/>累计营收 ${(p.data.revenueFen / 100).toLocaleString('zh-CN')} 元${p.data.serviceRadiusKm != null ? `<br/>服务半径约 ${p.data.serviceRadiusKm} km` : ''}<br/><span style="color:#65dec0">点击查看站点 →</span>`,
    },
    geo: {
      map: BEIJING_MAP, roam: true,
      layoutCenter: ['53%', '51%'], layoutSize: '108%', scaleLimit: { min: .8, max: 6 },
      label: { show: true, fontSize: 10, color: '#769b96' },
      itemStyle: { areaColor: '#163639', borderColor: '#47746d', borderWidth: .8, shadowBlur: 15, shadowColor: '#0006', shadowOffsetY: 4 },
      emphasis: { itemStyle: { areaColor: '#26534c' }, label: { color: '#c1e6d5', fontSize: 11 } },
      select: { disabled: true },
    },
    series: [{
      name: '站点分布', type: 'scatter', coordinateSystem: 'geo', z: 3,
      data: located.map((s) => ({
        name: s.name, value: [coordinate(s.longitude, 180), coordinate(s.latitude, 90)],
        stationId: s.stationId, chargerCount: s.chargerCount, idleCount: s.idleCount,
        utilizationRate: s.utilizationRate, revenueFen: s.revenueFen,
        ...(s.serviceRadiusKm != null ? { serviceRadiusKm: s.serviceRadiusKm } : {}),
        symbolSize: Math.max(9, Math.min(15, s.chargerCount / 3)),
        itemStyle: { color: selectedId === s.stationId ? '#f0c486' : '#79ecd0', borderColor: '#a4ffe3', borderWidth: 1.5, shadowBlur: 12, shadowColor: '#65dec0aa' },
      })),
      label: { show: true, formatter: p => p.name.replace(/充电站$/, ''), position: 'right', distance: 6, color: '#c4e7da', fontSize: 11, backgroundColor: '#102930bc', padding: [3, 5], borderRadius: 2 },
      labelLayout: { hideOverlap: true }, emphasis: { scale: 1.7, label: { show: true, fontSize: 12 } },
    }],
    graphic: [
      { type: 'text', right: 24, top: 16, style: { text: 'N', fill: '#8db6a8', font: '10px monospace' } },
      { type: 'polyline', right: 23, top: 32, shape: { points: [[0, 17], [4, 0], [8, 17], [4, 12], [0, 17]] }, style: { fill: '#579987', stroke: '#82b8a3', lineWidth: 1 } },
    ],
    meta: { stationCount: stations.length, mappedCount: located.length, unlocatedCount: stations.length - located.length, map: BEIJING_MAP },
  }
}
