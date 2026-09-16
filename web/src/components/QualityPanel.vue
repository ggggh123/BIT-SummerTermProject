<script setup>
import { computed, ref } from 'vue'
import UiIcon from './UiIcon.vue'
const props = defineProps({ quality: { type: Object, default: null } })
const dialog = ref(null)
const tab = ref('issues')
const total = computed(() => (props.quality?.issues ?? []).reduce((n, i) => n + (i.detected ?? 0), 0))
const maximum = computed(() => Math.max(1, ...(props.quality?.issues ?? []).map(i => i.detected ?? 0)))
const readiness = computed(() => props.quality?.readyForTeamDelivery === true ? '交付策略已确认' : props.quality?.readyForTeamDelivery === false ? '本批含待确认策略' : '策略状态未发布')
const number = (n) => n === null || n === undefined ? '—' : Number(n).toLocaleString('zh-CN')
const percent = (n) => n === null || n === undefined ? '—' : `${(Number(n) * 100).toFixed(1)}%`
</script>
<template>
  <p v-if="!quality" class="empty-state">质量报告尚未发布</p>
  <template v-else>
    <div class="quality-summary"><strong>{{ number(total) }}</strong><span>条规则命中 / {{ quality.issues.length }} 类规则</span></div>
    <div class="quality-strip" aria-hidden="true"><span v-for="i in quality.issues" :key="i.rule" :style="{ height: `${Math.max(3, i.detected / maximum * 20)}px` }" /></div>
    <div class="quality-meta"><span class="quality-state" :class="{ ready: quality.readyForTeamDelivery === true }">{{ readiness }}</span><button class="text-button" @click="dialog.showModal()">查看质量明细<UiIcon name="arrow" /></button></div>
    <p class="note">同一记录可命中多条规则，命中总数不等于删除行数。</p>
  </template>
  <dialog ref="dialog" class="dialog" aria-labelledby="quality-title" @click="e => { if (e.target === dialog) dialog.close() }">
    <div class="dialog-heading"><div><p class="eyebrow">DATA QUALITY / TRACEABLE EVIDENCE</p><h2 id="quality-title">数据质量 · 从原始数据到可信指标</h2></div><button class="dialog-close" aria-label="关闭质量明细" @click="dialog.close()"><UiIcon name="close" /></button></div>
    <p class="quality-run">ADS {{ quality?.runId || '—' }}<br>质量批次 {{ quality?.qualityRunId || '未发布' }} · 策略 {{ quality?.policyVersion || '未发布' }}</p>
    <p class="quality-state" :class="{ ready: quality?.readyForTeamDelivery === true }">{{ readiness }}</p>
    <div class="dialog-tabs" role="group" aria-label="质量明细类别"><button :aria-pressed="tab === 'issues'" @click="tab = 'issues'">规则对账</button><button :aria-pressed="tab === 'tables'" @click="tab = 'tables'">清洗前后与级联</button><button :aria-pressed="tab === 'policy'" @click="tab = 'policy'">数据口径与策略</button></div>
    <template v-if="tab === 'issues'">
      <div class="table-scroll"><table class="data-table"><thead><tr><th>规则</th><th>注入</th><th>检出</th><th>真阳性 TP</th><th>误报 FP</th><th>漏检 FN</th><th>召回率</th><th>精确率</th></tr></thead><tbody><tr v-for="i in quality?.issues" :key="i.rule"><td>{{ i.rule }} · {{ i.type }}</td><td>{{ number(i.injected) }}</td><td>{{ number(i.detected) }}</td><td>{{ number(i.truePositive) }}</td><td>{{ number(i.falsePositive) }}</td><td>{{ number(i.falseNegative) }}</td><td>{{ percent(i.recall) }}</td><td>{{ percent(i.precision) }}</td></tr></tbody></table></div>
      <p class="note">{{ quality?.metricSemantics || quality?.note }}<br>“—”表示本批未发布或分母为零；不把缺失的 FP / FN 当作 0。</p>
    </template>
    <template v-else-if="tab === 'tables'">
      <div class="table-scroll"><table class="data-table"><thead><tr><th>数据表</th><th>清洗前</th><th>清洗后</th><th>行数减少</th><th>级联影响行</th></tr></thead><tbody><tr v-for="t in quality?.tables" :key="t.name"><td>{{ t.name }}</td><td>{{ number(t.rowsBefore) }}</td><td>{{ number(t.rowsAfter) }}</td><td>{{ number(t.rowsBefore - t.rowsAfter) }}</td><td>{{ number(t.cascadeAffectedRows) }}</td></tr></tbody></table></div>
      <p class="note">级联影响来自清洗报告中的关联隔离统计；不通过“清洗前 − 清洗后”推算。字段修复可以保持行数不变。</p>
    </template>
    <template v-else>
      <h3>报告来源</h3><p class="note">{{ quality?.source || '未发布来源标识' }}<br>{{ quality?.note }}<br>{{ quality?.metricSemantics }}</p>
      <h3>待确认策略</h3><ul v-if="quality?.pendingPolicyNotes?.length" class="note"><li v-for="(n, i) in quality.pendingPolicyNotes" :key="i">{{ typeof n === 'string' ? n : JSON.stringify(n) }}</li></ul><p v-else class="note">本批未列出待确认策略。是否可正式交付，以报告状态及团队确认结果为准。</p>
      <h3>地图展示口径</h3><p class="note">缺失经纬度的站点不落图，但保留站点选择和经营统计；本页不在浏览器里补造坐标。</p>
    </template>
  </dialog>
</template>
