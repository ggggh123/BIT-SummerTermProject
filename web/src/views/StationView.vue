<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import { fetchEnvelope, fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import { loadBeijingMap } from '@/lib/beijingMap'
import { hasCoordinates } from '@/lib/coordinates'
import { buildBeijingStationOption } from '@/lib/charts/beijingStation'
import { buildCoverageOption, buildHealthOption, buildMixOption, buildStationOccupancyOption, buildUtilizationOption } from '@/lib/charts/station'
import { createLatestRequest } from '@/lib/latestRequest'

const data = ref(null), detail = ref(null), stationId = ref(null), error = ref('')
const mapReady = ref(false), detailLoading = ref(false)
const forecastPoints = ref([]), forecastNote = ref('')
const route = useRoute()
const detailRequest = createLatestRequest()
let alive = true, initialized = false
async function load() {
  try {
    const raw = await fetchGroup({ stations: ENDPOINTS.home.stations, coverage: ENDPOINTS.station.coverage })
    if (!alive) return
    data.value = { stations: raw.stations.map(s => ({ ...s, stationId: Number(s.stationId) })), coverage: raw.coverage }
    const desired = initialized ? stationId.value : Number(route.query.station)
    initialized = true
    const next = data.value.stations.some(s => s.stationId === desired) ? desired : data.value.stations[0]?.stationId ?? null
    error.value = ''
    if (next !== stationId.value) stationId.value = next
    else if (next != null) await loadDetail()
    else detail.value = null
  } catch (e) { if (alive) error.value = e?.message ?? String(e) }
}
async function loadDetail() {
  if (stationId.value == null) return
  const current = detailRequest.begin(), id = stationId.value
  detailLoading.value = true
  try {
    const [u, m, h] = await Promise.all([
      fetchEnvelope(`station/${id}/utilization`), fetchEnvelope(`station/${id}/mix`), fetchEnvelope(`station/${id}/health`),
    ])
    if (!alive || !current()) return
    detail.value = { utilization: u.data, mix: m.data, health: h.data }
    error.value = ''
  } catch (e) { if (alive && current()) error.value = e?.message ?? String(e) }
  finally { if (alive && current()) detailLoading.value = false }
}
watch(stationId, () => {
  detailRequest.invalidate()
  detail.value = null
  if (stationId.value != null) loadDetail()
}, { flush: 'sync' })
watch(() => route.query.station, value => {
  const id = Number(value)
  if (data.value?.stations.some(s => s.stationId === id)) stationId.value = id
})
/** 预测单独拉取：无激活批次时接口返回 code=4041，只看本站点的点，不影响其余面板 */
async function loadForecast() {
  try {
    const { data: batch } = await fetchEnvelope(ENDPOINTS.station.forecast24h[0])
    forecastPoints.value = batch?.points ?? []
    forecastNote.value = ''
  } catch (e) {
    const msg = e?.message ?? String(e)
    forecastPoints.value = []
    forecastNote.value = /code=4041/.test(msg)
      ? '暂无预测：当前 ADS 没有激活的预测批次。'
      : `预测数据加载失败：${msg}`
  }
}

let stop, stopForecast
onMounted(() => {
  stop = startPolling(load, 60000)
  stopForecast = startPolling(loadForecast, 60000)
  loadBeijingMap().then(() => { if (alive) mapReady.value = true }).catch(() => {})
})
onBeforeUnmount(() => { alive = false; detailRequest.invalidate(); stop?.(); stopForecast?.() })
const stations = computed(() => data.value?.stations ?? [])
const station = computed(() => stations.value.find(s => s.stationId === stationId.value))
const stationName = computed(() => station.value?.name ?? '加载站点')
const utilOption = computed(() => detail.value ? buildUtilizationOption(detail.value.utilization) : {})
const mixOption = computed(() => detail.value ? buildMixOption(detail.value.mix) : {})
const healthOption = computed(() => detail.value ? buildHealthOption(detail.value.health) : {})
const stationForecast = computed(() => forecastPoints.value.filter(p => Number(p.stationId) === stationId.value))
const occupancyOption = computed(() => buildStationOccupancyOption(stationForecast.value, station.value?.chargerCount ?? null))
const forecastMissing = computed(() => Boolean(occupancyOption.value.meta?.noForecast))
const forecastSummary = computed(() => {
  const meta = occupancyOption.value.meta ?? {}
  if (meta.noForecast) return forecastNote.value || '该站点暂无预测，仅展示本批实际数据。'
  const rate = meta.busiestUtilization != null ? `，利用率 ${meta.busiestUtilization}%` : ''
  const risk = meta.fullLoadHours.length
    ? `满载风险时段（利用率 ≥90%）${meta.fullLoadHours.join('、')}`
    : '无利用率 ≥90% 的时段'
  return `${stationName.value}｜最忙 ${meta.busiestHour}（占用 ${meta.busiestOccupied}/${meta.chargerCount} 桩${rate}）` +
    `｜最空闲 ${meta.idlestHour}（空闲 ${meta.idlestIdle} 桩）｜${risk}` +
    `｜当日预测电量约 ${Math.round(meta.predictedEnergyKwh).toLocaleString('zh-CN')} kWh`
})
const coverageOption = computed(() => {
  if (!data.value) return {}
  const rows = data.value.coverage.map(c => ({ ...stations.value.find(s => s.stationId === Number(c.stationId)), ...c, stationId: Number(c.stationId) }))
  return mapReady.value ? buildBeijingStationOption({ stations: rows }, stationId.value) : buildCoverageOption(rows.filter(hasCoordinates))
})
const faultRate = computed(() => detail.value?.health.faultRate ?? null)
const unlocatedNames = computed(() => stations.value.filter(s => !hasCoordinates(s)).map(s => s.name))
function selectMapStation(p) {
  if (stations.value.some(s => s.stationId === p?.data?.stationId)) stationId.value = p.data.stationId
}
</script>

<template>
  <p v-if="error" class="error-banner" role="status">数据加载失败：{{ error }}。当前站点成功数据保留，切站数据不会混用。</p>
  <div class="view-intro"><div><p class="eyebrow">03 / STATION OPERATIONS</p><h2>设施运营 · 看懂每一个能源节点</h2><p>单站负荷、快慢充配置与设备健康联动分析。</p></div><label class="station-selector">当前站点<select v-model.number="stationId" aria-label="运营分析站点"><option v-for="s in stations" :key="s.stationId" :value="s.stationId">{{ s.name }}</option></select></label></div>
  <section class="panel station-facts" aria-label="站点概况"><span><strong>{{ station?.chargerCount ?? '—' }}</strong>充电设备</span><span><strong>{{ station?.idleCount ?? '—' }}</strong>当前空闲</span><span><strong>{{ station?.utilizationRate ?? '—' }}%</strong>站点利用率</span><span><strong>{{ faultRate ?? '—' }}%</strong>设备故障率</span><span v-if="detailLoading">正在核验所选站点…</span></section>
  <section class="analysis-grid station-grid">
    <section class="panel panel--dv"><DvFrame title="单站利用率时段曲线（%）" code="01"><p class="note">{{ stationName }}｜设备故障率 {{ faultRate ?? '—' }}%｜最近一日设备占用比例</p><EChart v-if="detail" :option="utilOption" /><p v-else class="empty-state">{{ detailLoading ? '正在读取该站点数据…' : '暂无站点数据' }}</p></DvFrame></section>
    <section class="panel panel--dv coverage-panel"><DvFrame title="区域覆盖与选址分析（点大小=桩数，悬停看服务半径）" code="04"><EChart v-if="data" :option="coverageOption" @chart-click="selectMapStation" /><p class="note">金色为当前选中站点 · 点击地图或下方站名切换。<template v-if="unlocatedNames.length">坐标不完整、不落图：{{ unlocatedNames.join('、') }}（保留统计，仍可选择查看）。</template></p><div class="station-roster"><button v-for="s in stations" :key="s.stationId" :aria-pressed="stationId === s.stationId" @click="stationId = s.stationId"><span>{{ String(s.stationId).padStart(2, '0') }}</span>{{ s.name }}</button></div></DvFrame></section>
    <div class="station-secondary">
      <section class="panel panel--dv"><DvFrame title="快慢充结构（个）" code="02"><EChart v-if="detail" :option="mixOption" /><p v-if="detail" class="note">快充订单占比 {{ (detail.mix.fastOrderShare * 100).toFixed(0) }}% · 功率见图例</p></DvFrame></section>
      <section class="panel panel--dv"><DvFrame title="设备健康：Top 桩累计充电次数" code="03"><EChart v-if="detail" :option="healthOption" /><p v-if="detail" class="note">存在故障记录 {{ detail.health.faultCount }} 桩 · 运维巡检参考</p></DvFrame></section>
    </div>
    <section class="panel panel--dv forecast-panel"><DvFrame title="未来 24h 桩位占用与忙碌时段预测（个 · 颜色=预测拥堵等级）" code="05"><EChart v-if="!forecastMissing" :option="occupancyOption" /><p v-else class="empty-state">{{ forecastSummary }}</p><p class="note">{{ forecastSummary }}<br>占用 + 空闲 = 本站桩数；颜色为模型给出的拥堵等级。本站视角看「什么时候会排队」，城市级负荷见政府视角。</p></DvFrame></section>
  </section>
</template>
