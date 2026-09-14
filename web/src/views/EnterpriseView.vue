<script setup>
// 企业视角：整体营收与发展（老师口述里最强调的那块）
const planned = [
  ['营收/订单/电量趋势', '7 / 30 / 90 日切换', 'GET /api/enterprise/revenue-trend'],
  ['站点营收排行', '营收、订单、客单价 Top N', 'GET /api/enterprise/station-ranking'],
  ['用户增长与活跃', '新增用户、日活充电用户曲线', 'GET /api/enterprise/user-growth'],
  ['用户 RFM 分层', '重要价值/保持/挽留用户分布', 'GET /api/enterprise/user-rfm'],
  ['月度经营汇总', '月度营收、电量、订单、单桩日均收益', 'GET /api/enterprise/monthly'],
]
</script>

<template>
  <div class="panel">
    <h2>企业视角 · 整体营收与发展</h2>
    <p class="note">数据源：ADS `ads_revenue_overview`、`ads_user_profile_rfm`；DWS `dws_station_day`。</p>
    <ul class="todo">
      <li v-for="[name, desc, api] in planned" :key="name">
        <strong>{{ name }}</strong> —— {{ desc }}（{{ api }}）<em>待接入</em>
      </li>
    </ul>
  </div>
</template>
