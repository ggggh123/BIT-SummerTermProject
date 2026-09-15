// 北京行政区 GeoJSON 的注册（离线内置，不走 CDN；《01》§5.5）
import * as echarts from 'echarts'
import { BEIJING_MAP } from '@/lib/charts/beijingStation'

let pending = null

/** 首次调用时拉取并注册地图，之后复用同一 Promise */
export function loadBeijingMap() {
  if (!pending) {
    const url = `${import.meta.env.BASE_URL}geo/beijing.json`
    pending = fetch(url)
      .then((res) => {
        if (!res.ok) throw new Error(`GeoJSON 加载失败 HTTP ${res.status}`)
        return res.json()
      })
      .then((geo) => {
        echarts.registerMap(BEIJING_MAP, geo)
        return { name: BEIJING_MAP, districts: geo.features?.length ?? 0 }
      })
      .catch((err) => {
        pending = null // 允许下次重试
        throw err
      })
  }
  return pending
}
