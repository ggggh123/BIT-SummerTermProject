// 页面级 E2E（P1-1）：打真实页面，不 mock 图表。
// 默认打虚拟机上的演示栈；也可指向本地：PART2_BASE_URL=http://localhost:5000 npx playwright test
// 默认使用系统已安装的 Chrome（channel: chrome），不下载 Playwright 自带浏览器。
import { defineConfig } from '@playwright/test'

const baseURL = process.env.PART2_BASE_URL ?? 'http://192.168.59.128:5000'

export default defineConfig({
  testDir: './e2e',
  timeout: 90_000,
  expect: { timeout: 20_000 },
  fullyParallel: false,
  workers: 1,
  reporter: [['line']],
  use: {
    baseURL,
    channel: process.env.PART2_CHANNEL ?? 'chrome',
    headless: true,
    viewport: { width: 1920, height: 1080 },
    locale: 'zh-CN',
  },
})
