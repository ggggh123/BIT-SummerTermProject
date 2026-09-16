// 第二阶段 Web 显示层：不改业务数值，不修改第一阶段共享构造器。
export const ENERGY_COLORS = ['#65dec0', '#78acd5', '#d8b27a', '#a792cd', '#e78d76', '#7b9b98']
const DONUT_CENTER = ['29%', '48%']
export function compactNumber(value) {
  const n = Number(value)
  if (!Number.isFinite(n)) return '—'
  if (Math.abs(n) >= 100000000) return `${+(n / 100000000).toFixed(1)}亿`
  if (Math.abs(n) >= 10000) return `${+(n / 10000).toFixed(1)}万`
  return `${+n.toFixed(2)}`
}
const asArray = (v) => Array.isArray(v) ? v : v ? [v] : []

export function withEnergyTheme(source, { reducedMotion = false } = {}) {
  const option = { ...source }
  const series = asArray(source.series)
  const heat = series.some((s) => s.type === 'heatmap')
  const pie = series.find((s) => s.type === 'pie')
  const horizontal = asArray(source.yAxis).some((a) => a.type === 'category') && !heat
  const axis = (a, position) => ({
    ...a,
    nameTextStyle: { color: '#91abae', fontSize: 11, padding: [0, 0, 4, 0], ...a.nameTextStyle },
    nameGap: a.nameGap ?? 10,
    axisLine: { show: a.type === 'category', lineStyle: { color: '#28434b' }, ...a.axisLine },
    axisTick: { show: false, ...a.axisTick },
    splitNumber: 3,
    splitLine: { show: a.type !== 'category', lineStyle: { color: '#24404a', type: 'dashed', opacity: .6 }, ...a.splitLine },
    splitArea: { show: false },
    axisLabel: {
      color: '#91abae', fontSize: 12, margin: 12, hideOverlap: true,
      ...(a.type === 'value' ? { formatter: compactNumber } : {}), ...a.axisLabel,
      ...(horizontal && position === 'y' ? { width: 110, overflow: 'truncate' } : {}),
    },
  })
  for (const key of ['xAxis', 'yAxis']) {
    if (source[key]) option[key] = asArray(source[key]).map((a) => axis(a, key[0]))
  }
  option.color = source.color ?? ENERGY_COLORS
  option.backgroundColor = 'transparent'
  option.textStyle = { fontFamily: '"Noto Sans SC", "Microsoft YaHei", sans-serif', color: '#e4efed', ...source.textStyle }
  option.animation = !reducedMotion
  option.animationDuration = 650
  option.animationDurationUpdate = 350
  option.tooltip = {
    trigger: pie || source.geo || heat ? 'item' : 'axis',
    backgroundColor: '#102630', borderColor: '#3e686c', borderWidth: 1,
    textStyle: { color: '#e4efed', fontSize: 13 }, padding: [12, 16],
    confine: true, extraCssText: 'box-shadow: 0 8px 24px #0005; border-radius: 4px;',
    axisPointer: { lineStyle: { color: '#6aa8aa', type: 'dashed' }, shadowStyle: { color: '#65dec00b' } },
    ...source.tooltip,
  }
  if (source.xAxis || source.yAxis) option.grid = {
    left: 8, right: asArray(source.yAxis).length > 1 ? 16 : 32,
    top: source.legend ? 48 : 30, bottom: 10, containLabel: true, ...source.grid,
    ...(heat ? { left: 8, right: 60, top: 14, bottom: 6, containLabel: true } : {}),
  }
  if (source.legend) option.legend = {
    ...source.legend, top: source.legend.bottom === undefined ? 0 : undefined,
    itemWidth: 12, itemHeight: 6, itemGap: 18,
    textStyle: { color: '#a4bbbd', fontSize: 12 }, inactiveColor: '#456068',
    pageTextStyle: { color: '#a4bbbd' }, pageIconColor: '#65dec0',
  }
  if (source.visualMap) option.visualMap = {
    ...source.visualMap, itemWidth: 8, itemHeight: 120,
    textStyle: { color: '#91abae', fontSize: 11 }, inRange: { color: ['#112c35', '#26656a', '#63d3b8', '#e2c180'] }, calculable: false,
  }
  option.series = series.map((s, i) => {
    const color = option.color[i % option.color.length]
    const gradient = { type: 'linear', x: 0, y: 0, x2: horizontal ? 1 : 0, y2: horizontal ? 0 : 1, colorStops: [{ offset: 0, color }, { offset: 1, color: `${color}55` }] }
    let result = { ...s }
    if (s.type === 'bar') result = {
      ...result, barMaxWidth: horizontal ? 10 : 18, showBackground: horizontal,
      backgroundStyle: { color: '#79b4b10a', borderRadius: 2 },
      itemStyle: { color: gradient, borderRadius: horizontal ? [0, 2, 2, 0] : [2, 2, 0, 0], ...s.itemStyle },
    }
    if (s.type === 'line') result = {
      ...result, symbol: 'circle', symbolSize: 5, showSymbol: false,
      smooth: false, lineStyle: { width: 2.2, ...s.lineStyle },
      ...(s.areaStyle ? { areaStyle: { color: { ...gradient, x2: 0, y2: 1, colorStops: [{ offset: 0, color: `${color}35` }, { offset: 1, color: `${color}00` }] } } } : {}),
    }
    if (s.type === 'scatter') result.itemStyle = { color, opacity: .9, borderColor: `${color}aa`, borderWidth: 1, ...s.itemStyle }
    if (s.type === 'heatmap') result.itemStyle = { borderColor: '#0d2028', borderWidth: 2, borderRadius: 2, ...s.itemStyle }
    if (s.type === 'pie') result = {
      ...result, radius: ['43%', '68%'], center: [...DONUT_CENTER], label: { show: false }, labelLine: { show: false },
      itemStyle: { borderColor: '#0d2028', borderWidth: 3 }, emphasis: { scaleSize: 4, label: { show: false } },
    }
    if (s.markLine) result.markLine = { ...s.markLine, lineStyle: { color: '#d8b27a', type: 'dashed', opacity: .7 }, label: { color: '#d8b27a', fontSize: 10, rotate: 0, position: horizontal ? 'end' : 'insideEndTop', ...s.markLine.label } }
    if (s.markPoint) result.markPoint = { ...s.markPoint, symbol: 'circle', symbolSize: 7, itemStyle: { color: '#d8b27a' }, label: { show: false } }
    return result
  })
  if (pie) {
    const values = pie.data ?? [], total = values.reduce((n, d) => n + Number(d.value ?? 0), 0)
    option.legend = {
      type: 'scroll', orient: 'vertical', right: 0, top: 'center', itemWidth: 7, itemHeight: 7, itemGap: 15, icon: 'circle',
      textStyle: { color: '#aec2c2', fontSize: 12 }, pageTextStyle: { color: '#aec2c2' }, pageIconColor: '#65dec0',
      formatter: (name) => `${name}    ${Number(values.find((v) => v.name === name)?.value ?? 0).toLocaleString('zh-CN')}`,
    }
    // 数字与副标题组成同一个文本块，整体锚定圆心。
    // 旧版固定向下偏移 32px，在首页 147px 高的图表里会把“总量”推入圆环。
    option.graphic = [{ type: 'group', left: DONUT_CENTER[0], top: DONUT_CENTER[1], bounding: 'raw', silent: true, children: [
      { type: 'text', z: 10, silent: true, style: {
        text: `{value|${compactNumber(total)}}\n{caption|总量}`,
        align: 'center', verticalAlign: 'middle',
        rich: {
          value: { fill: '#e4efed', font: '500 24px monospace', lineHeight: 28, align: 'center' },
          caption: { fill: '#91abae', font: '11px sans-serif', lineHeight: 14, align: 'center' },
        },
      } },
    ] }]
  }
  return option
}
