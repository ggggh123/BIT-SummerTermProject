<script setup>
import { computed } from 'vue'
const props = defineProps({ values: { type: Array, default: () => [] } })
const points = computed(() => {
  const rows = props.values.filter((v) => Number.isFinite(Number(v))).map(Number)
  if (rows.length < 2) return ''
  const low = Math.min(...rows), span = Math.max(...rows) - low || 1
  return rows.map((v, i) => `${i * 142 / (rows.length - 1) + 1},${33 - (v - low) / span * 28}`).join(' ')
})
</script>
<template>
  <svg class="mini-trend" viewBox="0 0 144 36" fill="none" aria-hidden="true">
    <path d="M0 35H144M0 18H144" stroke="currentColor" opacity=".12" />
    <polyline v-if="points" :points="points" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" />
  </svg>
</template>
