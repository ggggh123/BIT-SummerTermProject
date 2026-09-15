<script setup>
import { computed, onMounted, onBeforeUnmount, ref } from 'vue'
import { useRouter } from 'vue-router'
import EChart from '@/components/EChart.vue'
import ScaleFrame from '@/components/ScaleFrame.vue'
import { fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import { buildHomeViewModel } from '@/lib/viewModel'
import { formatFen, formatKwh } from '@/lib/contracts'
import { loadBeijingMap } from '@/lib/beijingMap'
import { buildBeijingStationOption } from '@/lib/charts/beijingStation'
import { buildRevenueTrendOption30d } from '@/lib/charts/overview'
import {
  buildLoadForecastOption,
  buildRankingOption,
  buildStationOption,
  buildStatusOption,
} from '@/lib/models'

const view = ref(null)
const error = ref('')
const selectedStation = ref(null)
const fast = ref(null)
const slow = ref(null)
const mapReady = ref(false)
const router = useRouter()

function rebuild() {
  if (!fast.value) return
  view.value = buildHomeViewModel({
    kpis: fast.value.kpis,
    stations: fast.value.stations,
    chargerStatus: fast.value.chargerStatus,
    events: fast.value.events,
    ranking: slow.value?.ranking ?? [],
    // 《01》§3.1：主页营收趋势为「近 30 日」
    revenueTrend: (slow.value?.revenueTrend?.points ?? []).slice(-30),
    load24h: slow.value?.load24h?.points ?? [],
    forecast24h: slow.value?.forecast24h?.points ?? [],
    quality: slow.value?.quality ?? null,
  })
  if (selectedStation.value === null) selectedStation.value = view.value.stations[0]?.stationId ?? null
}

/** 高频：KPI / 桩状态 / 事件流 / 站点（5s） */
async function loadFast() {
  try {
    const raw = await fetchGroup({
      kpis: ENDPOINTS.home.kpis,
      stations: ENDPOINTS.home.stations,
      chargerStatus: ENDPOINTS.home.chargerStatus,
      events: ENDPOINTS.home.events,
    })
    fast.value = raw
    error.value = ''
  } catch (e) {
    error.value = e?.message ?? String(e) // 保留上次成功数据
  }
  rebuild()
}

/** 低频：趋势 / 排行 / 负荷 / 预测 / 质量（60s） */
async function loadSlow() {
  try {
    const raw = await fetchGroup({
      revenueTrend: ENDPOINTS.home.revenueTrend,
      ranking: ENDPOINTS.home.ranking,
      load24h: ENDPOINTS.home.load24h,
      forecast24h: ENDPOINTS.home.forecast24h,
      quality: ENDPOINTS.home.quality,
    })
    slow.value = raw
    error.value = ''
  } catch (e) {
    error.value = e?.message ?? String(e)
  }
  rebuild()
}

let stopFast = null
let stopSlow = null

onMounted(() => {
  stopFast = startPolling(loadFast, 5000)
  stopSlow = startPolling(loadSlow, 60000)
  loadBeijingMap()
    .then(() => { mapReady.value = true })
    .catch(() => { mapReady.value = false }) // 加载失败自动降级为经纬度散点
})

onBeforeUnmount(() => {
  stopFast?.()
  stopSlow?.()
})

const kpis = computed(() => view.value?.kpis ?? {})
const revenueOption = computed(() => (view.value ? buildRevenueTrendOption30d(view.value) : {}))
const statusOption = computed(() => (view.value ? buildStatusOption(view.value) : {}))
const rankingOption = computed(() => (view.value ? buildRankingOption(view.value) : {}))
const stationOption = computed(() => {
  if (!view.value) return {}
  return mapReady.value ? buildBeijingStationOption(view.value) : buildStationOption(view.value)
})
function onStationClick(params) {
  if (params?.data?.stationId) router.push({ path: '/station', query: { station: params.data.stationId } })
}
const loadOption = computed(() =>
  view.value && selectedStation.value ? buildLoadForecastOption(view.value, selectedStation.value) : {},
)
const forecastMissing = computed(() => Boolean(loadOption.value?.noForecast))
const stations = computed(() => view.value?.stations ?? [])
const events = computed(() => view.value?.events ?? [])
const quality = computed(() => view.value?.quality ?? null)
const totalQualityIssues = computed(() =>
  (quality.value?.issues ?? []).reduce((sum, i) => sum + (i.detected ?? 0), 0),
)
</script>

<template>
  <p v-if="error" class="panel" style="border-color: #ff7a7a">
    数据加载失败：{{ error }}（页面保留上一次成功数据）
  </p>

  <ScaleFrame>
  <section class="grid kpi" aria-label="核心指标">
    <div class="panel kpi-card">
      <h2 class="kpi-label">累计营收</h2>
      <p class="kpi-value">{{ formatFen(kpis.totalRevenueFen) }}</p>
      <p class="kpi-hint">全站累计，含快充与慢充</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">累计充电量</h2>
      <p class="kpi-value">{{ formatKwh(kpis.totalEnergyKwh) }}</p>
      <p class="kpi-hint">全部充电桩累计输出电量</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">累计订单</h2>
      <p class="kpi-value">{{ (kpis.totalOrders ?? 0).toLocaleString('zh-CN') }}</p>
      <p class="kpi-hint">完成订单笔数</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">桩在线率</h2>
      <p class="kpi-value">{{ kpis.onlineRate ?? '—' }}%</p>
      <p class="kpi-hint">空闲桩 {{ kpis.idleCount ?? '—' }} / 总桩 {{ kpis.chargerCount ?? '—' }}</p>
    </div>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel">
      <h2>近 30 日营收趋势（元）</h2>
      <EChart v-if="view" :option="revenueOption" />
    </div>
    <div class="panel">
      <h2>充电桩状态分布（个）</h2>
      <EChart v-if="view" :option="statusOption" />
    </div>
  </section>

  <section class="panel" style="margin-top: 16px">
    <h2>
      24 小时实际负荷与未来 24 小时预测（kW）
      <label style="float: right; font-weight: 400; font-size: 13px">
        站点
        <select v-model.number="selectedStation">
          <option v-for="s in stations" :key="s.stationId" :value="s.stationId">{{ s.name }}</option>
        </select>
      </label>
    </h2>
    <EChart v-if="view" :option="loadOption" tall />
    <p v-if="forecastMissing" class="note">该站点暂无预测（模型未产出或未启用），仅显示实际负荷。</p>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel">
      <h2>站点利用率排行（%）</h2>
      <EChart v-if="view" :option="rankingOption" />
    </div>
    <div class="panel">
      <h2>北京市站点分布（点击站点跳转充电站视角）</h2>
      <EChart v-if="view" :option="stationOption" @chart-click="onStationClick" />
      <p v-if="!mapReady" class="note">北京 GeoJSON 底图加载失败，已降级为经纬度散点（离线文件：public/geo/beijing.json）。</p>
    </div>
  </section>

  <section class="grid two-even" style="margin-top: 16px">
    <div class="panel">
      <h2>数据质量（PySpark 探查与清洗对账）</h2>
      <p v-if="!quality" class="note">暂无质量报告，等待 #3 的 quality_report.json。</p>
      <template v-else>
        <p class="note">10 类问题累计检出 {{ totalQualityIssues.toLocaleString('zh-CN') }} 条</p>
        <ul class="event-list">
          <li v-for="i in quality.issues" :key="i.rule">
            <span class="time">{{ i.rule }}</span>
            <span>{{ i.type }}</span>
            <span style="margin-left: auto">注入 {{ i.injected }} / 检出 {{ i.detected }}</span>
          </li>
        </ul>
      </template>
    </div>
    <div class="panel">
      <h2>实时事件流</h2>
      <ul class="event-list">
        <li v-for="e in events" :key="e.createdAt + e.eventType">
          <span class="time">{{ e.createdAt?.slice(11, 16) }}</span>
          <span>{{ e.message }}</span>
        </li>
      </ul>
    </div>
  </section>
  </ScaleFrame>
</template>
