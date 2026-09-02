// Page bootstrap + rendering lifecycle for the single-page operations dashboard.
// Task 4 scope: validate + render one snapshot; Task 5 replaces the one-shot
// fetch with the polling/degradation poller while reusing renderSnapshot.

import {
  validateSnapshot,
  formatFen,
  formatKwh,
  formatPercent,
  isForecastStale,
} from './contracts.js';
import {
  buildRevenueOption,
  buildLoadForecastOption,
  buildStatusOption,
  buildRankingOption,
  buildStationOption,
} from './models.js';

const SNAPSHOT_URL = 'runtime/dashboard_snapshot.json';

const PALETTE = ['#4fd8ff', '#4d8bff', '#ffb84d', '#ff7a7a', '#3ecf8e'];
const STATUS_COLORS = ['#3ecf8e', '#4d8bff', '#ffb84d', '#ff7a7a', '#8796b8'];
const TOOLTIP = {
  backgroundColor: '#0f1730',
  borderColor: '#263049',
  textStyle: { color: '#dbe4f5', fontSize: 12 },
};

const EVENT_TAGS = {
  'charge.start': '开始充电',
  'order.completed': '订单完成',
  'order.settled': '订单结算',
  'charger.fault': '桩故障',
  'charger.recover': '桩恢复',
  'charger.restart': '远程重启',
  'forecast.published': '预测发布',
};

let lastSnapshot = null;
let selectedStationId = null;
const charts = new Map();

function el(id) {
  return document.getElementById(id);
}

function initChart(id) {
  const node = el(id);
  if (!node || !window.echarts) return null;
  const instance = window.echarts.init(node);
  charts.set(id, instance);
  return instance;
}

function setConnection(state, label) {
  const pill = el('connection-state');
  if (!pill) return;
  pill.textContent = label;
  pill.className = `pill pill-${state}`;
}

function setGeneratedAt(snapshot) {
  const node = el('generated-at');
  if (node) node.textContent = `生成时间：${snapshot.generatedAt}`;
}

function renderKpis(snapshot) {
  const { kpis, chargerStatus } = snapshot;
  const set = (id, text) => {
    const node = el(id);
    if (node) node.textContent = text;
  };
  set('kpi-revenue', formatFen(kpis.todayRevenueFen));
  set('kpi-energy', formatKwh(kpis.todayEnergyKwh));
  set('kpi-orders', `${kpis.todayOrderCount} 单`);
  set('kpi-online', formatPercent(kpis.onlineRate));
  set('kpi-idle', `空闲桩 ${kpis.idleChargerCount} / ${chargerStatus.total}`);
}

function renderStationPicker(snapshot) {
  const select = el('station-select');
  if (!select) return;
  const previous = selectedStationId;
  select.replaceChildren();
  for (const station of snapshot.stations) {
    const option = document.createElement('option');
    option.value = String(station.stationId);
    option.textContent = station.name;
    select.appendChild(option);
  }
  const desired = previous ?? snapshot.stationRanking[0]?.stationId ?? null;
  selectedStationId = Number.isInteger(desired) && snapshot.stations.some((s) => s.stationId === desired)
    ? desired
    : (snapshot.stations[0]?.stationId ?? null);
  if (selectedStationId !== null) select.value = String(selectedStationId);
}

function applyOption(id, option) {
  const instance = charts.get(id);
  if (!instance) return;
  const color = id === 'status-chart' ? STATUS_COLORS : PALETTE;
  instance.setOption(
    {
      color,
      tooltip: TOOLTIP,
      grid: { left: 64, right: 24, top: 36, bottom: 30 },
      legend: { top: 0, left: 0, textStyle: { color: '#8796b8', fontSize: 11 } },
      ...option,
    },
    true,
  );
}

function renderCharts(snapshot) {
  applyOption('revenue-chart', buildRevenueOption(snapshot));
  applyOption('status-chart', buildStatusOption(snapshot));
  applyOption('ranking-chart', buildRankingOption(snapshot));
  const stationOption = buildStationOption(snapshot);
  const instance = charts.get('station-map');
  if (instance) {
    const color = PALETTE;
    instance.setOption(
      {
        color,
        tooltip: {
          ...TOOLTIP,
          formatter: (params) => {
            const d = params.data;
            if (!d) return '';
            return [
              `<b>${d.name}</b>`,
              `坐标：${d.longitude}，${d.latitude}`,
              `空闲桩：${d.idleCount} / 总数：${d.total}`,
              `今日营收：${formatFen(d.revenueFen)}`,
              d.forecastEnabled ? '负荷预测：可用' : '负荷预测：暂无',
            ].join('<br>');
          },
        },
        grid: { left: 64, right: 24, top: 20, bottom: 34 },
        ...stationOption,
      },
      true,
    );
    instance.off('click');
    instance.on('click', (params) => {
      const stationId = params.data && params.data.stationId;
      if (Number.isInteger(stationId)) selectStation(snapshot, stationId);
    });
  }
}

function renderLoadChart(snapshot, stationId) {
  const option = buildLoadForecastOption(snapshot, stationId);
  const note = el('load-note');
  if (note) {
    note.hidden = !option.noForecast;
    note.textContent = option.noForecast ? '该站当前不参与负荷预测，仅展示实际负荷。' : '';
  }
  applyOption('load-chart', option);
}

function renderStationDetail(snapshot, stationId) {
  const station = snapshot.stations.find((s) => s.stationId === stationId);
  const dl = el('station-detail');
  if (!dl) return;
  dl.replaceChildren();
  if (!station) return;
  const rows = [
    ['站名', station.name],
    ['经纬度', `${station.longitude}，${station.latitude}`],
    ['空闲桩', `${station.idleCount} / ${station.chargerCount}`],
    ['在线率', formatPercent(station.onlineRate)],
    ['今日营收', formatFen(station.revenueFen)],
  ];
  if (!station.forecastEnabled) rows.push(['负荷预测', '暂无预测']);
  for (const [term, detail] of rows) {
    const dt = document.createElement('dt');
    dt.textContent = term;
    const dd = document.createElement('dd');
    dd.textContent = detail;
    if (term === '负荷预测') dd.classList.add('no-forecast');
    dl.appendChild(dt);
    dl.appendChild(dd);
  }
}

function renderEvents(snapshot) {
  const list = el('event-list');
  if (!list) return;
  list.replaceChildren();
  // Always build nodes with textContent; never innerHTML from snapshot data.
  for (const event of snapshot.events) {
    const item = document.createElement('li');
    const time = document.createElement('time');
    time.textContent = `${event.createdAt.slice(5, 10)} ${event.createdAt.slice(11, 16)}`;
    const body = document.createElement('span');
    const tag = document.createElement('span');
    tag.className = 'tag';
    tag.textContent = EVENT_TAGS[event.eventType] || event.eventType;
    body.append(tag, document.createTextNode(event.message));
    item.append(time, body);
    list.appendChild(item);
  }
}

function selectStation(snapshot, stationId) {
  selectedStationId = stationId;
  const select = el('station-select');
  if (select) select.value = String(stationId);
  renderLoadChart(snapshot, stationId);
  renderStationDetail(snapshot, stationId);
}

export function renderSnapshot(snapshot) {
  lastSnapshot = snapshot;
  setConnection('live', '已连接（实时快照）');
  setGeneratedAt(snapshot);
  renderKpis(snapshot);
  renderStationPicker(snapshot);
  renderCharts(snapshot);
  renderLoadChart(snapshot, selectedStationId);
  renderStationDetail(snapshot, selectedStationId);
  renderEvents(snapshot);
  if (snapshot.forecastRun && snapshot.forecastRun.activatedAt) {
    const stale = isForecastStale(snapshot.forecastRun.activatedAt, Date.now());
    if (stale) {
      const note = el('load-note');
      if (note) {
        note.hidden = false;
        note.textContent = '预测批次已超过 2 小时，曲线可能不再代表最新状态。';
      }
    }
  }
}

async function boot() {
  initChart('revenue-chart');
  initChart('load-chart');
  initChart('status-chart');
  initChart('ranking-chart');
  initChart('station-map');

  const select = el('station-select');
  if (select) {
    select.addEventListener('change', () => {
      if (lastSnapshot) selectStation(lastSnapshot, Number(select.value));
    });
  }

  let resizeTimer = null;
  window.addEventListener('resize', () => {
    if (resizeTimer) return;
    resizeTimer = setTimeout(() => {
      resizeTimer = null;
      for (const instance of charts.values()) instance.resize();
    }, 150);
  });

  try {
    const res = await fetch(`${SNAPSHOT_URL}?ts=${Date.now()}`, { cache: 'no-store' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const snapshot = await res.json();
    validateSnapshot(snapshot);
    renderSnapshot(snapshot);
  } catch (error) {
    setConnection('error', '快照加载失败');
    const note = el('load-note');
    if (note) {
      note.hidden = false;
      note.textContent = `无法加载快照（${error.message}）。保留最后成功数据显示与轮询降级将在连接恢复后生效。`;
    }
    console.error('dashboard boot failed:', error);
  }
}

boot();
