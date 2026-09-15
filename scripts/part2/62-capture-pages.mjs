// =============================================================================
// 62-capture-pages.mjs —— 大屏取证：抓每个图表的真实 option + 五页截图
// =============================================================================
// 抽验表的「大屏显示值」这一段就靠它。为什么不是看截图读数：
//   图表里的数字是画布像素，肉眼读既有误差、又不可复现；而
//   `web/src/components/EChart.vue` 把 ECharts 实例挂在容器的 `__echarts` 上
//   （作者留给 E2E 的口子），所以可以直接取 `getOption()` —— 那就是图表**实际渲染的那组数**。
//   截图仍然照出，作为「确实渲染了」的旁证与答辩材料。
//
// 用法（在仓库根目录执行，需要 web/node_modules 里的 playwright + 本机 Chrome）：
//   node scripts/part2/62-capture-pages.mjs
//   node scripts/part2/62-capture-pages.mjs http://192.168.88.131:5000
//   node scripts/part2/62-capture-pages.mjs --out docs/test/evidence/part2-2026-09-15/screenshots
//   node scripts/part2/62-capture-pages.mjs --no-shots          # 只取数据不截图
//
// 输出：<out>/NN-<page>.json（图表 option + 页面文本）与 <out>/png/NN-<page>.png
//       （编号命名与 docs/test/evidence/*/screenshots/ 的既有约定一致，
//        直接 `--out` 指向那个目录即可原地刷新截图）
// =============================================================================
import { createRequire } from 'node:module'
import { mkdirSync, writeFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const SELF = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(SELF, '..', '..')

const args = process.argv.slice(2)
const flagOut = args.indexOf('--out')
const shots = !args.includes('--no-shots')
const positional = args.filter((a, i) => !a.startsWith('--') && i !== flagOut + 1)
const BASE = positional[0] ?? process.env.PART2_BASE_URL ?? 'http://192.168.88.131:5000'
const OUT = resolve(
  flagOut >= 0 ? args[flagOut + 1] : join(REPO, 'runtime', 'page-capture'),
)

const require = createRequire(join(REPO, 'web', 'package.json'))
let chromium
try {
  ;({ chromium } = require('playwright'))
} catch (err) {
  console.error('[fail] 找不到 playwright —— 先在 web/ 下 npm install（脚本依赖 web/node_modules）')
  console.error(String(err))
  process.exit(1)
}

mkdirSync(OUT, { recursive: true })
if (shots) mkdirSync(join(OUT, 'png'), { recursive: true })

const PAGES = [
  ['home', '#/'],
  ['user', '#/user'],
  ['station', '#/station'],
  ['enterprise', '#/enterprise'],
  ['gov', '#/gov'],
].map(([name, hash], i) => [`${String(i + 1).padStart(2, '0')}-${name}`, name, hash])

// 在页面上下文里执行：优先用 EChart.vue 暴露的实例，退化到 Vue 组件 props
function extractInPage() {
  const trim = (v, n = 400) => (v == null ? null : String(v).slice(0, n))
  const charts = []
  for (const el of document.querySelectorAll('.chart')) {
    const chart = el.__echarts
    if (!chart) continue
    let opt
    try {
      opt = chart.getOption()
    } catch {
      continue
    }
    if (!opt || !opt.series) continue
    const series = (Array.isArray(opt.series) ? opt.series : [opt.series]).map((s) => ({
      name: s.name ?? null,
      type: s.type ?? null,
      data: trim(JSON.stringify(s.data ?? null), 4000),
      markLine: s.markLine ? trim(JSON.stringify(s.markLine)) : null,
      markPoint: s.markPoint ? trim(JSON.stringify(s.markPoint)) : null,
    }))
    const xa = Array.isArray(opt.xAxis) ? opt.xAxis[0] : opt.xAxis
    charts.push({
      className: el.className,
      series,
      xAxisData: trim(JSON.stringify(xa?.data ?? null), 2000),
      legend: opt.legend?.data ?? null,
    })
  }
  const texts = [...document.querySelectorAll('.kpi-card, .panel-heading, .footnote, .metric')]
    .map((n) => (n.innerText || '').replace(/\s+/g, ' ').trim().slice(0, 160))
    .filter(Boolean)
  return { charts, texts }
}

const browser = await chromium.launch({
  channel: process.env.PART2_CHANNEL ?? 'chrome',
  headless: true,
})
const page = await browser.newPage({
  viewport: { width: 1920, height: 1080 },
  locale: 'zh-CN',
})
const errors = []
page.on('pageerror', (e) => errors.push(String(e)))
page.on('console', (m) => m.type() === 'error' && errors.push(m.text()))

console.log(`===== 大屏取证：${BASE} =====`)
console.log(`  输出：${OUT}   截图：${shots ? '是' : '否'}`)
console.log()

const summary = {}
let failed = 0
for (const [slug, name, hash] of PAGES) {
  try {
    await page.goto(`${BASE}/${hash}`, { waitUntil: 'load', timeout: 60000 })
    await page.waitForSelector('.chart canvas', { timeout: 20000 })
    await page.waitForTimeout(2500) // 等慢接口回来把图表重绘完
    const dump = await page.evaluate(extractInPage)
    writeFileSync(join(OUT, `${slug}.json`), JSON.stringify(dump, null, 1), 'utf8')
    if (shots) {
      await page.screenshot({ path: join(OUT, 'png', `${slug}.png`) })
    }
    summary[name] = { charts: dump.charts.length, texts: dump.texts.length }
    console.log(`  ${name.padEnd(11)} 图表 ${dump.charts.length} 个，文本块 ${dump.texts.length} 个`)
  } catch (err) {
    failed++
    summary[name] = { error: String(err) }
    console.log(`  ${name.padEnd(11)} [fail] ${err}`)
  }
}

writeFileSync(join(OUT, 'console-errors.json'), JSON.stringify(errors, null, 1), 'utf8')
writeFileSync(join(OUT, 'summary.json'), JSON.stringify(summary, null, 1), 'utf8')

console.log()
console.log(`  控制台报错 ${errors.length} 条${errors.length ? '（见 console-errors.json）' : ''}`)
console.log(`===== 完成：${PAGES.length - failed}/${PAGES.length} 页取证成功 =====`)

await browser.close()
process.exit(failed === 0 && errors.length === 0 ? 0 : 1)
