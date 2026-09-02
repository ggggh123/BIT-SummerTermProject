// Deterministic ECharts data models built from one validated snapshot.
// Pure functions: no DOM access and no ECharts instance creation.
// Consumed by the page renderer in Task 4 and by Node tests here.

const CHARGER_STATE_ORDER = ['idle', 'reserved', 'charging', 'fault', 'restarting'];
const CHARGER_STATE_NAMES = {
  idle: '空闲',
  reserved: '已预约',
  charging: '充电中',
  fault: '故障',
  restarting: '重启中',
};

function shortDate(iso) {
  return iso.slice(5, 10); // YYYY-MM-DD -> MM-DD
}

function shortTime(iso) {
  return `${iso.slice(5, 10)} ${iso.slice(11, 16)}`; // -> "MM-DD HH:mm"
}

function sortByKey(rows, key) {
  return [...rows].sort((a, b) => (a[key] < b[key] ? -1 : a[key] > b[key] ? 1 : 0));
}

export function buildRevenueOption(snapshot) {
  const labels = snapshot.revenue7d.map((row) => shortDate(row.date));
  const amounts = snapshot.revenue7d.map((row) => row.revenueFen / 100); // fen -> yuan
  return {
    xAxis: { type: 'category', data: labels, name: '日期' },
    yAxis: { type: 'value', name: '元' },
    series: [{ name: '近7日营收', type: 'bar', unit: '元', data: amounts }],
  };
}

export function buildLoadForecastOption(snapshot, stationId) {
  const station = snapshot.stations.find((s) => s.stationId === stationId) || null;
  const stationName = station ? station.name : '';
  const actual = sortByKey(
    snapshot.actualLoad24h.filter((row) => row.stationId === stationId),
    'observedAt',
  );
  const forecast = sortByKey(
    snapshot.forecast24h.filter((row) => row.stationId === stationId),
    'forecastAt',
  );
  const enabled = Boolean(station && station.forecastEnabled && forecast.length > 0);

  if (!enabled) {
    return {
      stationId,
      stationName,
      noForecast: true,
      message: '暂无预测',
      xAxis: { type: 'category', data: actual.map((row) => shortTime(row.observedAt)), name: '时间' },
      yAxis: { type: 'value', name: 'kW' },
      series: [
        {
          name: '实际负荷',
          type: 'line',
          unit: 'kW',
          data: actual.map((row) => row.loadKw),
        },
      ],
    };
  }

  const null24 = Array(24).fill(null);
  const labels = [
    ...actual.map((row) => shortTime(row.observedAt)),
    ...forecast.map((row) => shortTime(row.forecastAt)),
  ];
  const peakData = forecast
    .filter((row) => row.isPeak)
    .map((row) => ({ coord: [24 + forecast.indexOf(row), row.predictedLoadKw], name: '高峰' }));

  return {
    stationId,
    stationName,
    noForecast: false,
    xAxis: { type: 'category', data: labels, name: '时间' },
    yAxis: { type: 'value', name: 'kW' },
    series: [
      {
        name: '实际负荷',
        type: 'line',
        unit: 'kW',
        data: [...actual.map((row) => row.loadKw), ...null24],
      },
      {
        name: '预测负荷',
        type: 'line',
        unit: 'kW',
        data: [...null24, ...forecast.map((row) => row.predictedLoadKw)],
        markPoint: peakData.length > 0 ? { data: peakData } : undefined,
      },
    ],
  };
}

export function buildStatusOption(snapshot) {
  const status = snapshot.chargerStatus;
  return {
    series: [
      {
        name: '桩状态',
        type: 'pie',
        unit: '个',
        data: CHARGER_STATE_ORDER.map((state) => ({
          name: CHARGER_STATE_NAMES[state],
          value: status[state],
        })),
      },
    ],
  };
}

export function buildRankingOption(snapshot) {
  const rows = [...snapshot.stationRanking].sort(
    (a, b) => b.utilizationRate - a.utilizationRate || b.revenueFen - a.revenueFen,
  );
  return {
    xAxis: { type: 'value', name: '%' },
    yAxis: { type: 'category', data: rows.map((row) => row.name), name: '站点' },
    series: [
      {
        name: '利用率',
        type: 'bar',
        unit: '%',
        data: rows.map((row) => ({
          name: row.name,
          value: row.utilizationRate,
          idleCount: row.idleCount,
          chargerCount: row.chargerCount,
          revenueFen: row.revenueFen,
        })),
      },
    ],
  };
}

export function buildStationOption(snapshot) {
  return {
    xAxis: { type: 'value', name: '经度' },
    yAxis: { type: 'value', name: '纬度' },
    series: [
      {
        name: '站点分布',
        type: 'scatter',
        data: snapshot.stations.map((row) => ({
          name: row.name,
          value: [row.longitude, row.latitude],
          longitude: row.longitude,
          latitude: row.latitude,
          stationId: row.stationId,
          idleCount: row.idleCount,
          total: row.chargerCount,
          revenueFen: row.revenueFen,
          forecastEnabled: row.forecastEnabled,
        })),
      },
    ],
  };
}
