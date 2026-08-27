# 前端缓存管理（基于 Build ID）

本文档说明 `Demo/web` 与其随构建发布的 `Demo/docs` 如何通过 **Build ID** 管理静态资源缓存，避免发版后浏览器仍使用旧版本资源。

---

## 1. 设计原则

- **入口 HTML 不缓存**：`index.html` 必须始终从服务端拉取最新版本，确保浏览器能拿到新的资源引用。
- **带版本戳的资源可长期缓存**：JS/CSS/WAV 等资源通过 Build ID 或文件 hash 做版本化，URL 变化时浏览器才会重新下载。
- **Build ID 统一可追踪**：优先使用 `git short hash`，无 git 时回退到构建时间戳；也可通过环境变量显式指定。

---

## 2. Demo/web（Vite + Vue3）

### 2.1 Build ID 注入

- 文件：`Demo/web/vite.config.ts`
- 通过 `define` 注入全局常量 `__BUILD_ID__`：
  - 优先读取 `VITE_BUILD_ID` 环境变量；
  - 未指定时使用构建时间戳（`YYYY-MM-DDTHH-mm-ss-SSSZ`）。

### 2.2 运行时资源加戳

- 文件：`Demo/web/src/Multiopus.vue`
- 发布端加载的多声道 WAV 素材（`4/6/8_Channel_ID.wav`）自动追加 `?v=<BUILD_ID>`：

  ```text
  /4_Channel_ID.wav?v=2026-08-20T03-23-08-282Z
  ```

- Vite 构建出的 JS/CSS 本身已带文件 hash（如 `index-D7CKTfQQ.js`），无需额外处理。

### 2.3 Nginx 缓存策略

- 文件：`Demo/web/nginx.conf`

| 资源类型 | Location 匹配 | Cache-Control |
|---|---|---|
| `index.html` | `= /index.html` | `no-cache, no-store, must-revalidate` |
| 带 hash 的 JS/CSS | `~* \.[a-f0-9]{8,}\.(js|css)$` | `public, immutable`（1 年） |
| WAV/图标/图片 | `~* \.(wav\|ico\|png\|svg\|...)$` | `public`（1 年） |
| 其他静态文件 | `~* \.(json\|woff2?\|ttf\|...)$` | `public`（7 天） |

### 2.4 Docker 构建参数

- 文件：`Demo/web/Dockerfile`
- 新增 `VITE_BUILD_ID` build arg，可在 CI/CD 中传入固定 Build ID：

  ```bash
  docker build --build-arg VITE_BUILD_ID=$(git rev-parse --short HEAD) -t agora-multichannel-web .
  ```

### 2.5 常用命令

```bash
cd Demo/web

# 本地开发（自动使用当前时间戳作为 build id）
npm run dev

# 为文档和 Web 使用同一个 Build ID 后再生产构建
BUILD_ID=$(git rev-parse --short HEAD)
BUILD_ID="$BUILD_ID" node ../docs/build.mjs
VITE_BUILD_ID="$BUILD_ID" npm run build

# Docker 构建并指定 build id
docker build --build-arg VITE_BUILD_ID=abc123 -t agora-multichannel-web .
```

---

## 3. Demo/docs（静态 HTML）

### 3.1 Build 脚本

- 文件：`Demo/docs/build.mjs`
- 功能：
  1. 生成 Build ID（优先 `BUILD_ID` 环境变量，其次 `git rev-parse --short HEAD`，最后时间戳）。
  2. 为三份 HTML 文件中的 `assets/style.css` 与 `assets/i18n.js` 追加 `?v=<BUILD_ID>`。
  3. 在每份 HTML 的 `<head>` 中注入/更新 `<meta name="build-id" content="...">`。

### 3.2 常用命令

```bash
cd Demo/docs

# 默认生成 build id 并打戳
node build.mjs

# 显式指定 build id
BUILD_ID=abc123 node build.mjs
```

执行后 HTML 示例：

```html
<meta name="build-id" content="2026-08-20T03-20-31-451Z">
<link rel="stylesheet" href="assets/style.css?v=2026-08-20T03-20-31-451Z">
<script src="assets/i18n.js?v=2026-08-20T03-20-31-451Z"></script>
```

---

## 4. 部署建议

1. **每次发版前使用同一个 Build ID 生成 docs 并构建 Web**：
   - 在仓库根目录执行 `BUILD_ID=$(git rev-parse --short HEAD)`，再运行 `BUILD_ID="$BUILD_ID" node Demo/docs/build.mjs`。
   - 随后在 `Demo/web` 运行 `VITE_BUILD_ID="$BUILD_ID" npm run build`；构建插件会将 docs 复制到 `dist/docs/`。

2. **优先使用 git commit hash**：
   - 时间戳虽然能确保唯一，但无法直接对应代码版本；git hash 更利于问题排查。

3. **避免将带 Build ID 的资源 URL 当作“源文件”长期不动**：
   - Docs 的 `build.mjs` 会修改 HTML 文件，应在部署流水线中执行，而不是手动维护；不能只构建 Web 而跳过该步骤。

---

## 5. 相关文件清单

```text
Demo/web/
  ├── vite.config.ts          # 注入 __BUILD_ID__，并将 ../docs 复制到 dist/docs/
  ├── src/vite-env.d.ts       # 声明 __BUILD_ID__
  ├── src/Multiopus.vue       # WAV URL 加 ?v=<BUILD_ID>
  ├── nginx.conf              # 分层缓存头
  └── Dockerfile              # 接收 VITE_BUILD_ID build arg

Demo/docs/
  ├── build.mjs               # 静态 HTML 资源加戳脚本
  ├── index.html              # 已打戳（示例）
  ├── web-sdk-multichannel.html
  └── linux-sdk-multichannel.html
```
