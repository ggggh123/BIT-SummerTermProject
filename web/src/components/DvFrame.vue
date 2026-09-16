<script setup>
// DataV 大屏边框封装：统一把面板内容包进 dv-border-box-8，并在标题右侧加装饰，
// 满足老师要求第 5 条「使用 DataV 进行大屏展示」。样式与内容布局交给使用方。
import { BorderBox8 } from '@kjgl77/datav-vue3'
import { onBeforeUnmount, onMounted, ref } from 'vue'

const frame = ref(null)
let resizeObserver, resizeFrame
onMounted(() => {
  // DataV 1.7 自带观察器只监听 style 属性和 window.resize，不会发现异步图表撑高。
  // 按真实布局尺寸调用其公开 initWH，避免边框只包住标题、横穿图表。
  resizeObserver = new ResizeObserver(() => {
    cancelAnimationFrame(resizeFrame)
    resizeFrame = requestAnimationFrame(() => frame.value?.initWH())
  })
  if (frame.value?.$el) resizeObserver.observe(frame.value.$el)
})
onBeforeUnmount(() => { resizeObserver?.disconnect(); cancelAnimationFrame(resizeFrame) })

defineProps({
  title: { type: String, default: '' },
  code: { type: String, default: '' },
})
</script>

<template>
  <BorderBox8 ref="frame" class="dv-frame" :color="['#23434b', '#558d88']" :dur="20" background-color="transparent">
    <div class="dv-frame-body">
      <h2 v-if="title" class="dv-frame-title">
        <span class="frame-index">{{ code || '＋' }}</span><span>{{ title }}</span>
        <slot name="extra"><span class="frame-ticks" aria-hidden="true">▏▏▏</span></slot>
      </h2>
      <slot />
    </div>
  </BorderBox8>
</template>

<style scoped>
.dv-frame {
  width: 100%;
  height: 100%;
}

.dv-frame :deep(.border-box-content) { position: relative; height: 100%; }

.dv-frame-body {
  padding: 18px 20px 16px;
  height: 100%;
  min-width: 0;
}

.dv-frame-title {
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 0 0 10px;
  font-size: 15px;
}

.frame-index { font: 10px monospace; color: var(--accent); opacity: .7; }
.frame-ticks { margin-left: auto; color: #456761; font: 10px monospace; }
</style>
