<script setup>
import { computed, onMounted, ref } from 'vue'
import EChart from '@/components/EChart.vue'
import { fetchGroup } from '@/api/client'
import { ENDPOINTS } from '@/api/endpoints'
import {
  buildIdleRankingOption,
  buildPeakHeatmapOption,
  buildPriceCompareOption,
  buildPriceDistanceOption,
} from '@/lib/charts/user'

const data = ref(null)
const error = ref('')

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
}

onMounted(load)

const cheapest = computed(() => {
  const rows = data.value?.priceCompare.stations ?? []
  return [...rows].sort((a, b) => a.priceFenPerKwh - b.priceFenPerKwh)[0] ?? null
})
const nearest = computed(() => data.value?.priceDistance[0] ?? null)
const mostIdle = computed(() => data.value?.idleRanking[0] ?? null)

const priceOption = computed(() => (data.value ? buildPriceCompareOption(data.value.priceCompare) : {}))
const distanceOption = computed(() => (data.value ? buildPriceDistanceOption(data.value.priceDistance) : {}))
const idleOption = computed(() => (data.value ? buildIdleRankingOption(data.value.idleRanking) : {}))
const heatOption = computed(() => (data.value ? buildPeakHeatmapOption(data.value.peakHeatmap) : {}))
</script>

<template>
  <p v-if="error" class="panel" style="border-color: #ff7a7a">数据加载失败：{{ error }}</p>

  <section class="grid kpi" aria-label="用户决策速览">
    <div class="panel kpi-card">
      <h2 class="kpi-label">电价最低</h2>
      <p class="kpi-value">{{ cheapest ? (cheapest.priceFenPerKwh / 100).toFixed(2) : '—' }}<span style="font-size: 14px"> 元/度</span></p>
      <p class="kpi-hint">{{ cheapest?.name ?? '—' }}</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">距市中心最近</h2>
      <p class="kpi-value">{{ nearest ? nearest.distanceKm.toFixed(1) : '—' }}<span style="font-size: 14px"> km</span></p>
      <p class="kpi-hint">{{ nearest?.name ?? '—' }}（参考点：天安门）</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">当前空闲最多</h2>
      <p class="kpi-value">{{ mostIdle ? mostIdle.idleCount : '—' }}<span style="font-size: 14px"> 个</span></p>
      <p class="kpi-hint">{{ mostIdle?.name ?? '—' }}（空闲率 {{ mostIdle?.idleRate ?? '—' }}%）</p>
    </div>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel">
      <h2>各站电价对比（元/度，含全市均价）</h2>
      <EChart v-if="data" :option="priceOption" tall />
    </div>
    <div class="panel">
      <h2>距离-价格散点（找「近且便宜」，点大小=空闲桩）</h2>
      <EChart v-if="data" :option="distanceOption" tall />
    </div>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel">
      <h2>各站当前空闲桩排行（个）</h2>
      <EChart v-if="data" :option="idleOption" tall />
    </div>
    <div class="panel">
      <h2>充电时段热力图（站点 × 24h 占用桩数）</h2>
      <EChart v-if="data" :option="heatOption" tall />
    </div>
  </section>

  <section class="panel" style="margin-top: 16px">
    <h2>低拥堵推荐榜（选做）</h2>
    <p class="note">
      该模块依赖 #5 的 `ads_forecast_result`（未来 1h 预测空闲桩与拥堵等级），接口 `GET /api/forecast/recommend` 就绪后接入；
      当前无预测结果时按《01》要求显示「暂无预测」，不伪造曲线。
    </p>
  </section>
</template>
