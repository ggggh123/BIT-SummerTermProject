<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { connectionState, dataSource, generatedAt } from '@/api/state'

const route = useRoute()
const title = computed(() => route.meta.title ?? '')
const navs = [
  { name: 'home', path: '/', label: '主页' },
  { name: 'user', path: '/user', label: '用户视角' },
  { name: 'station', path: '/station', label: '充电站视角' },
  { name: 'enterprise', path: '/enterprise', label: '企业视角' },
  { name: 'gov', path: '/gov', label: '政府视角' },
]

// 数据来源与新鲜度：接口失败时保留上次成功数据，并在顶部明确标出「已过期」
const badge = computed(() => {
  if (dataSource.value === 'mock') return { cls: 'pill-cached', text: '演示数据' }
  if (connectionState.value === 'error') return { cls: 'pill-error', text: '数据已过期 · 保留上次成功数据' }
  return { cls: 'pill-live', text: '实时接口' }
})
</script>

<template>
  <header class="topbar">
    <div class="brand">
      <h1>电动汽车充电平台 · 大数据运营大屏</h1>
      <p class="subtitle">{{ title }}｜数据来自 SparkSQL 数仓 ADS 层 · 项目生成的模拟数据</p>
    </div>
    <nav class="nav">
      <RouterLink v-for="n in navs" :key="n.name" :to="n.path" class="nav-link">{{ n.label }}</RouterLink>
    </nav>
    <div class="status-area">
      <span class="pill" :class="badge.cls">{{ badge.text }}</span>
      <span class="generated-at">数据时间：{{ generatedAt || '—' }}</span>
    </div>
  </header>

  <main class="page">
    <RouterView />
  </main>

  <footer class="footnote">
    金额单位：元（后端为整数分，前端统一除 100）｜时间口径 +08:00 ISO 8601｜预测模块无结果时显示「暂无预测」
  </footer>
</template>
