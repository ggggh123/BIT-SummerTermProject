<script setup>
// 充电站视角：站点分布与配置是否合理
const planned = [
  ['单站利用率时段曲线', '选中站点 90 天利用率与日内峰谷', 'GET /api/station/{id}/utilization'],
  ['快慢充结构饼图', '各站快充/慢充占比与功率结构', 'GET /api/station/{id}/mix'],
  ['设备健康面板', '故障率、累计充电次数/时长 Top 桩', 'GET /api/station/{id}/health'],
  ['区域覆盖与选址', '站点分布密度，识别覆盖盲区', 'GET /api/station/coverage'],
]
</script>

<template>
  <div class="panel">
    <h2>充电站视角 · 站点分布与配置是否合理</h2>
    <p class="note">数据源：DWS `dws_station_day`、`dws_charger_day`、ADS `ads_charger_health`。</p>
    <ul class="todo">
      <li v-for="[name, desc, api] in planned" :key="name">
        <strong>{{ name }}</strong> —— {{ desc }}（{{ api }}）<em>待接入</em>
      </li>
    </ul>
  </div>
</template>
