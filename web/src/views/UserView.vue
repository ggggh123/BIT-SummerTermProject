<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import UiIcon from '@/components/UiIcon.vue'
import { fetchEnvelope, fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import {
  buildIdleRankingOption,
  buildPeakHeatmapOption,
  buildPriceCompareOption,
  buildPriceDistanceOption,
} from '@/lib/charts/user'

const data = ref(null)
const error = ref('')
const recommendations = ref([]), recommendError = ref('')

async function load() {
  try {
    const raw = await fetchGroup(ENDPOINTS.user)
    data.value = {
      priceCompare: raw.priceCompare,
      priceDistance: raw.priceDistance,
      idleRanking: raw.idleRanking,
      peakHeatmap: raw.peakHeatmap,
    }
    error.value = ''
  } catch (e) {
    error.value = e?.message ?? String(e)
  }
  try {
    const response = await fetchEnvelope(ENDPOINTS.user.recommend[0])
    recommendations.value = response.data
    recommendError.value = ''
  } catch (e) {
    recommendError.value = '推荐接口暂不可用，未展示过期推荐。'
    recommendations.value = []
  }
}

let stop = null
onMounted(() => { stop = startPolling(load, 60000) })
onBeforeUnmount(() => stop?.())

const cheapest = computed(() => {
  const rows = data.value?.priceCompare.stations ?? []
  return [...rows].sort((a, b) => a.priceFenPerKwh - b.priceFenPerKwh)[0] ?? null
})
const nearest = computed(() => [...(data.value?.priceDistance ?? [])].filter(r => r.distanceKm != null).sort((a, b) => a.distanceKm - b.distanceKm)[0] ?? null)
const mostIdle = computed(() => [...(data.value?.idleRanking ?? [])].sort((a, b) => b.idleCount - a.idleCount)[0] ?? null)

const priceOption = computed(() => (data.value ? buildPriceCompareOption(data.value.priceCompare) : {}))
const distanceOption = computed(() => (data.value ? buildPriceDistanceOption(data.value.priceDistance) : {}))
const idleOption = computed(() => (data.value ? buildIdleRankingOption(data.value.idleRanking) : {}))
const heatOption = computed(() => (data.value ? buildPeakHeatmapOption(data.value.peakHeatmap) : {}))
</script>

<template>
  <p v-if="error" class="error-banner" role="status">数据加载失败：{{ error }}。保留上一次成功数据。</p>
  <div class="view-intro"><div><p class="eyebrow">02 / CHARGING DECISIONS</p><h2>充电决策 · 找到适合你的下一站</h2><p>价格、距离、空闲与繁忙时段，四个维度看懂“去哪充”。</p></div><div class="intro-aside">参考位置 / 天安门<br>距离为地理直线距离，不是驾车导航里程</div></div>
  <section class="grid three" aria-label="用户决策速览">
    <div class="panel kpi-card"><span class="kpi-index">01 / BEST PRICE</span><h2 class="kpi-label">电价最低</h2><p class="kpi-value">{{ cheapest ? (cheapest.priceFenPerKwh / 100).toFixed(2) : '—' }}<span class="unit">元/度</span></p><p class="kpi-hint">{{ cheapest?.name ?? '—' }}</p></div>
    <div class="panel kpi-card"><span class="kpi-index">02 / NEAREST</span><h2 class="kpi-label">距市中心最近</h2><p class="kpi-value">{{ nearest ? nearest.distanceKm.toFixed(1) : '—' }}<span class="unit">km</span></p><p class="kpi-hint">{{ nearest?.name ?? '—' }} · 参考点：天安门</p></div>
    <div class="panel kpi-card"><span class="kpi-index">03 / AVAILABILITY</span><h2 class="kpi-label">当前空闲最多</h2><p class="kpi-value">{{ mostIdle ? mostIdle.idleCount : '—' }}<span class="unit">个</span></p><p class="kpi-hint">{{ mostIdle?.name ?? '—' }} · 空闲率 {{ mostIdle?.idleRate ?? '—' }}%</p></div>
  </section>
  <section class="analysis-grid user-grid">
    <section class="panel panel--dv"><DvFrame title="各站电价对比（元/度，含全市均价）" code="01"><EChart v-if="data" :option="priceOption" /><p class="chart-note">按电价从低到高排列 · 金色虚线为全市均价。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="充电时段热力图（站点 × 24h 占用桩数）" code="02"><EChart v-if="data" :option="heatOption" /><p class="chart-note">颜色越亮，时段内占用桩数越多；用于识别错峰机会。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="各站当前空闲桩排行（个）" code="03"><EChart v-if="data" :option="idleOption" /><p class="chart-note">数据为本批站点状态快照，不代表实际到站时保证有空位。</p></DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="距离-价格散点（找「近且便宜」，点大小=空闲桩）" code="04"><EChart v-if="data" :option="distanceOption" /><p class="chart-note">越靠左下方，参考点距离越近、电价越低；悬停查看站点。</p></DvFrame></section>
  </section>
  <section class="panel panel--dv recommendation-panel"><DvFrame title="本批未来 1h · 空闲推荐" code="05">
    <p v-if="recommendError || !recommendations.length" class="note">{{ recommendError || '暂无预测，等待有效模型结果。' }}</p>
    <div v-else class="recommendations"><RouterLink v-for="(s, i) in recommendations.slice(0, 3)" :key="s.stationId" :to="{path: '/station', query: {station: s.stationId}}" class="recommend-card"><span class="recommend-rank">0{{ i + 1 }}</span><div><strong>{{ s.name }}</strong><p>预测空闲 {{ s.predictedIdleCount }} 桩 · {{ {low: '低拥堵', medium: '中拥堵', high: '高拥堵'}[s.congestionLevel] || '拥堵等级未知' }}</p></div><UiIcon name="arrow" /></RouterLink></div>
    <p class="chart-note">按本批未来 1h 预测空闲桩数排序，与“当前空闲”分开呈现；不是实时可用承诺。</p>
  </DvFrame></section>
</template>
