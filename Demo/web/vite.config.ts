// @ts-nocheck
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { existsSync } from 'node:fs'
import { cp, readFile } from 'node:fs/promises'
import { extname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

// 避免引入 @types/node，仅为环境变量声明最小类型
declare const process: { env: Record<string, string | undefined> }

// 生成 build id：优先从环境变量读取，否则用构建时间戳
const buildId = process.env.VITE_BUILD_ID || new Date().toISOString().replace(/[:.]/g, '-')
const webDir = fileURLToPath(new URL('.', import.meta.url))
const docsDir = resolve(webDir, '../docs')

const contentTypes: Record<string, string> = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
}

function docsPlugin() {
  return {
    name: 'serve-and-copy-docs',
    configureServer(server) {
      server.middlewares.use('/docs', async (req, res, next) => {
        try {
          const requestPath = decodeURIComponent((req.url || '/').split('?')[0]);
          const relativePath = requestPath === '/' ? 'index.html' : `.${requestPath}`;
          const filePath = resolve(docsDir, relativePath);
          if (!filePath.startsWith(`${docsDir}/`)) {
            res.statusCode = 403;
            res.end('Forbidden');
            return;
          }
          const content = await readFile(filePath);
          res.setHeader('Content-Type', contentTypes[extname(filePath)] || 'application/octet-stream');
          res.end(content);
        } catch {
          next();
        }
      });
    },
    async closeBundle() {
      if (!existsSync(docsDir)) {
        throw new Error(
          `[docsPlugin] 文档目录不存在：${docsDir}。` +
          '本地构建请保持 Demo/docs 与 Demo/web 的相对位置；' +
          'Docker 构建请以 Demo/ 为构建上下文（见 Dockerfile 头部说明）。'
        );
      }
      await cp(docsDir, resolve(webDir, 'dist/docs'), { recursive: true });
    },
  }
}

// https://vitejs.dev/config/
// VITE_BASE：子路径部署时设置（如 VITE_BASE=/multiopus/ npm run build），默认根路径
// VITE_BUILD_ID：显式指定构建 ID；未指定时使用上面的时间戳
export default defineConfig({
  base: process.env.VITE_BASE || '/',
  plugins: [vue(), docsPlugin()],
  define: {
    __BUILD_ID__: JSON.stringify(buildId),
  },
})
