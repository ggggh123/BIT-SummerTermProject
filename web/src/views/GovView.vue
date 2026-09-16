<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import { fetchEnvelope, fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import { formatKwh } from '@/lib/contracts'
import {
  buildCarbonSummary,
  buildCityForecastOption,
  buildDistrictCoverageOption,
  buildPeakLoadOption,
  buildServiceStatsRows,
  buildUtilizationFairnessOption,
} from '@/lib/charts/gov'

const data = ref(null)
const error = ref('')
const forecastPoints = ref([]), forecastNote = ref('')

async function load() {
  try {
    const raw = await fetchGroup(ENDPOINTS.gov)
    data.value = {
      coverage: raw.coverage,
      serviceStats: raw.serviceStats,
      carbon: raw.carbon,
      peakLoad: raw.peakLoad,
      utilization: raw.utilization,
    }
    error.value = ''
  } catch (e) {
    error.value = e?.message ?? String(e)
  }
}

/** 预测单独拉取：无激活批次时接口返回 code=4041，只影响预测面板，不影响其余指标 */
async function loadForecast() {
  try {
    const { data: batch } = await fetchEnvelope(ENDPOINTS.gov.forecast24h[0])
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

let stop = null
let stopForecast = null
onMounted(() => {
  stop = startPolling(load, 60000)
  stopForecast = startPolling(loadForecast, 60000)
})
onBeforeUnmount(() => { stop?.(); stopForecast?.() })

const coverageOption = computed(() => (data.value ? buildDistrictCoverageOption(data.value.coverage) : {}))
const serviceRows = computed(() => (data.value ? buildServiceStatsRows(data.value.serviceStats) : []))
const carbon = computed(() => (data.value ? buildCarbonSummary(data.value.carbon) : null))
const peakOption = computed(() => (data.value ? buildPeakLoadOption(data.value.peakLoad) : {}))
const utilOption = computed(() => (data.value ? buildUtilizationFairnessOption(data.value.utilization) : {}))
const cityForecastOption = computed(() => buildCityForecastOption(forecastPoints.value))
const cityForecastMissing = computed(() => Boolean(cityForecastOption.value.meta?.noForecast) || !forecastPoints.value.length)
/** 累计口径窗口（/api/gov/carbon 的 windowStart/End/Days）：缺字段时返回空串，页面不硬编码日期 */
const windowFull = computed(() => {
  const c = data.value?.carbon ?? {}
  if (!c.windowStart || !c.windowEnd) return ''
  return `${c.windowStart} ~ ${c.windowEnd}${c.windowDays ? `（${c.windowDays} 天）` : ''}`
})
const windowSinceText = computed(() => (data.value?.carbon?.windowStart ? ` · 自 ${String(data.value.carbon.windowStart).slice(5)} 累计` : ''))

const cityForecastSummary = computed(() => {
  const meta = cityForecastOption.value.meta ?? {}
  if (!meta.hours) return forecastNote.value || '暂无预测：当前 ADS 没有激活的预测批次。'
  const warn = meta.warnHours?.length ? meta.warnHours.join('、') : '无'
  return `${meta.stations} 个启用站按小时求和｜预测峰值 ${Math.round(meta.peakLoadKw)} kW（${meta.peakHour}）｜预测高峰时段 ${warn}` +
    `｜折算预测电量约 ${Math.round(meta.predictedEnergyKwh).toLocaleString('zh-CN')} kWh（各小时负荷 × 1h 求和）`
})
</script>

<template>
  <p v-if="error" class="error-banner" role="status">数据加载失败：{{ error }}。保留上一次成功数据。</p>
  <div class="view-intro"><div><p class="eyebrow">05 / URBAN IMPACT</p><h2>城市效能 · 让每一度电更有价值</h2><p>观察公共服务覆盖、设施利用差异与等效碳减排。</p></div><div class="intro-aside">公共服务 / 能源调度<br>按行政区汇总 · 减排为模型折算<template v-if="windowFull"><br>累计窗口 {{ windowFull }}</template></div></div>
  <section class="grid kpi" aria-label="民生与减排概览">
    <div class="panel kpi-card"><span class="kpi-index">ENERGY / TOTAL</span><h2 class="kpi-label">累计充电量</h2><p class="kpi-value">{{ carbon ? formatKwh(carbon.totalEnergyKwh) : '—' }}</p><p class="kpi-hint">全区县累计，用于减排折算{{ windowSinceText }}</p></div>
    <div class="panel kpi-card"><span class="kpi-index">CARBON / EQUIVALENT</span><h2 class="kpi-label">等效碳减排</h2><p class="kpi-value">{{ carbon ? carbon.co2SavedTon.toLocaleString('zh-CN') : '—' }}<span class="unit">tCO₂</span></p><p class="kpi-hint">折算因子 {{ carbon?.factorTonPerMwh ?? '—' }} tCO₂/MWh{{ windowSinceText }}</p></div>
    <div class="panel kpi-card"><span class="kpi-index">TREES / EQUIVALENT</span><h2 class="kpi-label">等效植树</h2><p class="kpi-value">{{ carbon ? carbon.equivalentTrees.toLocaleString('zh-CN') : '—' }}<span class="unit">棵</span></p><p class="kpi-hint">按单棵树年固碳 18kg 估算，非实际植树量{{ windowSinceText }}</p></div>
    <div class="panel kpi-card"><span class="kpi-index">LOAD / DAILY PEAK</span><h2 class="kpi-label">全城峰值负荷（当日）</h2><p class="kpi-value">{{ peakOption.meta?.peakLoadKw != null ? Math.round(peakOption.meta.peakLoadKw).toLocaleString('zh-CN') : '—' }}<span class="unit">kW</span></p><p class="kpi-hint">出现在 {{ peakOption.meta?.date ?? '—' }} {{ peakOption.meta?.peakHour ?? '—' }}（当日，非窗口峰值）</p></div>
  </section>
  <section class="analysis-grid gov-grid">
    <section class="panel panel--dv"><DvFrame title="行政区覆盖密度（桩数 / 每万人桩数）" code="01"><EChart v-if="data" :option="coverageOption" tall /><p class="chart-note">设备数量与人口密度并读，识别相对供给差异。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="全城 24 小时负荷曲线（辅助电网调度）" code="02"><EChart v-if="data" :option="peakOption" tall /><p class="chart-note">仅本批最后一日负荷 · 金色标记为当日峰值时段。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="各区服务指标（订单 / 服务用户 / 平均等待）" code="03"><div class="table-scroll"><table class="data-table"><thead><tr><th>行政区</th><th>订单量</th><th>服务用户</th><th>平均等待</th><th>人均单量</th></tr></thead><tbody><tr v-for="r in serviceRows" :key="r.district"><td>{{ r.district }}</td><td>{{ r.orderCount.toLocaleString('zh-CN') }}</td><td>{{ r.servedUserCnt.toLocaleString('zh-CN') }}</td><td>{{ r.avgWaitMin }} 分钟</td><td>{{ r.ordersPerUser }}</td></tr></tbody></table></div><p class="chart-note">服务用户按行政区去重；跨区充电的用户可能出现在多个区。</p><p class="chart-note">平均等待≈0 是本批模拟数据的特征：约 99.7% 的完成订单「预约后立即开始」（各区均值 0.2–0.4 分钟），生成器未模拟排队等待，不代表真实排队时长。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="设施利用率公平性（识别「建而不用」）" code="04"><EChart v-if="data" :option="utilOption" /><p v-if="carbon" class="chart-note">减排口径：{{ carbon.note }}。电量 × 因子核验：{{ carbon.consistent ? '一致' : '不一致，需核对' }}。</p></DvFrame></section>
    <section class="panel panel--dv city-forecast-panel"><DvFrame title="全城未来 24h 负荷预测与高峰预警（kW）" code="05"><EChart v-if="!cityForecastMissing" :option="cityForecastOption" tall /><p v-else class="empty-state">{{ cityForecastSummary }}</p><p class="chart-note">{{ cityForecastSummary }}<br>基于本批 Spark MLlib 预测批次（`GET /api/forecast/24h`），为各启用站点预测负荷之和，仅作调度参考。</p></DvFrame></section>
  </section>
</template>
