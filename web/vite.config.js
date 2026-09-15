import { fileURLToPath, URL } from 'node:url'
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// 第二阶段大屏：产物用相对路径，Flask 可直接托管 dist（离线演示不需要 CDN）
export default defineConfig({
  plugins: [vue()],
  base: './',
  resolve: {
    alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) },
  },
  server: {
    port: 5173,
    // lib/ 直接复用仓库里 dashboard/assets 的纯模块，需要允许读取工程外的文件
    fs: { allow: ['..'] },
    proxy: {
      '/api': { target: 'http://127.0.0.1:5000', changeOrigin: true },
    },
  },
  build: { outDir: 'dist', emptyOutDir: true },
})
