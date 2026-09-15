<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { computeScale } from '@/lib/scale'

// 固定设计尺寸，按可用区域等比缩放（不拉伸）
// 注意：宿主高度按**实际内容高度**算 —— 主页内容高于 1080 设计高度时，
// 早期实现（高度固定 1080×scale + overflow:hidden）会把下半部分静默裁掉
// （地图下半部、数据质量面板、事件流在 1920×1080 投屏上不可见）。
const props = defineProps({
  baseWidth: { type: Number, default: 1920 },
  baseHeight: { type: Number, default: 1080 },
  reservedHeight: { type: Number, default: 120 }, // 顶栏 + 页脚占位
})

const host = ref(null)
const inner = ref(null)
const scale = ref(1)
const contentHeight = ref(props.baseHeight)

function update() {
  const width = host.value?.clientWidth ?? window.innerWidth
  const height = window.innerHeight - props.reservedHeight
  scale.value = computeScale(width, height, props.baseWidth, props.baseHeight)
  contentHeight.value = Math.max(props.baseHeight, inner.value?.scrollHeight ?? props.baseHeight)
}

let observer = null

onMounted(() => {
  update()
  window.addEventListener('resize', update)
  observer = new ResizeObserver(update)
  if (inner.value) observer.observe(inner.value)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', update)
  observer?.disconnect()
})
</script>

<template>
  <div ref="host" class="scale-host" :style="{ height: `${contentHeight * scale}px` }">
    <div
      ref="inner"
      class="scale-inner"
      :style="{ width: `${baseWidth}px`, height: `${baseHeight}px`, transform: `scale(${scale})` }"
    >
      <slot />
    </div>
  </div>
</template>
