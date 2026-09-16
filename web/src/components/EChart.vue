<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { withEnergyTheme } from '@/lib/chartTheme'

const props = defineProps({
  option: { type: Object, required: true },
  tall: { type: Boolean, default: false },
})

const emit = defineEmits(['chart-click'])

const el = ref(null)
let chart = null
let observer = null

function draw() {
  if (!chart) return
  const next = withEnergyTheme(props.option, {
    reducedMotion: window.matchMedia('(prefers-reduced-motion: reduce)').matches,
  })
  // notMerge=true 全量替换会把用户手动缩放/拖拽的 geo 视角重置回默认，
  // 轮询场景下表现为「放大后过几秒自己缩回去」；这里把当前 roam 状态带回新 option。
  const prevGeo = chart.getOption()?.geo
  if (next.geo && prevGeo?.length) {
    const carry = (g, p) => ({ ...g, zoom: p?.zoom ?? g.zoom, center: p?.center ?? g.center })
    next.geo = Array.isArray(next.geo)
      ? next.geo.map((g, i) => carry(g, prevGeo[i]))
      : carry(next.geo, prevGeo[0])
  }
  // notMerge=true：每次全量替换，避免残影与残留 series
  chart.setOption(next, true)
}

onMounted(() => {
  chart = echarts.init(el.value, null, { renderer: 'canvas' })
  // 把实例挂到容器上：页面里没有全局 echarts，E2E/排障需要取实例（如 convertToPixel 换算点位）
  el.value.__echarts = chart
  draw()
  chart.on('click', (params) => emit('chart-click', params))
  observer = new ResizeObserver(() => chart && chart.resize())
  observer.observe(el.value)
})

watch(() => props.option, draw, { deep: true })

onBeforeUnmount(() => {
  observer?.disconnect()
  chart?.dispose()
  delete el.value?.__echarts
  chart = null
})
</script>

<template>
  <div ref="el" class="chart" :class="{ tall }" role="img" :aria-label="option.series?.map(s => s.name).join('、') || '正在加载图表'" />
</template>
