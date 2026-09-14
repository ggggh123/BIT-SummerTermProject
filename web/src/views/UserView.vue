<script setup>
// 用户视角：去哪充电划算、不排队（老师口述的 3 个界面之一）
const planned = [
  ['站点价格对比', '各站 元/度 对比，标注全市均价', 'GET /api/user/price-compare'],
  ['距离-价格散点', '以天安门为参考点，找「近且便宜」的站', 'GET /api/user/price-distance'],
  ['实时空闲桩排行', '各站当前空闲数量与占比', 'GET /api/user/idle-ranking'],
  ['充电时段热力图', '站点 × 24h 繁忙程度，引导错峰', 'GET /api/user/peak-heatmap'],
  ['低拥堵推荐榜（选做）', '结合 ML 未来 1h 预测空闲桩与拥堵等级推荐', 'GET /api/forecast/recommend'],
]
</script>

<template>
  <div class="panel">
    <h2>用户视角 · 去哪充电划算、不排队</h2>
    <p class="note">数据源：ADS `ads_station_ranking`、`ads_peak_hour`；预测部分依赖 `ads_forecast_result`。</p>
    <ul class="todo">
      <li v-for="[name, desc, api] in planned" :key="name">
        <strong>{{ name }}</strong> —— {{ desc }}（{{ api }}）<em>待接入</em>
      </li>
    </ul>
    <p class="note">页面骨架与接口契约为第一阶段复用的部分；图表实现沿用 dashboard 的 option 构造器。</p>
  </div>
</template>
