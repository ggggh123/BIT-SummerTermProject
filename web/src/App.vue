<script setup>
import { computed, watch } from 'vue'
import { useRoute } from 'vue-router'
import UiIcon from '@/components/UiIcon.vue'
import { connectionState, dataSource, generatedAt } from '@/api/state'
import { beginApiView } from '@/api/client'

const route = useRoute()
watch(() => route.path, beginApiView, { immediate: true, flush: 'sync' })
const navs = [
  { name: 'home', path: '/', label: '主页', caption: '全域总览' },
  { name: 'user', path: '/user', label: '用户视角', caption: '充电决策' },
  { name: 'station', path: '/station', label: '充电站视角', caption: '设施运营' },
  { name: 'enterprise', path: '/enterprise', label: '企业视角', caption: '经营洞察' },
  { name: 'gov', path: '/gov', label: '社会视角', caption: '城市效能' },
]
const badge = computed(() => {
  if (connectionState.value === 'error') return { cls: 'pill-error', text: '接口异常 · 保留上次数据' }
  if (dataSource.value === 'mock') return { cls: 'pill-cached', text: '本地演示快照' }
  if (!generatedAt.value) return { cls: 'pill-cached', text: '正在连接数据' }
  return { cls: 'pill-live', text: '数据接口已连接' }
})
const dataTime = computed(() => generatedAt.value ? generatedAt.value.replace('T', ' ').replace(/\+08:00$/, '') : '等待数据批次')
</script>

<template>
  <header class="topbar">
    <RouterLink to="/" class="brand" aria-label="城市能源指挥舱主页">
      <div class="brand-mark"><UiIcon name="energy" /></div>
      <div><p class="brand-eyebrow">NEUSOFT / URBAN ENERGY</p><h1>城市能源指挥舱<span>北京</span></h1></div>
    </RouterLink>
    <nav class="nav" aria-label="大屏视角导航">
      <RouterLink v-for="(n, i) in navs" :key="n.name" :to="n.path" class="nav-link" :class="{ 'is-current': route.path === n.path }" :aria-label="n.label">
        <UiIcon :name="n.name" /><span><small>0{{ i + 1 }} / {{ n.caption }}</small>{{ n.label }}</span>
      </RouterLink>
    </nav>
    <div class="status-area"><span class="pill" :class="badge.cls"><i />{{ badge.text }}</span><span class="generated-at">批次时间 {{ dataTime }}</span></div>
  </header>
  <main class="page" :class="{ 'page-home': route.path === '/' }"><RouterView /></main>
  <footer class="footnote">
    <span><i class="data-dot" />项目模拟数据 · 非真实城市运营数据</span>
    <span>离线数仓分析 / 按接口轮询更新 · 金额：元 · 时间：北京时间</span>
    <span class="foot-signature">EV ENERGY OBSERVATORY / 02</span>
  </footer>
</template>
