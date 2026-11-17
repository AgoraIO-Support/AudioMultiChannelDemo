# Web 音频多声道（multiopus）验证 Demo

基于 Agora Web SDK（`agora-rtc-sdk-ng@4.24.10`，主线版本）的音频多声道收发验证工程，技术栈 Vue3 + Vite + Naive UI。需求背景见 `Spec/web_sdk_audio_multichannel_spec.md`。

[English Documentation](README.en.md)

## 功能说明

- **多声道发布（Publisher）**：选择内置多声道 WAV 素材（4/6/8 声道的 Channel ID 测试音），经 `fetch` → `AudioContext.decodeAudioData` → 显式 `channelCount` 的 `MediaStreamDestination` → `AgoraRTC.createCustomAudioTrack({ mediaStreamTrack, channelCount })` 发布为 OPUSMC 多声道流，素材循环播放。
- **多声道订阅（Subscriber）**：输入频道 / 本端 uid(+token) / 目标 uid（subTo），订阅目标远端音频并播放。
- **逐声道音量条**：订阅后每 300ms 调用 `getVolumeLevels()` 渲染各声道实时音量。
- **codec 判定**：hook `RTCPeerConnection.getStats()` 读取底层 inbound codec（`opus` / `multiopus`），订阅期间每 800ms 持续轮询（可观测协商回退）。
- **声道选择播放**：`selectChannels(channels, mode)` 对远端音轨选择任意声道组合，支持 `surround` / `stereo` / `mono` 三种渲染模式，用于逐声道试听核对 Channel ID 素材。
- **奇数声道修正补丁（`src/oddChannelFix.ts`）**：订阅 3/5/7 奇数声道流时，SDK 会按 N+1 偶数声道误解码（末声道为幻影复制、误判多声道 ✓）；补丁在运行时检测「末两声道逐样本恒等」签名，命中后修正音量显示声道数、`channelCount` 与播放输出（丢弃幻影声道），并弹窗提示。**三个 Live Demo 操作区都有「奇数声道补丁」开关（默认开，三页共享并记忆）**：订阅中关掉会立即撤销补丁恢复 SDK 原始行为（音量条变回 N+1 路、末两声道恒等），打开则重新检测修正，便于对比验证。原理与限制见 `Docs/audio_multichannel_test_summary.md` §4.2。
- **视频 + 多声道音频同时订阅（Live Demo 3）**：`?demo=3` 打开。输入频道 / 本端 uid(+token) / 目标 uid，一键同时订阅目标远端的多声道音频与视频：视频渲染到页面视频区，音频沿用 Live Demo 1 的逐声道音量条、codec 判定（音频 / 视频 codec 均显示）与声道选择播放；订阅期间对端补发视频或音频会自动补订。主要用于配合 Linux MKV demo（`sample_send_mkv_multiopus`，1 路视频 + 8 声道 OPUSMC 音频）做端到端验证，也可订阅 Live Demo 1/2 发布的纯音频流（此时视频区显示未发布）。
- **双标签页角色控制**：通过 URL 参数 `channel` / `uid` / `subTo` 区分发布端与订阅端标签页。
- **多 Live Demo 布局**：默认（`?demo=1`）为本工程布局；`?demo=2` 为参考 quick-demo-multiopus 原版的布局（自由文本音频地址、确定(环绕声/双声道/单声道)按钮、简洁音量条；该布局无 codec 判定 / 结果面板，也无 subTo 输入——自动订阅最新发布者或首个有音频的远端）；`?demo=3` 为视频 + 多声道音频同时订阅布局。各 demo 头部有快捷链接互相切换。
- **中英文切换**：右上角按钮切换界面语言（默认跟随浏览器语言，选择记忆在 localStorage）。

关键前提：三个 Live Demo 组件（`src/Multiopus.vue` / `src/MultiopusV2.vue` / `src/MultiopusV3.vue`）均在 `createClient` 配置中传入 `multiopus: true`，即 `AgoraRTC.createClient({ mode: "rtc", codec: "vp8", multiopus: true })`。

## 环境要求

- **浏览器：Chrome 96+，推荐 110+**。声道选择依赖 Insertable Streams（绕开 Chrome WebAudio 缺陷 issue 1116042）。
- **Safari / Firefox 不支持**：加入多声道频道行为未定义（可能无声或仅立体声）。
- **仅偶数声道**：Web 端仅支持 4/6/8 声道解码；3/5/7 声道流**不报错但会被误解码**为 N+1 偶数声道（mono 子流当 stereo 解，末声道复制出幻影声道，界面误判多声道 ✓），结果不可信（2026-10-12 实测，见 `Docs/audio_multichannel_test_summary.md` §4.2）；16 声道及以上不支持。
- Node.js 18+（本地开发）。

## 安装与启动

```bash
cd Demo/web
npm install
npm run dev   # 默认 http://localhost:5173/
```

其他脚本：`npm run build`（vue-tsc 类型检查 + 构建到 `dist/`）、`npm run preview`。

> 自 `agora-rtc-sdk-ng@4.24.10` 起，音频多声道功能已合并进入 Web SDK 主线版本。`package.json` 固定使用 `4.24.10`，执行 `npm install` 即可从 npm registry 安装，无需再准备多声道定制版本地 tgz 包。

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
| `appId` | 覆盖页面默认 / 已存（localStorage）的 App ID |

## 配合 Linux demo 做端到端验证

1. Linux 端（见 `Demo/linux/` 与 `Spec/linux_sdk_audio_multichannel_spec.md`）以 8 声道 OPUSMC 向频道发布音频，记下其 uid。
2. 本 demo 以订阅端角色打开：`?channel=<同一频道>&uid=<web端uid>&subTo=<Linux端uid>`，填入 token 后加入频道、订阅检测。
3. 预期：音量条出现 **8 路**，codec 判定为 **multiopus**，远端音轨 channelCount（信令）显示 8。
4. 用「声道选择」逐路试听（如 `0`、`1` … `7`），核对 Channel ID 内容与 Linux 端声道布局映射（T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R）一致、无串声道、无杂音。

**MKV 音视频端到端（Live Demo 3）**：Linux 端运行
`sample_send_mkv_multiopus --channelId <频道> --token <token> --userId <uid> --videoFile public/VictoriaMulti.mkv`
发布 1 路视频 + 8 声道音频（4xstereo 布局）；本 demo 以
`?demo=3&channel=<同一频道>&uid=<web端uid>&subTo=<Linux端uid>` 打开，加入频道后点「订阅音视频」。
预期：视频区正常出图，音频 8 路音量条、音频 codec 为 multiopus、视频 codec 为 h264。

注意：频道内应保证所有端都支持多声道。Linux 发送时若有不支持端加入，发送端会回退普通 Opus（Web 端观测到 codec 变为 opus、声道数下降）；Web 发送时不回退，不支持端无声。

### 集成注意：远端停流时的重复退订

`user-unpublished` 与 `user-left` 可能在很短时间内先后到达。如果两个回调都进入 `await client.unsubscribe(...)`，第二次调用可能因目标用户已经离开而抛出 `INVALID_REMOTE_USER`。这是**集成层的异步清理竞态**，不是多声道解码异常；SDK 拒绝退订一个已经不在频道的用户属于预期行为。

Live Demo 2 的集成处理如下：

- 使用 `unsubscribing` 做 in-flight 防重，先保存本次退订目标并同步清理订阅状态，再执行 SDK 调用，避免另一个远端事件重复进入。
- 仅忽略目标用户已经离开的 `INVALID_REMOTE_USER` 竞态，其他退订错误继续抛出，不在全局吞掉所有错误。
- 退订完成前禁止重新订阅和离开频道，避免旧退订与新操作叠加。

业务集成验收时，应同时检查音量与订阅状态正确清理、没有未处理 Promise 异常，并在 Sender 重启后重新订阅确认媒体恢复，而不能只看 UI 是否清空。

## 配合 macOS sender 做端到端验证

1. macOS 端（`Demo/macOS/MultiChannelAudioSender`，需 AgoraRtcEngine_macOS ≥ 4.5.3）以主播角色发布内置
   4/6/8 声道 Channel ID WAV；支持命令行自动化：
   `./MultiChannelAudioSender -autoChannel <频道> -autoUid <uid> -autoWav 0 -autoJoin YES -autoPublish YES`。
2. 本 demo 以订阅端角色打开：`?channel=<同一频道>&uid=<web端uid>&subTo=<macOS端uid>`，加入频道后点「订阅检测」。
3. 预期：音量条出现对应声道数（4/6/8）且各声道均有内容级峰值，codec 判定为 multiopus。
   注意：macOS **4.5.2** 发送时仅前 2 声道有内容（已知输入钳制），**≥ 4.5.3** 全声道正常；
   版本实测矩阵见 `Docs/audio_multichannel_test_summary.md` §4.6。

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

- **「打开发布端 / 打开订阅端」两个链接是干什么的？** 双标签页自验证的入口：「打开发布端」以本端 UID 开新标签页加入并发布素材；「打开订阅端」以目标 UID 开新标签页、订阅本端 UID（subTo）的流做逐声道检测。两个链接都会把当前频道名带入新标签页（页面上链接上方也有同样说明）。
- **订阅 Linux 发送的 8 声道流时，没发声的声道音量柱也显示 ~10%（实测 4.6~12%）？** 两个原因叠加：① 音量表是 dB 宽量程刻度——`getVolumeLevels()` 的实现为 `(dBFS_RMS + 100) / 100`，只有精确数字零才显示 0%，-90dB 的不可闻残余就显示 ~10%（该实现无 Linux/Web 分支，读的是真实解码 PCM）；② Linux 发送用的 16bit 素材静音段带 -84dB dither，而 Web 发布用的 24bit 素材静音段是精确数字零，所以 web→web 显示 0（已用 24bit 素材在同一 Linux 发送端做对照实验：底数消失、min=0，确认素材 dither 为根源）。实测编码/传输/解码链路与最终播放 PCM 均无泄漏（详见 `Docs/audio_multichannel_test_summary.md` §4.5）。逐声道内容核对请以「声道选择 + 试听」为准；读音量柱时记住 ~10% ≈ -90dB ≈ 无声。

- **订阅多声道流只有单/双声道？** 检查是否在 `createClient` 配置中传入了 `multiopus: true`；未开启则按普通音频处理，只能解出单/双声道。
- **token 报错 / 加入失败？** token 与 uid 绑定，两个标签页必须使用各自 uid 的 token（或 uid=0 通配 token）。demo 内 `APP_ID` 为明文演示凭证，正式使用请替换并在自己的服务端签发 token。
- **素材是 44.1kHz？** 4/6 声道素材为 44.1kHz/16bit，8 声道素材为 48kHz/24bit；经 Web Audio 图解码后统一按协商的 48kHz 编码（多声道编解码仅 48kHz），不影响验证。
- **浏览器不支持？** Safari/Firefox 不支持多声道（行为未定义）；Chrome 94/95 相关接口默认关闭，96 起正式支持，推荐 110+。
- **频道里进来不支持多声道的端？** Linux 发送时发送端回退、多声道失效（Linux 端可用 `che.audio.codec_unfallback` 强制不回退）；Web 发送时不回退且该端无声。
- **最多几声？** 8 声道；仅偶数声道；16 声道及以上不支持。

## 目录结构

```
Demo/web/
├─ index.html
├─ package.json            # agora-rtc-sdk-ng 固定 4.24.10
├─ vite.config.ts
├─ tsconfig.json / tsconfig.app.json / tsconfig.node.json
├─ public/                 # 4/6/8_Channel_ID.wav 素材
└─ src/
   ├─ main.ts              # createApp + mount
   ├─ App.vue              # NNotificationProvider 包裹，按 ?demo= 查询参数切换三个布局
   ├─ Multiopus.vue        # Live Demo 1（默认）：全部逻辑与 UI（发布/订阅/声道选择/codec 判定）
   ├─ MultiopusV2.vue      # Live Demo 2（?demo=2）：参考 quick-demo-multiopus 原版布局
   ├─ MultiopusV3.vue      # Live Demo 3（?demo=3）：视频 + 多声道音频同时订阅（配合 Linux MKV demo）
   ├─ oddChannelFix.ts     # 奇数声道（3/5/7）误解码修正补丁，三个 Live Demo 共用
   ├─ style.css
   └─ vite-env.d.ts
```
