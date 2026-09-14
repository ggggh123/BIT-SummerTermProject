<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import EChart from '@/components/EChart.vue'
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
  <p v-if="error" class="panel" style="border-color: #ff7a7a">数据加载失败：{{ error }}</p>

  <section class="grid kpi" aria-label="民生与减排概览">
    <div class="panel kpi-card">
      <h2 class="kpi-label">累计充电量</h2>
      <p class="kpi-value">{{ carbon ? formatKwh(carbon.totalEnergyKwh) : '—' }}</p>
      <p class="kpi-hint">全区县累计，用于减排折算</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">等效碳减排</h2>
      <p class="kpi-value">{{ carbon ? carbon.co2SavedTon : '—' }}<span style="font-size: 14px"> tCO₂</span></p>
      <p class="kpi-hint">折算因子 {{ carbon?.factorTonPerMwh ?? '—' }} tCO₂/MWh</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">等效植树</h2>
      <p class="kpi-value">{{ carbon ? carbon.equivalentTrees.toLocaleString('zh-CN') : '—' }}<span style="font-size: 14px"> 棵</span></p>
      <p class="kpi-hint">按单棵树年固碳 18kg 估算</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">全城峰值负荷</h2>
      <p class="kpi-value">{{ peakOption.meta ? Math.round(peakOption.meta.peakLoadKw).toLocaleString('zh-CN') : '—' }}<span style="font-size: 14px"> kW</span></p>
      <p class="kpi-hint">出现在 {{ peakOption.meta?.peakHour ?? '—' }}</p>
    </div>
  </section>

  <section class="panel" style="margin-top: 16px">
    <h2>行政区覆盖密度（桩数 / 每万人桩数）</h2>
    <EChart v-if="data" :option="coverageOption" tall />
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel">
      <h2>各区服务指标（订单 / 服务用户 / 平均等待）</h2>
      <table class="data-table">
        <thead>
          <tr><th>行政区</th><th>订单量</th><th>服务用户</th><th>平均等待</th><th>人均单量</th></tr>
        </thead>
        <tbody>
          <tr v-for="r in serviceRows" :key="r.district">
            <td>{{ r.district }}</td>
            <td>{{ r.orderCount.toLocaleString('zh-CN') }}</td>
            <td>{{ r.servedUserCnt.toLocaleString('zh-CN') }}</td>
            <td>{{ r.avgWaitMin }} 分钟</td>
            <td>{{ r.ordersPerUser }}</td>
          </tr>
        </tbody>
      </table>
    </div>
    <div class="panel">
      <h2>设施利用率公平性（识别「建而不用」）</h2>
      <EChart v-if="data" :option="utilOption" tall />
    </div>
  </section>

  <section class="panel" style="margin-top: 16px">
    <h2>全城 24 小时负荷曲线（辅助电网调度）</h2>
    <EChart v-if="data" :option="peakOption" tall />
    <p v-if="carbon" class="note">
      碳减排口径：{{ carbon.note }}；已校验「减排量 = 电量(MWh) × 因子」自洽：
      {{ carbon.consistent ? '一致' : '不一致（需核对）' }}。
    </p>
  </section>
</template>
