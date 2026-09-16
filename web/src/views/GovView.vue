<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import { fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import { formatKwh } from '@/lib/contracts'
import {
  buildCarbonSummary,
  buildDistrictCoverageOption,
  buildPeakLoadOption,
  buildServiceStatsRows,
  buildUtilizationFairnessOption,
} from '@/lib/charts/gov'

const data = ref(null)
const error = ref('')

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

let stop = null
onMounted(() => { stop = startPolling(load, 60000) })
onBeforeUnmount(() => stop?.())

const coverageOption = computed(() => (data.value ? buildDistrictCoverageOption(data.value.coverage) : {}))
const serviceRows = computed(() => (data.value ? buildServiceStatsRows(data.value.serviceStats) : []))
const carbon = computed(() => (data.value ? buildCarbonSummary(data.value.carbon) : null))
const peakOption = computed(() => (data.value ? buildPeakLoadOption(data.value.peakLoad) : {}))
const utilOption = computed(() => (data.value ? buildUtilizationFairnessOption(data.value.utilization) : {}))
</script>

<template>
  <p v-if="error" class="error-banner" role="status">数据加载失败：{{ error }}。保留上一次成功数据。</p>
  <div class="view-intro"><div><p class="eyebrow">05 / URBAN IMPACT</p><h2>城市效能 · 让每一度电更有价值</h2><p>观察公共服务覆盖、设施利用差异与等效碳减排。</p></div><div class="intro-aside">公共服务 / 能源调度<br>按行政区汇总 · 减排为模型折算</div></div>
  <section class="grid kpi" aria-label="民生与减排概览">
    <div class="panel kpi-card"><span class="kpi-index">ENERGY / TOTAL</span><h2 class="kpi-label">累计充电量</h2><p class="kpi-value">{{ carbon ? formatKwh(carbon.totalEnergyKwh) : '—' }}</p><p class="kpi-hint">全区县累计，用于减排折算</p></div>
    <div class="panel kpi-card"><span class="kpi-index">CARBON / EQUIVALENT</span><h2 class="kpi-label">等效碳减排</h2><p class="kpi-value">{{ carbon ? carbon.co2SavedTon.toLocaleString('zh-CN') : '—' }}<span class="unit">tCO₂</span></p><p class="kpi-hint">折算因子 {{ carbon?.factorTonPerMwh ?? '—' }} tCO₂/MWh</p></div>
    <div class="panel kpi-card"><span class="kpi-index">TREES / EQUIVALENT</span><h2 class="kpi-label">等效植树</h2><p class="kpi-value">{{ carbon ? carbon.equivalentTrees.toLocaleString('zh-CN') : '—' }}<span class="unit">棵</span></p><p class="kpi-hint">按单棵树年固碳 18kg 估算，非实际植树量</p></div>
    <div class="panel kpi-card"><span class="kpi-index">LOAD / DAILY PEAK</span><h2 class="kpi-label">全城峰值负荷（当日）</h2><p class="kpi-value">{{ peakOption.meta?.peakLoadKw != null ? Math.round(peakOption.meta.peakLoadKw).toLocaleString('zh-CN') : '—' }}<span class="unit">kW</span></p><p class="kpi-hint">出现在 {{ peakOption.meta?.date ?? '—' }} {{ peakOption.meta?.peakHour ?? '—' }}（当日，非窗口峰值）</p></div>
  </section>
  <section class="analysis-grid gov-grid">
    <section class="panel panel--dv"><DvFrame title="行政区覆盖密度（桩数 / 每万人桩数）" code="01"><EChart v-if="data" :option="coverageOption" tall /><p class="chart-note">设备数量与人口密度并读，识别相对供给差异。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="全城 24 小时负荷曲线（辅助电网调度）" code="02"><EChart v-if="data" :option="peakOption" tall /><p class="chart-note">仅本批最后一日负荷 · 金色标记为当日峰值时段。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="各区服务指标（订单 / 服务用户 / 平均等待）" code="03"><div class="table-scroll"><table class="data-table"><thead><tr><th>行政区</th><th>订单量</th><th>服务用户</th><th>平均等待</th><th>人均单量</th></tr></thead><tbody><tr v-for="r in serviceRows" :key="r.district"><td>{{ r.district }}</td><td>{{ r.orderCount.toLocaleString('zh-CN') }}</td><td>{{ r.servedUserCnt.toLocaleString('zh-CN') }}</td><td>{{ r.avgWaitMin }} 分钟</td><td>{{ r.ordersPerUser }}</td></tr></tbody></table></div><p class="chart-note">服务用户按行政区去重；跨区充电的用户可能出现在多个区。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="设施利用率公平性（识别「建而不用」）" code="04"><EChart v-if="data" :option="utilOption" /><p v-if="carbon" class="chart-note">减排口径：{{ carbon.note }}。电量 × 因子核验：{{ carbon.consistent ? '一致' : '不一致，需核对' }}。</p></DvFrame></section>
  </section>
</template>
