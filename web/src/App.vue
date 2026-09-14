<script setup>
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { dataSource, generatedAt } from '@/api/state'

const route = useRoute()
const title = computed(() => route.meta.title ?? '')
const navs = [
  { name: 'home', path: '/', label: '主页' },
  { name: 'user', path: '/user', label: '用户视角' },
  { name: 'station', path: '/station', label: '充电站视角' },
  { name: 'enterprise', path: '/enterprise', label: '企业视角' },
]
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
      <span class="pill" :class="dataSource === 'mock' ? 'pill-cached' : 'pill-live'">
        {{ dataSource === 'mock' ? '演示数据' : '实时接口' }}
      </span>
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
