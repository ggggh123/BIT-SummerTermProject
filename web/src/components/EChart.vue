<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps({
  option: { type: Object, required: true },
  tall: { type: Boolean, default: false },
})

const el = ref(null)
let chart = null
let observer = null

function draw() {
  if (!chart) return
  // notMerge=true：每次全量替换，避免残影与残留 series
  chart.setOption(props.option, true)
}

onMounted(() => {
  chart = echarts.init(el.value, null, { renderer: 'canvas' })
  draw()
  observer = new ResizeObserver(() => chart && chart.resize())
  observer.observe(el.value)
})

watch(() => props.option, draw, { deep: true })

onBeforeUnmount(() => {
  observer?.disconnect()
  chart?.dispose()
  chart = null
})
</script>

<template>
  <div ref="el" class="chart" :class="{ tall }" />
</template>
