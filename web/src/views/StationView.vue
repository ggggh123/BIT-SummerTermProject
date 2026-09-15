<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import EChart from '@/components/EChart.vue'
import DvFrame from '@/components/DvFrame.vue'
import { fetchEnvelope, fetchGroup } from '@/api/client'
import { startPolling } from '@/api/polling'
import { ENDPOINTS } from '@/api/endpoints'
import {
  buildCoverageOption,
  buildHealthOption,
  buildMixOption,
  buildUtilizationOption,
} from '@/lib/charts/station'

const data = ref(null)
const detail = ref(null)
const stationId = ref(1)
const error = ref('')
const route = useRoute()

async function load() {
  try {
    const raw = await fetchGroup({ stations: ENDPOINTS.home.stations, coverage: ENDPOINTS.station.coverage })
    data.value = { stations: raw.stations, coverage: raw.coverage }
    // 主页站点散点点击会带上 ?station=<id>
    const fromQuery = Number(route.query.station)
    const exists = raw.stations.some((s) => s.stationId === fromQuery)
    stationId.value = exists ? fromQuery : raw.stations[0]?.stationId ?? 1
    await loadDetail()
    error.value = ''
  } catch (e) {
    error.value = e?.message ?? String(e)
  }
}

async function loadDetail() {
  const [u, m, h] = await Promise.all([
    fetchEnvelope(`station/${stationId.value}/utilization`),
    fetchEnvelope(`station/${stationId.value}/mix`),
    fetchEnvelope(`station/${stationId.value}/health`),
  ])
  detail.value = { utilization: u.data, mix: m.data, health: h.data }
}

let stop = null
onMounted(() => { stop = startPolling(load, 60000) })
onBeforeUnmount(() => stop?.())
watch(stationId, () => { loadDetail().catch((e) => { error.value = e?.message ?? String(e) }) })

const stations = computed(() => data.value?.stations ?? [])
const stationName = computed(() => stations.value.find((s) => s.stationId === stationId.value)?.name ?? '')
const utilOption = computed(() => (detail.value ? buildUtilizationOption(detail.value.utilization) : {}))
const mixOption = computed(() => (detail.value ? buildMixOption(detail.value.mix) : {}))
const healthOption = computed(() => (detail.value ? buildHealthOption(detail.value.health) : {}))
const coverageOption = computed(() => (data.value ? buildCoverageOption(data.value.coverage) : {}))
const faultRate = computed(() => detail.value?.health.faultRate ?? null)
</script>

<template>
  <p v-if="error" class="panel" style="border-color: #ff7a7a">数据加载失败：{{ error }}</p>

  <section class="panel panel--dv">
    <DvFrame>
      <h2 class="panel-heading">
        单站利用率时段曲线（%）
        <label class="panel-heading-extra">
          站点
          <select v-model.number="stationId">
            <option v-for="s in stations" :key="s.stationId" :value="s.stationId">{{ s.name }}</option>
          </select>
        </label>
      </h2>
      <p class="note">
        {{ stationName }}｜设备故障率 {{ faultRate ?? '—' }}%｜数据源 DWS `dws_station_day`、ADS `ads_charger_health`
      </p>
      <EChart v-if="detail" :option="utilOption" tall />
    </DvFrame>
  </section>

  <section class="grid two" style="margin-top: 16px">
    <div class="panel panel--dv">
      <DvFrame title="快慢充结构（个）">
        <EChart v-if="detail" :option="mixOption" />
        <p v-if="detail" class="note">快充订单占比 {{ (detail.mix.fastOrderShare * 100).toFixed(0) }}%，单桩功率结构见扇区标签</p>
      </DvFrame>
    </div>
    <div class="panel panel--dv">
      <DvFrame title="设备健康：Top 桩累计充电次数">
        <EChart v-if="detail" :option="healthOption" />
        <p v-if="detail" class="note">存在故障记录的桩 {{ detail.health.faultCount }} 个，建议纳入远程运维巡检</p>
      </DvFrame>
    </div>
  </section>

  <section class="panel panel--dv" style="margin-top: 16px">
    <DvFrame title="区域覆盖与选址分析（点大小=桩数，悬停看服务半径）">
      <EChart v-if="data" :option="coverageOption" tall />
      <p class="note">用于识别覆盖盲区与配置失衡；北京 GeoJSON 行政区底图待接入（本地文件，不走 CDN）。</p>
    </DvFrame>
  </section>
</template>
