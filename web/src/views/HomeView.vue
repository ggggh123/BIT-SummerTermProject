<script setup>
import { computed, onMounted, onBeforeUnmount, ref } from 'vue'
import { useRouter } from 'vue-router'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import ScaleFrame from '@/components/ScaleFrame.vue'
import MiniTrend from '@/components/MiniTrend.vue'
import QualityPanel from '@/components/QualityPanel.vue'
import { ScrollBoard } from '@kjgl77/datav-vue3'
import { fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import { buildHomeViewModel } from '@/lib/viewModel'
import { loadBeijingMap } from '@/lib/beijingMap'
import { buildBeijingStationOption } from '@/lib/charts/beijingStation'
import { hasCoordinates, escapeHtml } from '@/lib/coordinates'
import { buildRevenueTrendOption30d } from '@/lib/charts/overview'
import { formatForecastSource } from '@/lib/contracts'
import { buildLoadForecastOption, buildRankingOption, buildStationOption, buildStatusOption } from '@/lib/models'

const view = ref(null), fast = ref(null), slow = ref(null)
const fastError = ref(''), slowError = ref('')
const selectedStation = ref(null), mapReady = ref(false), mapFailed = ref(false)
const router = useRouter()
function rebuild() {
  if (!fast.value) return
  view.value = buildHomeViewModel({
    ...fast.value,
    ranking: slow.value?.ranking ?? [],
    revenueTrend: (slow.value?.revenueTrend?.points ?? []).slice(-30),
    load24h: slow.value?.load24h?.points ?? [],
    forecast24h: slow.value?.forecast24h?.points ?? [],
    quality: slow.value?.quality ?? null,
  })
  if (!view.value.stations.some(s => s.stationId === selectedStation.value)) selectedStation.value = view.value.stations[0]?.stationId ?? null
}
async function loadFast() {
  try {
    fast.value = await fetchGroup({
      kpis: ENDPOINTS.home.kpis, stations: ENDPOINTS.home.stations,
      chargerStatus: ENDPOINTS.home.chargerStatus, events: ENDPOINTS.home.events,
    })
    fastError.value = ''
  } catch (e) { fastError.value = e?.message ?? String(e) }
  rebuild()
}
async function loadSlow() {
  try {
    slow.value = await fetchGroup({
      revenueTrend: ENDPOINTS.home.revenueTrend, ranking: ENDPOINTS.home.ranking,
      load24h: ENDPOINTS.home.load24h, forecast24h: ENDPOINTS.home.forecast24h, quality: ENDPOINTS.home.quality,
    })
    slowError.value = ''
  } catch (e) { slowError.value = e?.message ?? String(e) }
  rebuild()
}
let stopFast, stopSlow
onMounted(() => {
  stopFast = startPolling(loadFast, 5000)
  stopSlow = startPolling(loadSlow, 60000)
  loadBeijingMap().then(() => { mapReady.value = true }).catch(() => { mapFailed.value = true })
})
onBeforeUnmount(() => { stopFast?.(); stopSlow?.() })

const kpis = computed(() => view.value?.kpis ?? {})
const money = (n) => n == null ? '—' : (Number(n) / 100).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
const number = (n, digits = 0) => n == null ? '—' : Number(n).toLocaleString('zh-CN', { maximumFractionDigits: digits })
const recent = computed(() => view.value?.revenue7d ?? [])
const revenueOption = computed(() => view.value ? buildRevenueTrendOption30d(view.value) : {})
const statusOption = computed(() => view.value ? { ...buildStatusOption(view.value), color: ['#65dec0', '#d8b27a', '#78acd5', '#e78d76', '#a792cd'] } : {})
const rankingOption = computed(() => {
  if (!view.value) return {}
  const result = buildRankingOption(view.value)
  result.yAxis.inverse = true
  result.yAxis.name = ''
  result.xAxis.max = 100
  result.series[0].label = { show: true, position: 'right', color: '#91b5b1', fontSize: 10, formatter: p => p.value + '%' }
  return result
})
const stations = computed(() => view.value?.stations ?? [])
const forecastSource = computed(() => formatForecastSource(slow.value?.forecast24h))
const stationOption = computed(() => {
  if (!view.value) return {}
  return mapReady.value ? buildBeijingStationOption(view.value) : buildStationOption({ ...view.value, stations: stations.value.filter(hasCoordinates) })
})
function onStationClick(params) {
  if (params?.data?.stationId != null) router.push({ path: '/station', query: { station: params.data.stationId } })
}
const loadOption = computed(() => {
  if (!view.value || selectedStation.value == null) return {}
  const result = buildLoadForecastOption(view.value, selectedStation.value)
  result.color = ['#78acd5', '#65dec0']
  result.legend = { data: result.series.map(s => s.name), right: 0 }
  result.grid = { top: 32 }
  result.series = result.series.map((s, i) => ({ ...s, areaStyle: {}, lineStyle: { type: i === 1 ? 'dashed' : 'solid' } }))
  return result
})
const eventBoard = computed(() => ({
  header: ['时间', '事件记录'],
  data: (view.value?.events ?? []).slice(0, 30).map(e => [escapeHtml(e.createdAt?.slice(11, 16)), escapeHtml(e.message)]),
  rowNum: 4, headerBGC: '#173039', oddRowBGC: '#102730', evenRowBGC: '#0d222b',
  headerHeight: 26, columnWidth: [66], align: ['center', 'left'], waitTime: 4500,
}))
</script>

<template>
  <p v-if="fastError || slowError" class="error-banner" role="status">数据加载异常：{{ fastError || slowError }}。保留上一次成功数据。</p>
  <ScaleFrame :zoom="1.06">
    <div class="command-heading"><h2><span>01 / OVERVIEW</span>全域能源态势</h2><span class="brief">从城市分布到站点负荷 · 一屏掌握充电网络</span></div>
    <section class="grid kpi" aria-label="核心指标">
      <div class="panel kpi-card"><span class="kpi-index">REVENUE / CNY</span><h2 class="kpi-label">累计营收</h2><p class="kpi-value">{{ money(kpis.totalRevenueFen) }}<span class="unit">元</span></p><p class="kpi-hint">全部站点 · 累计完成订单</p><MiniTrend :values="recent.map(r => r.revenueFen)" /></div>
      <div class="panel kpi-card"><span class="kpi-index">ENERGY / KWH</span><h2 class="kpi-label">累计充电量</h2><p class="kpi-value">{{ number(kpis.totalEnergyKwh, 1) }}<span class="unit">kWh</span></p><p class="kpi-hint">充电网络累计输出</p><MiniTrend :values="recent.map(r => r.energyKwh)" /></div>
      <div class="panel kpi-card"><span class="kpi-index">ORDERS / TOTAL</span><h2 class="kpi-label">累计订单</h2><p class="kpi-value">{{ number(kpis.totalOrders) }}<span class="unit">笔</span></p><p class="kpi-hint">已完成充电订单</p><MiniTrend :values="recent.map(r => r.orderCount)" /></div>
      <div class="panel kpi-card"><span class="kpi-index">NETWORK / HEALTH</span><h2 class="kpi-label">桩在线率</h2><p class="kpi-value">{{ kpis.onlineRate ?? '—' }}<span class="unit">%</span></p><p class="kpi-hint">空闲 {{ number(kpis.idleCount) }} / 总桩 {{ number(kpis.chargerCount) }}</p><svg class="online-gauge" viewBox="0 0 60 60" aria-hidden="true"><circle cx="30" cy="30" r="25" /><circle cx="30" cy="30" r="25" :stroke-dasharray="`${Math.max(0, Math.min(100, kpis.onlineRate ?? 0)) / 100 * 157} 157`" /></svg></div>
    </section>

    <section class="grid home-main">
      <div class="dv-col">
        <section class="panel panel--dv"><DvFrame title="近 30 日营收趋势（元）" code="01"><EChart v-if="view" :option="revenueOption" class="side-chart" /></DvFrame></section>
        <section class="panel panel--dv"><DvFrame title="充电桩状态分布（个）" code="02"><EChart v-if="view" :option="statusOption" class="side-chart" /></DvFrame></section>
      </div>
      <section class="panel map-panel" style="padding: 0" aria-label="北京市站点分布">
        <div class="map-heading"><div><h2>北京 · 城市充电网络</h2><small>BEIJING / CHARGING NETWORK</small></div><div class="map-count"><span><strong>{{ number(stations.length) }}</strong>站点</span><span><strong>{{ number(kpis.chargerCount) }}</strong>充电设备</span></div></div>
        <span class="map-coordinates" aria-hidden="true">GEOGRAPHIC VIEW / BEIJING</span>
        <EChart v-if="view" :option="stationOption" class="map-chart" @chart-click="onStationClick" />
        <p v-if="mapFailed" class="map-missing">离线底图不可用，已切换坐标散点。</p>
        <div class="map-legend"><span><i />站点 · 点大小表示设备容量</span><span class="map-instruction">拖动 / 缩放 · 点击站点进入详情 ↗</span></div>
      </section>
      <div class="dv-col">
        <section class="panel panel--dv"><DvFrame title="站点利用率排行（%）" code="03"><EChart v-if="view" :option="rankingOption" class="side-chart" /></DvFrame></section>
        <section class="panel panel--dv"><DvFrame title="数据质量 · 可追溯对账" code="04"><QualityPanel :quality="view?.quality" /></DvFrame></section>
      </div>
    </section>

    <section class="grid home-bottom">
      <section class="panel panel--dv"><DvFrame>
        <h2 class="panel-heading"><span class="frame-index">05</span>24 小时实际负荷与未来 24 小时预测（kW）<label class="panel-heading-extra">站点<select v-model.number="selectedStation" aria-label="负荷预测站点"><option v-for="s in stations" :key="s.stationId" :value="s.stationId">{{ s.name }}</option></select></label></h2>
        <EChart v-if="view" :option="loadOption" />
        <p class="note">{{ loadOption.noForecast ? '该站点暂无预测，仅展示实际负荷。' : `实线为历史负荷 · 虚线为${forecastSource} · 本批次结果，非实时电网遥测。` }}</p>
      </DvFrame></section>
      <section class="panel panel--dv"><DvFrame title="网络事件记录" code="06"><div v-if="view?.events?.length" class="event-board"><ScrollBoard :config="eventBoard" /></div><p v-else class="empty-state">本批暂无事件记录</p></DvFrame></section>
    </section>
  </ScaleFrame>
</template>
