import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// 避免引入 @types/node，仅为环境变量声明最小类型
declare const process: { env: Record<string, string | undefined> }

// 生成 build id：优先从环境变量读取，否则用构建时间戳
const buildId = process.env.VITE_BUILD_ID || new Date().toISOString().replace(/[:.]/g, '-')

// https://vitejs.dev/config/
// VITE_BASE：子路径部署时设置（如 VITE_BASE=/multiopus/ npm run build），默认根路径
// VITE_BUILD_ID：显式指定构建 ID；未指定时使用上面的时间戳
export default defineConfig({
  base: process.env.VITE_BASE || '/',
  plugins: [vue()],
  define: {
    __BUILD_ID__: JSON.stringify(buildId),
  },
})
