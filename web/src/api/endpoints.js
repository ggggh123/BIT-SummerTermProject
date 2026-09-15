// 第二阶段接口清单（与 #2 TL 冻结；字段口径：金额整数分、时间 +08:00 ISO 8601）
// 来源：桌面 Part2\02-TL-Hadoop平台与Flask后端设计.md §3.3
export const ENDPOINTS = {
  home: {
    kpis: ['overview/kpis'],
    stations: ['overview/stations'],
    chargerStatus: ['overview/charger-status'],
    load24h: ['overview/load-24h'],
    events: ['overview/events'],
    revenueTrend: ['enterprise/revenue-trend', { days: 7 }],
    ranking: ['enterprise/station-ranking'],
    forecast24h: ['forecast/24h'],
    quality: ['quality/summary'],
  },
  user: {
    priceCompare: ['user/price-compare'],
    priceDistance: ['user/price-distance'],
    idleRanking: ['user/idle-ranking'],
    peakHeatmap: ['user/peak-heatmap'],
  },
  station: {
    coverage: ['station/coverage'],
    // station/{id}/utilization | mix | health 由页面按选中站点动态拼接路径
  },
  enterprise: {
    revenueTrend: ['enterprise/revenue-trend', { days: 30 }],
    stationRanking: ['enterprise/station-ranking'],
    userGrowth: ['enterprise/user-growth'],
    userRfm: ['enterprise/user-rfm'],
    monthly: ['enterprise/monthly'],
  },
  gov: {
    coverage: ['gov/coverage'],
    serviceStats: ['gov/service-stats'],
    carbon: ['gov/carbon'],
    peakLoad: ['gov/peak-load'],
    utilization: ['gov/utilization'],
  },
}
