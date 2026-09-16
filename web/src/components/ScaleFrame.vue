<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { computeScale } from '@/lib/scale'

// 固定设计尺寸，按可用区域等比缩放（不拉伸）
// 注意：宿主高度按**实际内容高度**算 —— 主页内容高于 1080 设计高度时，
// 早期实现（高度固定 1080×scale + overflow:hidden）会把下半部分静默裁掉
// （地图下半部、数据质量面板、事件流在 1920×1080 投屏上不可见）。
const props = defineProps({
  baseWidth: { type: Number, default: 1840 },
  baseHeight: { type: Number, default: 920 },
  // 整体放大倍数：设计基准等比缩小，等效于把内容放大。
  // 宽度仍然按可用宽度自适应（不出现横向滚动），代价是纵向变高、可滚动。
  zoom: { type: Number, default: 1 },
  reservedHeight: { type: Number, default: 120 }, // 顶栏 + 页脚占位
})

// 设计基准（zoom 生效后）：zoom=1.06 即按 1840/1.06 × 920/1.06 排版再等比放大
const designWidth = computed(() => props.baseWidth / props.zoom)
const designHeight = computed(() => props.baseHeight / props.zoom)

const host = ref(null)
const inner = ref(null)
const scale = ref(1)
const contentHeight = ref(designHeight.value)

function update() {
  const width = host.value?.clientWidth ?? window.innerWidth
  // 预留高度按**真实占位**算：宿主顶部（顶栏等）+ 页脚 + 间距。
  // 早期固定 120 时，顶栏+页脚实际约 175px，导致整页比视口高 ~55px，1920×1080 下仍要滚一下。
  const top = (host.value?.getBoundingClientRect().top ?? 0) + window.scrollY
  const footer = document.querySelector('.footnote')?.offsetHeight ?? 0
  const reserved = Math.max(props.reservedHeight, Math.ceil(top + footer + 12))
  const height = Math.max(320, window.innerHeight - reserved)
  scale.value = computeScale(width, height, designWidth.value, designHeight.value)
  contentHeight.value = Math.max(designHeight.value, inner.value?.scrollHeight ?? designHeight.value)
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
      :style="{ width: `${designWidth}px`, minHeight: `${designHeight}px`, transform: `scale(${scale})`, marginLeft: `max(0px, calc((100% - ${designWidth * scale}px) / 2))` }"
    >
      <slot />
    </div>
  </div>
</template>
