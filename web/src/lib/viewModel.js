// REST 响应 → dashboard/assets/models.js 期望的视图模型（字段名与第一阶段快照保持一致）
// 这样 5 个图表构造器一行都不用改，是本阶段"零重复"复用的关键一层。
// 后端（CSV → Spark → SQLite → JSON）常把数字/布尔返回成字符串，
// 这里统一转型：契约要求金额是整数分、比例是 0–100 数值、开关是布尔。
const num = (v, fallback = 0) => {
  const n = Number(v)
  return Number.isFinite(n) ? n : fallback
}
const bool = (v) => v === true || v === 1 || v === '1' || v === 'true'

export function buildHomeViewModel(payload) {
  const {
    kpis = {},
    stations = [],
    chargerStatus = {},
    ranking = [],
    revenueTrend = [],
    load24h = [],
    forecast24h = [],
    events = [],
    quality = null,
  } = payload

  return {
    kpis,
    stations: stations.map((s) => ({
      // 站点 id 若为字符串，选择器与图表按站点过滤会静默失配，故强制转数字
      stationId: num(s.stationId),
      name: s.name,
      longitude: num(s.longitude),
      latitude: num(s.latitude),
      chargerCount: num(s.chargerCount),
      idleCount: num(s.idleCount),
      utilizationRate: num(s.utilizationRate),
      revenueFen: num(s.revenueFen),
      forecastEnabled: bool(s.forecastEnabled),
    })),
    chargerStatus,
    stationRanking: ranking.map((r) => ({
      stationId: num(r.stationId),
      name: r.name,
      utilizationRate: num(r.utilizationRate),
      revenueFen: num(r.revenueFen),
      idleCount: num(r.idleCount),
      chargerCount: num(r.chargerCount),
      orderCount: num(r.orderCount),
    })),
    revenue7d: revenueTrend.map((p) => ({
      date: p.date,
      revenueFen: num(p.revenueFen),
      orderCount: num(p.orderCount),
      energyKwh: num(p.energyKwh),
    })),
    actualLoad24h: load24h.map((p) => ({
      stationId: num(p.stationId),
      observedAt: p.observedAt,
      loadKw: num(p.loadKw),
    })),
    forecast24h: forecast24h.map((p) => ({
      stationId: num(p.stationId),
      forecastAt: p.forecastAt,
      horizonH: num(p.horizonH),
      predictedLoadKw: num(p.predictedLoadKw),
      predictedIdleCount: num(p.predictedIdleCount),
      congestionLevel: p.congestionLevel ?? 'low',
      isPeak: bool(p.isPeak),
    })),
    events,
    quality,
  }
}

/** 按站点聚合 24h 平均负荷，供主页负荷曲线（无站点选择时用全站合计） */
export function stationsWithForecast(view) {
  return view.stations.filter((s) => s.forecastEnabled)
}
