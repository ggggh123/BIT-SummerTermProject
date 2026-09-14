// REST 响应 → dashboard/assets/models.js 期望的视图模型（字段名与第一阶段快照保持一致）
// 这样 5 个图表构造器一行都不用改，是本阶段"零重复"复用的关键一层。
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
      stationId: s.stationId,
      name: s.name,
      longitude: s.longitude,
      latitude: s.latitude,
      chargerCount: s.chargerCount ?? 0,
      idleCount: s.idleCount ?? 0,
      utilizationRate: s.utilizationRate ?? 0,
      revenueFen: s.revenueFen ?? 0,
      forecastEnabled: Boolean(s.forecastEnabled),
    })),
    chargerStatus,
    stationRanking: ranking.map((r) => ({
      stationId: r.stationId,
      name: r.name,
      utilizationRate: r.utilizationRate ?? 0,
      revenueFen: r.revenueFen ?? 0,
      idleCount: r.idleCount ?? 0,
      chargerCount: r.chargerCount ?? 0,
      orderCount: r.orderCount ?? 0,
    })),
    revenue7d: revenueTrend.map((p) => ({ date: p.date, revenueFen: p.revenueFen ?? 0 })),
    actualLoad24h: load24h.map((p) => ({
      stationId: p.stationId,
      observedAt: p.observedAt,
      loadKw: p.loadKw ?? 0,
    })),
    forecast24h: forecast24h.map((p) => ({
      stationId: p.stationId,
      forecastAt: p.forecastAt,
      horizonH: p.horizonH,
      predictedLoadKw: p.predictedLoadKw ?? 0,
      predictedIdleCount: p.predictedIdleCount ?? 0,
      congestionLevel: p.congestionLevel ?? 'low',
      isPeak: Boolean(p.isPeak),
    })),
    events,
    quality,
  }
}

/** 按站点聚合 24h 平均负荷，供主页负荷曲线（无站点选择时用全站合计） */
export function stationsWithForecast(view) {
  return view.stations.filter((s) => s.forecastEnabled)
}
