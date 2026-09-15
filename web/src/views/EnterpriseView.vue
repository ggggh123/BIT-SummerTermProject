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
  <p v-if="error" class="panel" style="border-color: #ff7a7a">数据加载失败：{{ error }}</p>

  <section class="grid kpi" aria-label="经营概览">
    <div class="panel kpi-card">
      <h2 class="kpi-label">近 {{ days }} 日营收</h2>
      <p class="kpi-value">{{ formatFen(summary.revenueFen) }}</p>
      <p class="kpi-hint">窗口：近 {{ days }} 日</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">近 {{ days }} 日订单</h2>
      <p class="kpi-value">{{ summary.orders.toLocaleString('zh-CN') }}</p>
      <p class="kpi-hint">完成订单笔数</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">近 {{ days }} 日电量</h2>
      <p class="kpi-value">{{ formatKwh(summary.energy) }}</p>
      <p class="kpi-hint">全部站点合计</p>
    </div>
    <div class="panel kpi-card">
      <h2 class="kpi-label">客单价</h2>
      <p class="kpi-value">{{ formatFen(summary.avgTicketFen) }}</p>
      <p class="kpi-hint">营收 ÷ 订单</p>
    </div>
  </section>

  <section class="panel panel--dv" style="margin-top: 16px">
    <DvFrame>
      <h2 class="panel-heading">
        营收 / 订单 / 电量趋势
        <span class="panel-heading-extra">
          <button v-for="d in [7, 30]" :key="d" :disabled="days === d" @click="days = d">近 {{ d }} 日</button>
        </span>
      </h2>
      <EChart v-if="data" :option="trendOption" tall />
    </DvFrame>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel panel--dv">
      <DvFrame title="站点营收排行（元）">
        <EChart v-if="data" :option="rankingOption" tall />
      </DvFrame>
    </div>
    <div class="panel panel--dv">
      <DvFrame title="用户 RFM 分层（人）">
        <EChart v-if="data" :option="rfmOption" tall />
      </DvFrame>
    </div>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel panel--dv">
      <DvFrame title="月度经营汇总">
        <table class="data-table">
          <thead>
            <tr><th>月份</th><th>营收</th><th>电量</th><th>订单</th><th>单桩日均收益</th></tr>
          </thead>
          <tbody>
            <tr v-for="r in monthlyRows" :key="r.month">
              <td>{{ r.month }}</td>
              <td>{{ formatFen(r.revenueFen) }}</td>
              <td>{{ formatKwh(r.energyKwh) }}</td>
              <td>{{ r.orderCount.toLocaleString('zh-CN') }}</td>
              <td>{{ formatFen(r.revenuePerChargerFen) }}</td>
            </tr>
          </tbody>
        </table>
      </DvFrame>
    </div>
    <div class="panel panel--dv">
      <DvFrame title="用户增长与活跃">
        <EChart v-if="data" :option="growthOption" />
        <p class="chart-note">
          「窗口内首单新客」＝窗口内完成首单的用户数，不是注册数（注册时间均在窗口之前，故本批为 0）；
          日活为当日在站点产生订单的去重用户数。
        </p>
      </DvFrame>
    </div>
  </section>
</template>
