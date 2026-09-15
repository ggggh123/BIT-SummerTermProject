<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { computeScale } from '@/lib/scale'

// 固定设计尺寸，按可用区域等比缩放（不拉伸、不裁切）
const props = defineProps({
  baseWidth: { type: Number, default: 1920 },
  baseHeight: { type: Number, default: 1080 },
  reservedHeight: { type: Number, default: 120 }, // 顶栏 + 页脚占位
})

const host = ref(null)
const scale = ref(1)

function update() {
  const width = host.value?.clientWidth ?? window.innerWidth
  const height = window.innerHeight - props.reservedHeight
  scale.value = computeScale(width, height, props.baseWidth, props.baseHeight)
}

onMounted(() => {
  update()
  window.addEventListener('resize', update)
})

onBeforeUnmount(() => window.removeEventListener('resize', update))
</script>

<template>
  <div ref="host" class="scale-host" :style="{ height: `${baseHeight * scale}px` }">
    <div
      class="scale-inner"
      :style="{ width: `${baseWidth}px`, height: `${baseHeight}px`, transform: `scale(${scale})` }"
    >
      <slot />
    </div>
  </div>
</template>
