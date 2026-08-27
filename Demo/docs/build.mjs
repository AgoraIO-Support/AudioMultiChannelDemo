#!/usr/bin/env node
/**
 * Demo/docs 前端缓存管理脚本
 *
 * 作用：
 * 1. 生成一个 build id（优先 BUILD_ID 环境变量，否则尝试 git short hash，最后用时间戳）。
 * 2. 给所有 HTML 文件中的 assets/style.css 与 assets/i18n.js 追加 ?v=<buildId> 查询串。
 * 3. 在每个 HTML 的 <head> 中注入/更新 <meta name="build-id" content="...">。
 *
 * 用法：
 *   node Demo/docs/build.mjs
 *   BUILD_ID=abc123 node Demo/docs/build.mjs
 */

import { readFile, writeFile } from 'node:fs/promises';
import { execSync } from 'node:child_process';
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

process.chdir(dirname(fileURLToPath(import.meta.url)));

const files = [
  'index.html',
  'linux-sdk-multichannel.html',
  'web-sdk-multichannel.html',
];

function generateBuildId() {
  if (process.env.BUILD_ID) {
    return process.env.BUILD_ID;
  }
  try {
    return execSync('git rev-parse --short HEAD', { encoding: 'utf8' }).trim();
  } catch {
    return new Date().toISOString().replace(/[:.]/g, '-');
  }
}

const buildId = generateBuildId();
console.log(`[docs-build] build id: ${buildId}`);

for (const file of files) {
  let html = await readFile(file, 'utf8');

  // 给 assets/style.css 与 assets/i18n.js 加缓存戳，同时覆盖旧的 ?v=...
  html = html.replace(
    /(["'])assets\/(style\.css|i18n\.js)(?:\?v=[^"']*)?\1/g,
    `$1assets/$2?v=${buildId}$1`
  );

  // 注入/更新 build-id meta 标签
  const metaTag = `<meta name="build-id" content="${buildId}">`;
  if (html.includes('<meta name="build-id"')) {
    html = html.replace(
      /<meta name="build-id" content="[^"]*">/,
      metaTag
    );
  } else {
    html = html.replace('</head>', `  ${metaTag}\n  </head>`);
  }

  await writeFile(file, html, 'utf8');
  console.log(`[docs-build] stamped ${file}`);
}

console.log('[docs-build] done');
