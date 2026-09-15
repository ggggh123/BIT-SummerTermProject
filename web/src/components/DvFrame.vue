<script setup>
// DataV 大屏边框封装：统一把面板内容包进 dv-border-box-8，并在标题右侧加装饰，
// 满足老师要求第 5 条「使用 DataV 进行大屏展示」。样式与内容布局交给使用方。
import { BorderBox8, Decoration10 } from '@kjgl77/datav-vue3'

defineProps({
  title: { type: String, default: '' },
})
</script>

<template>
  <BorderBox8 class="dv-frame">
    <div class="dv-frame-body">
      <h2 v-if="title" class="dv-frame-title">
        <span>{{ title }}</span>
        <Decoration10 class="dv-frame-deco" />
      </h2>
      <slot />
    </div>
  </BorderBox8>
</template>

<style scoped>
.dv-frame {
  width: 100%;
}

.dv-frame-body {
  padding: 14px 18px 18px;
}

.dv-frame-title {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 0 0 10px;
  font-size: 15px;
}

.dv-frame-deco {
  flex: none;
  width: 140px;
  height: 8px;
  margin-left: auto;
}
</style>
