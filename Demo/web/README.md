# Web 音频多声道（multiopus）验证 Demo

基于 Agora Web SDK（`agora-rtc-sdk-ng@4.20.1-200`，多声道定制版）的音频多声道收发验证工程，技术栈 Vue3 + Vite + Naive UI。需求背景见 `Spec/web_sdk_audio_multichannel_spec.md`。

[English Documentation](README.en.md)

## 功能说明

- **多声道发布（Publisher）**：选择内置多声道 WAV 素材（4/6/8 声道的 Channel ID 测试音），经 `fetch` → `AudioContext.decodeAudioData` → 显式 `channelCount` 的 `MediaStreamDestination` → `AgoraRTC.createCustomAudioTrack({ mediaStreamTrack, channelCount })` 发布为 OPUSMC 多声道流，素材循环播放。
- **多声道订阅（Subscriber）**：输入频道 / 本端 uid(+token) / 目标 uid（subTo），订阅目标远端音频并播放。
- **逐声道音量条**：订阅后每 300ms 调用 `getVolumeLevels()` 渲染各声道实时音量。
- **codec 判定**：hook `RTCPeerConnection.getStats()` 读取底层 inbound codec（`opus` / `multiopus`），800ms 轮询、最多 6 次。
- **声道选择播放**：`selectChannels(channels, mode)` 对远端音轨选择任意声道组合，支持 `surround` / `stereo` / `mono` 三种渲染模式，用于逐声道试听核对 Channel ID 素材。
- **双标签页角色控制**：通过 URL 参数 `channel` / `uid` / `subTo` 区分发布端与订阅端标签页。
- **双 UI 布局**：默认（`?demo=1`）为本工程布局；`?demo=2` 为参考 quick-demo-multiopus 原版的布局（自由文本音频地址、确定(环绕声/双声道/单声道)按钮、简洁音量条），两个 demo 头部有快捷链接互相切换。
- **中英文切换**：右上角按钮切换界面语言（默认跟随浏览器语言，选择记忆在 localStorage）。

关键前提：`AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)` 已在 `src/Multiopus.vue` 顶部、`createClient` 之前设置（代码内有注释说明，创建 client 后再设置不生效）。

## 环境要求

- **浏览器：Chrome 96+，推荐 110+**。声道选择依赖 Insertable Streams（绕开 Chrome WebAudio 缺陷 issue 1116042）。
- **Safari / Firefox 不支持**：加入多声道频道行为未定义（可能无声或仅立体声）。
- **仅偶数声道**：Web 端仅支持 4/6/8 声道解码（3/5/7 不适用，16 声道及以上不支持）。
- Node.js 18+（本地开发）。

## 安装与启动

```bash
cd Demo/web
npm install
npm run dev   # 默认 http://localhost:5173/
```

其他脚本：`npm run build`（vue-tsc 类型检查 + 构建到 `dist/`）、`npm run preview`。

> `agora-rtc-sdk-ng@4.20.1-200` 已发布在公共 npm，可直接安装。若拉取失败，可向 Agora 索取该定制版离线包，解压后将 `AgoraRTC_N-production.js` 与 `rtc-sdk_en.d.ts` 放入本地目录，改写 `package.json` 为 `"agora-rtc-sdk-ng": "file:./vendor/agora-rtc-sdk-ng"` 形式引入。

## Docker 部署

```bash
# 在 Demo/ 目录执行（构建上下文需同时包含 web/ 与 docs/，构建时 docs 会被复制进 dist/docs）
cd Demo
docker build -f web/Dockerfile -t agora-multichannel-web .
docker run --rm -p 8080:80 agora-multichannel-web
# 打开 http://localhost:8080/
```

多阶段构建：`node:20-alpine` 内执行 `npm ci` + `npm run build`，产物由 nginx 托管
（`nginx.conf` 已配置 SPA 回退与静态资源 gzip）。App ID / token / uid 均在页面内填写，
构建期无需任何配置。注意：在非 localhost 的 http 地址下浏览器可能限制媒体相关能力，
生产环境建议配置 HTTPS。

## 双标签页自收发验证

Agora token 与 uid 绑定：两个标签页需各自 uid 的 token（或在 Agora 控制台生成 uid=0 的通配 token，两页共用）。无 App Certificate 的项目 token 可留空。

1. 打开页面，填写「频道」「本端 UID」「目标 UID (subTo)」。页面下方两个链接按当前填写的 uid 生成角色 URL：
   - **打开发布端**：`?channel=<频道>&uid=<本端UID>`
   - **打开订阅端**：`?channel=<频道>&uid=<目标UID>&subTo=<本端UID>`
2. 点击「打开发布端」→ 新标签页中粘贴发布端 uid 对应的 token → 「加入频道」→ 选择素材（如 `8_Channel_ID.wav`，声道数自动联动为 8）→ 「发布音频」。
3. 回到第一页点击「打开订阅端」→ 新标签页中粘贴订阅端 uid 对应的 token → 「加入频道」→ 「订阅检测」。
4. 订阅端结果面板显示逐声道音量条与 codec 判定；在「声道选择」输入框填声道序号（如 `0,1`），点 surround / stereo / mono 试听。

URL 参数一览（均优先于 localStorage 回填值）：

| 参数 | 含义 |
| --- | --- |
| `channel` | 频道名 |
| `uid` | 本标签页加入用的 uid（决定角色） |
| `subTo` | 要订阅的目标 uid（订阅端使用） |
| `token` | 本端 uid 对应的 token（也可在页面内填写） |

## 配合 Linux demo 做端到端验证

1. Linux 端（见 `Demo/linux/` 与 `Spec/linux_sdk_audio_multichannel_spec.md`）以 8 声道 OPUSMC 向频道发布音频，记下其 uid。
2. 本 demo 以订阅端角色打开：`?channel=<同一频道>&uid=<web端uid>&subTo=<Linux端uid>`，填入 token 后加入频道、订阅检测。
3. 预期：音量条出现 **8 路**，codec 判定为 **multiopus**，远端音轨 channelCount（信令）显示 8。
4. 用「声道选择」逐路试听（如 `0`、`1` … `7`），核对 Channel ID 内容与 Linux 端声道布局映射（T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R）一致、无串声道、无杂音。

注意：频道内应保证所有端都支持多声道。Linux 发送时若有不支持端加入，发送端会回退普通 Opus（Web 端观测到 codec 变为 opus、声道数下降）；Web 发送时不回退，不支持端无声。

## 判定方法

| 判据 | 多声道生效 | 未生效 |
| --- | --- | --- |
| `getVolumeLevels()` 返回数组长度 | = 实际声道数（4/6/8） | 1（单声道） |
| getStats inbound codec | `multiopus` | `opus` |
| 远端音轨 `channelCount`（信令 pt 值） | 4/6/8 | — |

demo 结果面板按「音量数组长度为 4/6/8，或 codec = multiopus」给出多声道结论；普通单/双声道不会被判定为多声道。

## 声道选择模式说明

`remoteAudioTrack.selectChannels(channels, mode)`，`channels` 为声道序号数组（从 0 开始）：

- `surround`（默认）：按标准多声道协议输出所选声道。
- `stereo`：所选声道中 0,2,4,6… 混入左声道、1,3,5,7… 混入右声道，输出立体声。例如选 `0,1,2,3` 时 0、2 在左，1、3 在右。
- `mono`：所选声道全部混为单声道（输出为立体声容器）。

逐声道试听：输入框填单个声道号（如 `3`）选 mono，或填一对（如 `2,3`）选 stereo。

## 常见问题

- **订阅多声道流只有单/双声道？** 检查是否在 `createClient` 之前设置了 `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)`；未开启则按普通音频处理，只能解出单/双声道。
- **token 报错 / 加入失败？** token 与 uid 绑定，两个标签页必须使用各自 uid 的 token（或 uid=0 通配 token）。demo 内 `APP_ID` 为明文演示凭证，正式使用请替换并在自己的服务端签发 token。
- **素材是 44.1kHz？** 4/6 声道素材为 44.1kHz/16bit，8 声道素材为 48kHz/24bit；经 Web Audio 图解码后统一按协商的 48kHz 编码（多声道编解码仅 48kHz），不影响验证。
- **浏览器不支持？** Safari/Firefox 不支持多声道（行为未定义）；Chrome 94/95 相关接口默认关闭，96 起正式支持，推荐 110+。
- **频道里进来不支持多声道的端？** Linux 发送时发送端回退、多声道失效（Linux 端可用 `che.audio.codec_unfallback` 强制不回退）；Web 发送时不回退且该端无声。
- **最多几声？** 8 声道；仅偶数声道；16 声道及以上不支持。

## 目录结构

```
Demo/web/
├─ index.html
├─ package.json            # agora-rtc-sdk-ng 固定 4.20.1-200
├─ vite.config.ts
├─ tsconfig.json / tsconfig.app.json / tsconfig.node.json
├─ public/                 # 4/6/8_Channel_ID.wav 素材
└─ src/
   ├─ main.ts              # createApp + mount
   ├─ App.vue              # NNotificationProvider 包裹，按 ?demo= 查询参数切换两个布局
   ├─ Multiopus.vue        # Live Demo 1（默认）：全部逻辑与 UI（发布/订阅/声道选择/codec 判定）
   ├─ MultiopusV2.vue      # Live Demo 2（?demo=2）：参考 quick-demo-multiopus 原版布局
   ├─ style.css
   └─ vite-env.d.ts
```
