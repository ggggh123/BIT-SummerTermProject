<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import { fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import { formatFen, formatKwh } from '@/lib/contracts'
import {
  buildMonthlyRows,
  buildRevenueTrendOption,
  buildRfmOption,
  buildStationRevenueRankingOption,
  buildUserGrowthOption,
} from '@/lib/charts/enterprise'

const data = ref(null)
const error = ref('')
const days = ref(30)

async function load() {
  try {
    const raw = await fetchGroup(ENDPOINTS.enterprise)
    data.value = {
      revenueTrend: raw.revenueTrend.points,
      ranking: raw.stationRanking,
      userGrowth: raw.userGrowth.points,
      rfm: raw.userRfm,
      monthly: raw.monthly,
    }
    error.value = ''
  } catch (e) {
    error.value = e?.message ?? String(e)
  }
}

let stop = null
onMounted(() => { stop = startPolling(load, 60000) })
onBeforeUnmount(() => stop?.())

const windowPoints = computed(() => (data.value ? data.value.revenueTrend.slice(-days.value) : []))
const trendOption = computed(() => (data.value ? buildRevenueTrendOption(windowPoints.value) : {}))
const rankingOption = computed(() => (data.value ? buildStationRevenueRankingOption(data.value.ranking) : {}))
const rfmOption = computed(() => (data.value ? buildRfmOption(data.value.rfm) : {}))
const monthlyRows = computed(() => (data.value ? buildMonthlyRows(data.value.monthly) : []))
const growthOption = computed(() =>
  data.value ? buildUserGrowthOption(data.value.userGrowth) : {},
)
const summary = computed(() => {
  const rows = windowPoints.value
  const revenueFen = rows.reduce((s, r) => s + (r.revenueFen ?? 0), 0)
  const orders = rows.reduce((s, r) => s + (r.orderCount ?? 0), 0)
  const energy = rows.reduce((s, r) => s + (r.energyKwh ?? 0), 0)
  return { revenueFen, orders, energy, avgTicketFen: orders ? Math.round(revenueFen / orders) : 0 }
})
</script>

<template>
  <p v-if="error" class="error-banner" role="status">数据加载失败：{{ error }}。保留上一次成功数据。</p>
  <div class="view-intro"><div><p class="eyebrow">04 / BUSINESS INTELLIGENCE</p><h2>经营洞察 · 能源背后的增长</h2><p>营收、用户价值与站点贡献，放在同一张经营图景里。</p></div><div class="intro-aside">统计窗口 / 最近 {{ days }} 日<br>已完成订单口径 · 经营分析</div></div>
  <section class="grid kpi" aria-label="经营概览">
    <div class="panel kpi-card"><span class="kpi-index">01 / REVENUE</span><h2 class="kpi-label">近 {{ days }} 日营收</h2><p class="kpi-value">{{ data ? formatFen(summary.revenueFen) : '—' }}</p><p class="kpi-hint">窗口：近 {{ days }} 日 · 所有站点</p></div>
    <div class="panel kpi-card"><span class="kpi-index">02 / ORDERS</span><h2 class="kpi-label">近 {{ days }} 日订单</h2><p class="kpi-value">{{ data ? summary.orders.toLocaleString('zh-CN') : '—' }}<span class="unit">笔</span></p><p class="kpi-hint">完成订单笔数</p></div>
    <div class="panel kpi-card"><span class="kpi-index">03 / ENERGY</span><h2 class="kpi-label">近 {{ days }} 日电量</h2><p class="kpi-value">{{ data ? formatKwh(summary.energy) : '—' }}</p><p class="kpi-hint">全部站点合计</p></div>
    <div class="panel kpi-card"><span class="kpi-index">04 / TICKET</span><h2 class="kpi-label">客单价</h2><p class="kpi-value">{{ data ? formatFen(summary.avgTicketFen) : '—' }}</p><p class="kpi-hint">营收 ÷ 完成订单数</p></div>
  </section>
  <section class="analysis-grid enterprise-grid">
    <section class="panel panel--dv trend-panel"><DvFrame>
      <h2 class="panel-heading"><span class="frame-index">01</span>营收 / 订单 / 电量趋势<span class="panel-heading-extra segment-control"><button v-for="d in [7, 30]" :key="d" :disabled="days === d" :aria-pressed="days === d" @click="days = d">近 {{ d }} 日</button></span></h2>
      <EChart v-if="data" :option="trendOption" tall /><p class="chart-note">左轴：营收（元） · 右轴：订单（笔）/ 电量（kWh）。切换窗口同步更新上方经营指标。</p>
    </DvFrame></section>
    <section class="panel panel--dv"><DvFrame title="站点营收排行（元）" code="02"><EChart v-if="data" :option="rankingOption" tall /><p class="chart-note">按累计营收排序 · 悬停查看该站订单与利用率。</p></DvFrame></section>
    <section class="panel panel--dv rfm-panel"><DvFrame title="用户 RFM 分层（人）" code="03"><EChart v-if="data" :option="rfmOption" /><p class="chart-note">以最近消费、频次与金额识别用户价值。</p></DvFrame></section>
    <section class="panel panel--dv growth-panel"><DvFrame title="用户增长与活跃" code="04"><EChart v-if="data" :option="growthOption" /><p class="chart-note">「窗口内首单新客」是窗口内完成首单的用户数，不是注册数；日活为当日产生订单的去重用户数。</p></DvFrame></section>
    <section class="panel panel--dv monthly-panel"><DvFrame title="月度经营汇总" code="05">
      <div class="table-scroll"><table class="data-table"><thead><tr><th>月份</th><th>营收</th><th>电量</th><th>订单</th><th>单桩月营收</th></tr></thead><tbody><tr v-for="r in monthlyRows" :key="r.month"><td>{{ r.month }}月</td><td>{{ formatFen(r.revenueFen) }}</td><td>{{ formatKwh(r.energyKwh) }}</td><td>{{ r.orderCount.toLocaleString('zh-CN') }}</td><td>{{ formatFen(r.revenuePerChargerFen) }}</td></tr></tbody></table></div>
      <p class="chart-note">单桩月营收按当前桩数摊算，非日均值；月度汇总不随 7 / 30 日按钮改变。</p>
    </DvFrame></section>
  </section>
</template>
