# Web SDK 音频多声道收发 产品需求文档（PRD 初稿）

## Web SDK 音频多声道（发布/订阅/声道选择播放）需求

> 摘要：客户多声道项目场景下，Web SDK（agora-rtc-sdk-ng）需要支持音频多声道能力：订阅端接收 Linux SDK 等平台发来的 OPUSMC 多声道流（最高 8 声道），按业务需要选择任意声道组合播放（surround/stereo/mono）并逐声道获取音量；发布端可将多声道素材（如 8 声道 WAV）以自定义音轨发布到频道。能力通过 `ENABLE_MULTIOPUS_CODEC` 开启，依赖 Web VOS 服务端配合与 Chrome 96+（推荐 110+），Safari/Firefox 不支持。该能力已在 Web SDK v4.20.1-100 与定制版 4.20.1-200 交付验证（配套 quick-demo-multiopus 验证 demo——已于 2026-10 移除，现由本仓库 `Demo/web/` 继承），本文档将其能力事实、行为边界与验收口径收敛为需求基线。与 Linux 侧发送/布局映射的配合见《Linux SDK 音频多声道收发 产品需求文档》（`Spec/linux_sdk_audio_multichannel_spec.md`）。

---

## 一、文档基础信息

| 项目 | 内容 |
| --- | --- |
| PM | 待补充 |
| TO | 待补充 |
| PMO | 待补充 |
| 需求 Jira | 背景单：CSD-74687（音画不同步）、ASS-2272（Linux SDK 特殊版申请）；需求 Jira 待补充 |
| 需求版本 | v0.4 |

---

## 二、需求背景

### 2.1 客户原始需求

客户的多声道链路中，Web 端承担订阅监听与验证角色：Linux 服务器端注入 8 声道音频流后，Web 端（Chrome）订阅该流、确认实际解码声道数、逐声道试听与查看音量；同时 Web SDK 本身也具备多声道发布能力（多声道 WAV 素材 → 自定义音轨 → 发布）。客户印度研发团队在联调中使用订阅端验证 Linux 发送的 4×双声道 → 8 声道布局映射正确性（背景见 Linux 侧 spec 需求点9）。

| 内容项 | 详情 | 备注 |
| --- | --- | --- |
| 客户(VID) | （客户名称已隐去；VID 待补充） | |
| 需求 Reporter | 客户印度研发团队（企业微信沟通记录）；Agora 侧 CSD/ASS 流程 | |
| 需求业务线 | 广电级音视频传输 / 云端制作 | 依据交付记录推断，待客户确认 |
| 客户业务场景 | Web（Chrome）订阅 Linux SDK 发送的 4/6/8 声道音频流，按声道选择播放并逐声道观测音量，用于制作监听与收发验证；按需从 Web 端发布多声道音频 | |
| 客户碰到的问题 | 默认 Web SDK 订阅多声道流会被按单/双声道处理（不开启 multiopus 时）；声道选择播放受浏览器 WebAudio 缺陷限制（Chrome bug 1116042）需绕行 Insertable Streams；Safari/Firefox 行为未定义；联调早期曾因发送端 layout 错误导致接收端杂音（根因与结论见 Linux 侧 spec，2025-04-14 已解决） | |
| 该问题解决对客户的价值 | Web 端成为多声道链路的监听/验证入口：可确认声道数、逐声道试听、逐声道音量观测，支撑客户 8 声道（4 个独立立体声对）内容的端到端验收 | |
| 当前客户在用方案 | quick-demo-multiopus（Vue3 + Vite，已于 2026-10 移除，现行为 `Demo/web/`）：`ENABLE_MULTIOPUS_CODEC` 开启后，发布端 fetch 多声道 WAV → Web Audio 解码 → `createCustomAudioTrack({channelCount})` 发布；订阅端 subscribe → `selectChannels` → `getVolumeLevels` 逐声道音量条 | 见 6.4 现有 API 关联梳理 |
| 客户做了哪些尝试 | 双标签页自收发验证（Publisher/Subscriber 不同 uid + token）；通过 hook RTCPeerConnection 读取 getStats 判定实际 codec（opus/multiopus）；iOS App 订阅对比（`far_in_48000hz_2ch.wav`） | 来源：demo 代码与沟通记录 |
| 是否有竞品在 pk 中 | 无明确竞品信息 | |
| 竞品方案 | 无明确竞品线路 | |
| 客户在用版本 | Web SDK v4.20.1-100（agora-rtc-sdk-ng@4.20.1-100）；多声道定制版 4.20.1-200（有专项 Test Report） | 来源：Web 多声道设计文档、交付记录 |
| 当前使用的平台/语言/框架 | Web（Chrome），JavaScript/TypeScript；demo 技术栈 Vue3 + Vite + Naive UI | |
| 期望交付版本 | 4.20.1-200 或后续包含 multiopus 能力的版本 | |
| 期望交付的平台/框架 | Web 浏览器（Chrome 96+，推荐 110+） | |

### 2.2 当前产品方案

| 方案 | API | 功能描述 | 方案定位 | 方案成熟度 | 方案支持当前需求存在的 Gap |
| --- | --- | --- | --- | --- | --- |
| Web 端多声道订阅与声道选择 | `ENABLE_MULTIOPUS_CODEC`、远端音轨 `channelCount`、`selectChannels(channels, mode)`、`getVolumeLevels(channels?)` | 订阅 OPUSMC 多声道流，选择任意声道组合按 surround/stereo/mono 播放，逐声道音量 | 定制版能力 | 已在 4.20.1-100/200 验证交付 | 仅 Chrome；仅偶数声道解码；最多 8 声道 |
| Web 端多声道发布 | `createCustomAudioTrack({mediaStreamTrack, channelCount})`（参考代码中亦示 `createMultiAudioTrack`） | 将多声道音频（如 8ch WAV 解码后的 AudioBuffer 源）发布为 OPUSMC 流，SDK 按 channelCount 生成 custom_payload_type | 定制版能力 | 已验证 | 需业务侧自备多声道素材并保证声道布局约定 |
| Web VOS 服务端多声道配合 | —（服务端） | ORTC multiopus 能力协商（payload type 123、fmtp channel_mapping/coupled_streams/num_streams） | 线上已生效 | 已验证 | 频道内不支持端的行为差异（见需求点7） |
| Linux 端多声道发送（对端来源） | 见 Linux 侧 spec | Web 订阅的多声道流主要来源 | 已交付 | 已验证 | 布局映射由发送侧保证 |

---

## 三、同类历史需求

- 同一多声道项目的产品线落地（2024-03 设计）：Linux SDK 侧（ExT-1485178709）与 Web SDK 侧（ExT-1485178492）设计文档；Native SDK 同步支持。
- 客户交付线：Web SDK 4.20.1-200 多声道定制版 Test Report（交付记录 ExT-1574700707）；Linux 侧 v4.2.32 / v4.4.30.130（详见 Linux 侧 spec 第三章）。
- 2025-04 联调：客户侧 4×双声道 → 8 声道布局映射问题排查（杂音根因：发送端交错/平面读取不一致），Web 订阅端为验证手段之一；2025-04-14 客户确认解决。

## 四、需求类型和价值

| 内容项 | 详情 | 备注 |
| --- | --- | --- |
| 是否通用需求 | 否（客户交付驱动）；能力本身可复用于有多声道监听/互动诉求的 Web 场景 | |
| 需求类型 | 新需求（功能落地与验收基线收敛） | |
| 需求价值 | 客户：Web 端完成多声道流的订阅监听、逐声道验证与按需发布，补齐端到端多声道链路的 Web 入口；产品线：Web SDK 与 Linux/Native 拉齐多声道能力 | |

---

## 五、竞品分析 Competitive Analysis

无明确竞品线路（客户需求，材料中无竞品 PK 信息）。

| 方案 | API | 功能描述 | 方案定位 | 方案成熟度 | 方案支持当前需求存在的 Gap |
| --- | --- | --- | --- | --- | --- |
| Zego | — | 无明确竞品线路 | — | — | — |
| TRTC | — | 无明确竞品线路 | — | — | — |
| 火山 | — | 无明确竞品线路 | — | — | — |

---

## 六、产品方案

### 6.1 功能性需求

| 内容项 | 描述 | 备注 |
| --- | --- | --- |
| API 接口（描述性语言） | 能力开关：`AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)`（创建 client 前）。发布：自定义音轨创建接口携带 `channelCount`；音轨 `channelCount` 属性。订阅：远端音轨 `channelCount` 属性（来自信令 pt 值）；`selectChannels(channels, mode)` 选择输出声道与渲染模式；`getVolumeLevels(channels?)` 获取各声道音量 | 均为 Web SDK 4.20.1-100/200 现有接口 + 开关，无新增公开 API |
| 功能点概述 | ① 多声道能力开启与协商（multiopus ORTC）；② Web 端多声道发布（4/6/8 声道，channelCount 驱动 payload 生成）；③ Web 端多声道订阅与播放（声道数来自信令）；④ 声道选择播放（surround/stereo/mono）；⑤ 逐声道音量获取；⑥ 浏览器兼容性边界（Chrome 96+，推荐 110+；Safari/Firefox 不支持）；⑦ 能力协商回退的端侧表现；⑧ 验证 demo 与判定手段（音量数组长度、getStats codec） | |
| 客户使用逻辑概述 | 订阅：页面加载早期开启 `ENABLE_MULTIOPUS_CODEC` → 创建 client 加入频道 → 订阅目标远端音频 → `play()` → `getVolumeLevels()` 确认声道数 → `selectChannels` 选择声道试听。发布：准备多声道 WAV → fetch + `decodeAudioData` → `MediaStreamDestination`（显式 channelCount）→ `createCustomAudioTrack` → publish | |

### 6.2 体验性需求

| 内容项 | 描述 | 备注 |
| --- | --- | --- |
| API 接口（描述性语言） | 逐声道音量（`getVolumeLevels`）、声道选择（`selectChannels`）、codec 与声道数判定（demo 内 getStats 轮询） | 现成能力 |
| 体验指标概述 | 订阅后约 1 s 内可判定实际声道数（`getVolumeLevels` 数组长度路径，300 ms 周期刷新）；底层 codec 判定（getStats 轮询 800 ms×最多 6 次）最迟约 5 s；声道选择即时生效；各声道音量逐路可辨（注意 dB 刻度底噪，见场景5.1） | 阈值见第七章与 8.1 |
| 性能指标概述 | 频道为多声道流时，不支持端加入不应影响已订阅端（Web 发送场景除外，见需求点7）；具体 CPU/内存指标待性能测试报告补充 | |

### 6.3 客户/用户场景技术拆解

**RTC 场景覆盖**

- Web（Chrome）订阅 Linux SDK 发布的 4/6/8 声道 OPUSMC 流并选择声道播放（客户主场景）。
- Web（Chrome）发布多声道音频流，供 Web / Native / Linux 端订阅。
- 频道内存在不支持多声道端时的行为差异（Linux 发送 vs Web 发送）。

**用户场景覆盖**

- 验证人员打开 Subscriber 标签页（URL 携带 channel/uid/subTo），输入 token 后加入频道、订阅目标流，查看每声道音量条与 codec 判定（multiopus），逐声道试听 Channel ID 素材。
- 发布人员打开 Publisher 标签页，选择多声道 WAV（如 `8_Channel_ID.wav`）与声道数，发布到同一频道。
- 客户运维通过订阅端确认 Linux 发送端布局映射修复效果（2025-04 联调的实际用法）。

**技术维度问题清单**

| # | 技术维度 | 问题 | 答案 | 确认人 |
| --- | --- | --- | --- | --- |
| 1 | 浏览器兼容 | 支持哪些浏览器/版本？ | Chrome 96 起正式支持（94/95 可手动开启但默认关闭；96~109 取消订阅后的重协商差异已兼容；110+ 完全支持）；Safari/Firefox 不推荐，加入多声道频道行为未定义（可能无声或仅立体声） | 设计文档 QA 测试结论 |
| 2 | 声道数范围 | Web 端支持哪些声道数？ | 4/6/8；最多 8 声道；16 声道及以上不支持；仅支持偶数声道解码（3/5/7 不适用——2026-10-12 实测：不报错但按 N+1 偶数声道误解码、末声道复制出幻影声道、界面误判多声道 ✓，结果不可信，见 `Test/odd_channels_interop_test_20261012.md` §5） | 设计文档 + 实测 |
| 3 | 声道选择实现 | 为什么不用 WebAudio 做声道选择？ | Chrome bug（chromium issue 1116042），改用 Insertable Streams（MediaStreamTrackProcessor/Generator）实现 | 设计文档 |
| 4 | 解码协商 | 订阅端如何获知声道数？ | 远端音轨 `channelCount` 来自 `on_add_audio_stream` 信令中的 pt 值；ORTC fmtp 含 channel_mapping/coupled_streams/num_streams，`encodingParameters` 可直接设为最大值 8 以兼容 6/4/2 声道 | 设计文档 |
| 5 | 能力开关 | 不开启 `ENABLE_MULTIOPUS_CODEC` 会怎样？ | 订阅端按普通单/双声道处理，无法解出多声道；开关必须在创建 client 之前设置 | demo 代码注释 |
| 6 | 回退行为 | 频道内出现不支持端时的表现？ | Linux 发送：发送端回退（回退目标 mono 还是 dual-channel 两份文档表述不一致，见 Linux 侧 spec 待确认问题1）；Web 发送：不回退，不支持端无声；可用 `che.audio.codec_unfallback` 强制不回退（该参数设置位置要求见 Linux 侧 spec 场景4.4） | 设计文档 |

### 6.4 现有 API 关联梳理

| API / 能力 | 事实来源 | 职责 | 能力范围 | 与本需求的关系 |
| --- | --- | --- | --- | --- |
| `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)` | `References/AlexGongs/quick-demo-multiopus/src/Multiopus.vue:84`（该参考 demo 已于 2026-10 移除，继承者为 `Demo/web/`）；Web 设计文档 | 开启 multiopus 编解码协商能力 | 必须在 `createClient` 之前调用；未开启则按普通（单/双声道）处理 | 可复用 |
| `createCustomAudioTrack({ mediaStreamTrack, channelCount })` / `createMultiAudioTrack` | Web 设计文档"参考集成代码"；demo `USAGE.md` §6 | 创建多声道本地音轨并发布 | `channelCount` 为创建时设定值，SDK 据其生成发布用的 `custom_payload_type` | 可复用 |
| 远端音轨 `channelCount` 属性 | Web 设计文档 | 获取远端多声道音轨声道数 | 取值来自信令 `on_add_audio_stream` 的 pt 值，非从轨内解析 | 可复用 |
| `selectChannels(channels, mode)`（本地/远端音轨均有） | Web 设计文档 | 设置输出声道列表与渲染模式 | mode ∈ `surround`（标准多声道协议）/ `stereo`（0,2,4,6… 声道入左、1,3,5,7… 入右）/ `mono`（全部混为单声道，输出为立体声容器）；基于 Insertable Streams 实现 | 可复用 |
| `getVolumeLevels(channels?)` | Web 设计文档；`Demo/web/src/Multiopus.vue:604`（quick-demo 已移除并继承） | 返回各声道音量数组 | 不传参返回全部声道；demo 以返回数组长度作为实际声道数判据 | 可复用 |
| ORTC multiopus 协商（payloadType 123、rtpMap multiopus/48000、fmtp channel_mapping/coupled_streams/num_streams、minptime、useinbandfec） | Web 设计文档"Multi-channel Technical Details" | 声明多声道解码能力 | 总声道数 = coupled_streams + num_streams；`encodingParameters` 可设为 8 以兼容 6/4/2 | 可复用 |
| RTCPeerConnection getStats（demo 内 hook） | `Demo/web/src/Multiopus.vue:278-295,662`（quick-demo 已移除并继承） | 判定实际收到的 codec（opus / multiopus） | demo 层手段，非 SDK 公开 API | 可复用（验证链路） |
| Linux SDK 多声道发送（对端来源） | `Spec/linux_sdk_audio_multichannel_spec.md` | 产生 OPUSMC 流 | payload 123/124/125，声道顺序按已确认映射 | 强依赖 |

**API 间关系**：`ENABLE_MULTIOPUS_CODEC` 是订阅/发布多声道的前置总开关；发布路径由 `channelCount` 驱动 payload 协商；订阅路径由信令 pt 值驱动解码与 `channelCount` 展示；`selectChannels`/`getVolumeLevels` 作用于已建立的多声道音轨（本地或远端）。

**能力差距**：多声道能力未文档化为正式公开 API（依赖 `setParameter` 开关与 d.ts 内接口）；无 Safari/Firefox 支持；无奇数声道解码；SDK 未暴露协商后实际 codec/声道数事件（demo 以 getStats hook 绕行）；声道布局约定（哪路是 T1L/T1R…）由发送侧与业务约定，SDK 不感知。

### 6.5 流程图（Optional）

订阅验证链路（客户主场景）：

```
Linux 端发布 8ch OPUSMC (payload 125)
  → SD-RTN / Web VOS
  → Web 订阅端（ENABLE_MULTIOPUS_CODEC 已开启）
      → subscribe → track.play()（默认布局播放）
      → getVolumeLevels() 数组长度 = 声道数（判定 multiopus 生效）
      → selectChannels([...], surround/stereo/mono) 逐声道试听
```

发布链路（Web → 频道）：

```
多声道 WAV → fetch → AudioContext.decodeAudioData
  → MediaStreamDestination（显式 channelCount）→ 循环播放
  → createCustomAudioTrack({ mediaStreamTrack, channelCount }) → publish
```

---

## 七、详细需求规格

### 需求点1：多声道能力开启与协商

系统应支持通过开关开启 Web SDK 的 multiopus 能力，开启后订阅/发布走 OPUSMC 协商；未开启时保持普通音频行为。

#### 场景1.1：开启时机

- 前置条件：页面引入 agora-rtc-sdk-ng（4.20.1-100/200）
- 当：调用 `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)`
- 则：此后创建的 client 具备多声道编解码协商能力
- 且：必须在 `AgoraRTC.createClient()` 之前设置；创建后再设置不生效（demo 注释明确该约束）

#### 场景1.2：未开启时的默认行为

- 当：未设置开关即订阅多声道流
- 则：按普通单/双声道处理，无法还原多声道（demo 备注：只有开启后订阅端才可能收到多声道流；Web 端仅支持 4/6/8 偶数声道解码，3/5/7 不适用）

#### 场景1.3：解码能力声明

- 当：订阅端进行 ORTC 能力协商
- 则：声明 multiopus（payloadType 123、clockRate 48000、fmtp 含 channel_mapping/coupled_streams/num_streams、rtcpFeedback 含 transport-cc 与 nack）；`encodingParameters` 可设为最大值 8，兼容 6/4/2 声道流
- 且：总声道数 = coupled_streams + num_streams

### 需求点2：Web 端多声道发布

系统应支持将多声道音频素材以自定义音轨发布，声道数由创建参数决定并驱动 payload 协商。

#### 场景2.1：多声道 WAV 发布

- 前置条件：`ENABLE_MULTIOPUS_CODEC` 已开启；素材为多声道 WAV（demo 内置 `4/6/8_Channel_ID.wav`）
- 当：fetch 素材 → `AudioContext.decodeAudioData` → 经显式 `channelCount` 的 `MediaStreamDestination` 输出 `MediaStreamTrack` → `createCustomAudioTrack({ mediaStreamTrack, channelCount })` → publish
- 则：频道内产生 OPUSMC 多声道流，声道数与 `channelCount` 一致；支持循环播放（`onended` 重新挂载）
- 且：channelCount 支持 4/6/8；SDK 依据该值生成发布用 `custom_payload_type`；音轨 `channelCount` 属性取创建时设定值

#### 场景2.2：声道布局约定

- 当：发布 8 声道内容
- 则：声道路由（哪路对应哪个逻辑声道）由发布素材与业务约定决定，SDK 不做声道重排；客户场景下应与 Linux 侧已确认映射（T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R）保持一致
- 且：素材采样率经 Web Audio 图后按协商 48 kHz 编码（多声道编解码仅 48 kHz）

### 需求点3：Web 端多声道订阅与播放

系统应支持订阅远端多声道音频流并播放，远端音轨可读取声道数。

#### 场景3.1：订阅多声道流

- 前置条件：`ENABLE_MULTIOPUS_CODEC` 已开启；频道内存在 Linux/Web 发布的多声道流
- 当：`client.subscribe(user, "audio")` 成功后 `track.play()`
- 则：按多声道解码播放；远端音轨 `channelCount` 属性反映信令 pt 值对应的声道数
- 且：仅支持偶数声道（4/6/8）解码；奇数声道流对 Web 端不适用

#### 场景3.2：实际声道数判定

- 当：订阅建立后
- 则：`getVolumeLevels()` 返回数组长度等于实际解码声道数（demo 以此作为多声道判定主信号）；通过 getStats 可读到底层 codec 为 `multiopus`（demo 以 hook RTCPeerConnection 方式实现，非 SDK 公开 API）
- 且：codec 判定需容忍短暂延迟（demo 按 800 ms 轮询、最多 6 次）

### 需求点4：声道选择播放

系统应支持对多声道音轨（本地/远端）选择任意声道组合播放，并支持三种渲染模式。

#### 场景4.1：surround 模式

- 当：调用 `selectChannels(channels, "surround")`
- 则：按标准多声道协议输出所选声道；例如 5.1 布局中选择声道 2（中置）与声道 3（LFE）可按立体声播放

#### 场景4.2：stereo / mono 模式

- 当：调用 `selectChannels(channels, "stereo")`
- 则：所选声道中的 0,2,4,6… 声道混入左、1,3,5,7… 声道混入右，输出立体声
- 且：mode 为 `"mono"` 时全部声道混为单声道（输出为立体声容器）；默认 mode 为 `surround`

#### 场景4.3：实现约束

- 则：声道选择基于 Insertable Streams（MediaStreamTrackProcessor/Generator）实现（绕开 Chrome WebAudio 缺陷 issue 1116042）
- 且：依赖浏览器对 Insertable Streams 的支持（见需求点6 兼容性边界）

### 需求点5：逐声道音量获取

系统应支持获取多声道音轨各声道的实时音量。

#### 场景5.1：音量轮询

- 当：订阅/创建多声道音轨后调用 `getVolumeLevels()`
- 则：返回全部声道音量数组（0~1）；传入声道列表（如 `[0,1,2]`）时仅返回所列声道
- 且：demo 以 300 ms 周期刷新并渲染逐声道音量条，作为声道数与内容正确性的可视化判据
- 且：音量为 dB 宽量程刻度（`(dBFS_RMS + 100) / 100`）——16bit 素材静音段因 dither 读数约 10%，
  仅精确数字零显示 0%；判定时须区分「底噪读数」与「内容」，勿把静音声道的 ~10% 误判为串声道
  （实测分析见 `Test/web_volume_meter_floor_linux2web.md`）

### 需求点6：浏览器兼容性边界

系统行为应满足：Chrome 96+ 可用、110+ 完全支持；Safari/Firefox 不在支持范围。

#### 场景6.1：Chrome 版本差异

- 当：使用 Chrome 94/95
- 则：相关接口默认关闭，需手动开启，不推荐
- 且：Chrome 96 起正式支持；96~109 取消订阅后的重协商行为差异已做兼容；110+ 完全支持

#### 场景6.2：非 Chrome 浏览器

- 当：Safari 或 Firefox 加入多声道频道
- 则：行为未定义——可能无声或仅立体声
- 且：多声道频道内应避免引入 Safari/Firefox 端（与 Linux 侧"所有参与者需支持多声道"约束一致）

### 需求点7：能力协商回退的端侧表现

系统应遵循既定的回退规则：Linux 发送时触发回退，Web 发送时不回退。

#### 场景7.1：Linux 端发送、不支持端加入

- 前置条件：Linux 端正以 OPUSMC 发送多声道流
- 当：不支持多声道的端加入频道
- 则：发送端编解码回退到普通 Opus，多声道能力失效（回退目标表述不一致问题见 Linux 侧 spec 待确认问题1）；Web 订阅端将观测到 codec 由 multiopus 变为 opus、声道数降为回退值
- 且：发送端可设置 `"che.audio.codec_unfallback": [123, 124, 125]` 强制不回退（该私参必须在 `service->getAgoraParameter()` 上、`createRtcConnection()` 之前设置，设在 connection 级不生效）

#### 场景7.2：Web 端发送、不支持端加入

- 当：Web 端正以多声道发布，不支持端加入
- 则：不触发回退，该不支持端听不到声音
- 且：使用多声道能力时应保证频道内所有参与者支持多声道

### 需求点8：验证 demo 与判定手段

系统应提供可运行的 Web 验证 demo，支撑双标签页自收发与逐声道验证。

#### 场景8.1：双标签页验证流程

- 前置条件：Web 验证 demo（`Demo/web/`，quick-demo-multiopus 已于 2026-10 移除并继承）运行（Vite dev server）；两个标签页分别以 Publisher/Subscriber uid 与对应 token 加入同一频道（URL 参数 `channel`/`uid`/`subTo` 控制角色与订阅目标）
- 当：Publisher 发布多声道音频，Subscriber 订阅
- 则：Subscriber 面板显示 codec（multiopus）与逐声道音量条；仅当 codec 为 `multiopus`，或 `getVolumeLevels()` 返回 4/6/8 个声道时，判定为多声道。普通单/双声道不得判定为多声道。**注意：对 3/5/7 奇数声道流，该判据会被误判（Web 按 N+1 偶数声道误解码，末声道为幻影复制，界面会显示「多声道 ✓」）——奇数声道不得用 Web 端验证**（见 6.3#2 与 `Test/odd_channels_interop_test_20261012.md` §5）
- 且：token 与 uid 绑定，两个标签页需各自 uid 的 token（或 uid=0 通配 token）

#### 场景8.2：逐声道内容核对

- 当：订阅 Channel ID 类素材流
- 则：通过 `selectChannels` 逐声道试听，各声道内容与发送端约定声道顺序一致、无串声道、无杂音
- 且：该用例用于回归 Linux 发送侧 4×双声道 → 8 声道布局映射（Linux 侧 spec 场景9.4）

---

## 八、需求

### 8.1 需求汇总及验收标准

| 需求项 | 需求描述 | 性能要求 | 体验要求 | 优先级 | 交付时间 | Jira（Optional） | 安全合规审查 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 多声道能力开启与协商 | `ENABLE_MULTIOPUS_CODEC` 开启后 ORTC multiopus 协商生效，未开启保持普通行为 | — | 开关在 createClient 前设置 | P0 | 已交付（4.20.1-100/200） | — | 不涉及 | |
| Web 端多声道发布 | channelCount 4/6/8 自定义音轨发布，payload 自动生成 | — | 发布后远端可解出对应声道数 | P1 | 同上 | — | 不涉及 | demo 已含 WAV 发布路径 |
| Web 端多声道订阅与播放 | 订阅 OPUSMC 流播放，`channelCount` 可读 | — | 声道数判定约 1 s（音量数组长度）；codec 判定最迟约 5 s | P0 | 同上 | — | 不涉及 | 仅偶数声道 |
| 声道选择播放 | `selectChannels` 三模式（surround/stereo/mono）任意声道组合 | — | 选择即时生效 | P0 | 同上 | — | 不涉及 | Insertable Streams 实现 |
| 逐声道音量获取 | `getVolumeLevels` 返回各声道音量数组 | — | 数组长度 = 实际声道数 | P0 | 同上 | — | 不涉及 | dB 刻度：静音声道在 dither 素材下读数 ~10%（非串声道，见场景5.1） |
| 浏览器兼容性 | Chrome 96+ 可用、110+ 完全支持；Safari/Firefox 不支持 | — | 明确的不支持声明 | P0 | 同上 | — | 不涉及 | |
| 回退端侧表现 | Linux 发送回退可观测（codec/声道数变化）；Web 发送不回退 | — | 不支持端行为符合规则 | P0 | 同上 | — | 不涉及 | 与 Linux 侧 spec 交叉引用 |
| 验证 demo | Web 验证 demo（`Demo/web/`，quick-demo-multiopus 已于 2026-10 移除并继承）双标签页流程可用 | — | 逐声道音量条 + codec 判定 | P0 | 已交付 | — | 不涉及 | 奇数声道流会误判（见 6.3#2） |

### 8.2 产出物

| 产出物 | 产出物描述 | Jira（可选） | 是否必须 | 交付时间 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 开发包/服务 | agora-rtc-sdk-ng@4.20.1-100（npm：media/report/shared 同名版本；FULL zip 与 Aliyun CDN `AgoraRTC_N-4.20.1-100.js`）；定制版 4.20.1-200 | — | 必须 | 已交付 | Web VOS 服务端配合已上线 |
| 性能测试报告 | 多声道订阅/发布的浏览器端 CPU/内存基线，以及 4/6/8 声道、Chrome 96+/110+ 的测试环境与样本量 | 待补充 | 必须 | 发布前 | 未形成报告不得将性能与稳定性标记为已验收 |
| 体验测试报告 | 多声道收发主观验证（逐声道、回退、浏览器矩阵） | 待补充 | 必须 | 已有（Web SDK 4.20.1-200 Test Report） | 来源：交付记录 |
| API 文档 | multiopus 开关与 `selectChannels`/`getVolumeLevels`/`channelCount` 使用说明 | 待补充 | 必须 | 待补充 | 当前见 NPM 包内 `rtc-sdk_cn.d.ts` / `rtc-sdk_en.d.ts` 与内部设计文档 |
| 最佳实践文档 | 订阅验证与发布集成指南（含浏览器约束） | 待补充 | 必须 | 待补充 | |
| Demo | quick-demo-multiopus（Vue3 + Vite） | — | 必须 | 已交付 | `References/AlexGongs/quick-demo-multiopus/`（已于 2026-10 移除，继承者为 `Demo/web/`） |
| 安全合规审查结论 | 不涉及新增对外数据采集 | — | 【待明确】 | — | |

### 8.3 平台/语言/框架支持需求

| 大项 | 子项 | 是否需要 |
| --- | --- | --- |
| 平台 | Web（Chrome 96+，推荐 110+） | 是 |
| 平台 | Web（Safari / Firefox） | 否（行为未定义，不推荐） |
| 平台 | iOS / Android / Windows / Mac / Harmony OS / 通信OS/麒麟 | 否（Native 侧多声道见对应产品线） |
| 平台 | Linux Server | 否（见 Linux 侧 spec） |
| 跨平台开发框架 | Electron / Flutter / React Native / Unity / Cocos Creator / Uniapp | 【待明确】（Electron 内核为 Chromium，理论上可继承 Chrome 行为，未验证） |

### 8.4 协同需求

| 内容项 | 描述 | Jira（Optional） | 优先级 | 交付时间 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 计量计费需求 | 多声道当前按普通双声道音频计费，未来可能调整 | — | 【待明确】 | — | 来源：设计文档 Pricing 章节 |
| Web VOS 服务端 | multiopus 能力协商与 `PUnsupportedAudioCodec` 广播处理（Web 端加入触发广播、多声道 codec 不回退） | — | P0 | 已交付 | 线上已生效 |
| 云录制需求 | 多声道流的云录制行为未在输入材料中覆盖 | — | 【待明确】 | — | |
| 本地服务端录制 SDK | 不涉及 | — | — | — | |
| 推流服务 | 不涉及 | — | — | — | |
| 云端转码服务 | 不涉及 | — | — | — | |
| 在线媒体流服务 | 不涉及 | — | — | — | |
| RTMP网关服务 | 不涉及 | — | — | — | |
| 频道管理 | 不涉及 | — | — | — | |
| 水晶球 | 多声道通话在水晶球的指标展示（codec/声道数）未确认 | — | 【待明确】 | — | |
| 混合云 | 不涉及 | — | — | — | |
| 私有化 | 不涉及 | — | — | — | |

---

## 九、待确认问题

| # | 问题 | 维度 | 建议确认人 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | Safari/Firefox 是否有支持计划（当前行为未定义） | 兼容性 | PM / 研发 | 待确认 |
| 2 | 奇数声道（3/5/7）Web 端解码是否规划支持（当前仅偶数声道）。2026-10-12 实测补充：当前版本对奇数声道流不报错但按 N+1 偶数声道误解码（mono 子流当 stereo 解、末声道复制幻影声道、音量条误判多声道 ✓），结果不可信——见 `Test/odd_channels_interop_test_20261012.md` §5 | 功能边界 | PM / 研发 | 规划待确认；当前行为已实测（误解码且结果不可信） |
| 3 | Electron 等 Chromium 内核框架是否纳入支持矩阵（未验证） | 平台 | PM / QA | 待确认 |
| 4 | 多声道流在云录制、水晶球等云端链路上的表现与计费影响 | 协同 | PM / 服务端 | 待确认 |
| 5 | SDK 是否计划提供协商后实际 codec/声道数的公开事件（当前依赖 getStats hook 绕行） | API | PM / 研发 | 待确认 |

---

## 十、FAQ

| 问题 | 回复 |
| --- | --- |
| 订阅多声道流为什么只听到单/双声道？ | 检查是否在 `createClient` 之前设置了 `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)`；未开启则按普通音频处理 |
| 如何确认实际解出了几声？ | 订阅后调用 `getVolumeLevels()`，返回数组长度即实际声道数；或读取底层 getStats 的 codec（multiopus 为多声道）——demo 中两者均已实现 |
| 如何只放某几个声道？ | `remoteAudioTrack.selectChannels([0,1], "surround")`；`stereo` 模式偶数声道入左、奇数声道入右；`mono` 全部混单声道 |
| Web 端能发多声道吗？ | 能。多声道素材经 Web Audio 解码后以 `createCustomAudioTrack({ mediaStreamTrack, channelCount })` 发布，channelCount 支持 4/6/8；注意与 Linux 侧声道布局约定一致 |
| Safari/Firefox 能用吗？ | 不推荐。加入多声道频道行为未定义（可能无声或仅立体声），请使用 Chrome 96+（推荐 110+） |
| 频道里进来不支持多声道的端会怎样？ | Linux 发送时发送端回退、多声道失效（可用 `che.audio.codec_unfallback` 强制不回退，注意其设置位置要求）；Web 发送时不回退且该端无声 |
| 最多支持几声？ | 8 声道；16 声道及以上不支持；Web 端仅偶数声道解码 |

---

## 附录

### 研发准入自检

| # | 检查项 | 状态 | 备注 |
| --- | --- | --- | --- |
| 1 | 功能点有明确的输入/输出/边界行为 | ✅ | 开启时机、发布/订阅/声道选择/音量、回退表现均有场景化描述 |
| 2 | 平台差异已逐条说明 | ✅ | Chrome 各版本段、Safari/Firefox、Linux 发送 vs Web 发送回退差异 |
| 3 | 有可量化的验收标准（性能+体验） | [待澄清] | 声道数判定/音量可量化；浏览器端性能基线待性能报告 |
| 4 | 客户在用版本和预期交付版本已明确 | ✅ | 4.20.1-100 / 4.20.1-200 |
| 5 | 客户/用户场景已拆解为技术维度问题 | ✅ | 见 6.3 技术维度问题清单 |
| 6 | 现有 API 关联关系已梳理 | ✅ | 见 6.4，均附 demo 代码/设计文档来源 |

### 参考资料

- Web 多声道设计文档：`References/Docs/ExT-1485178492-100726-1000-58_en.md`
- Linux 多声道设计文档：`References/Docs/ExT-1485178709-100726-0959-56_en.md`（协商/回退与 payload 映射）
- 交付记录：`References/Docs/ExT-1574700707-170726-0613-20.pdf`（Web SDK 4.20.1-200 多声道定制版及 Test Report）
- 验证 demo：`References/AlexGongs/quick-demo-multiopus/`（`src/Multiopus.vue`、`USAGE.md`、`README.md`）——已于 2026-10 移除，功能由 `Demo/web/` 继承
- 关联文档：`Spec/linux_sdk_audio_multichannel_spec.md`（Linux 侧发送/接收/布局映射需求）
- 布局映射沟通记录：原 `References/沟通记录/` 已于 2026-10-09 删除；内容存档见 `References/communication_records_backup.md` / `communication_records_backup_en.md`（最终确认声道映射：逐样本交错 T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R）
- Chrome 缺陷依据：<https://bugs.chromium.org/p/chromium/issues/detail?id=1116042>；Opus multistream API：<https://www.opus-codec.org/docs/opus_api-1.1.2/group__opus__multistream.html>

### 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
| --- | --- | --- | --- |
| v0.1 | 2026-08-12 | 初稿：基于 Web 多声道设计文档与 quick-demo-multiopus 收敛发布/订阅/声道选择需求 | Kimi Code |
| v0.2 | 2026-10-12 | 一致性修订：场景1.2「3-8 声道」更正为「仅 4/6/8 偶数声道」（与 :120/:216 对齐）；场景2.2「声道光箱重排」笔误更正；参考资料中已删除的 `References/沟通记录/` 指向文字化存档；判定口径补齐（音量数组约 1 s / codec 最迟约 5 s）；场景5.1 与 8.1 补 dB 刻度底噪 caveat（~10% 为 dither 底噪非内容）；同步认可此前 quick-demo 移除等未入记录的正文修订 | Kimi Code |
| v0.3 | 2026-10-12 | 奇数声道 Web 行为实测更正：6.3 技术维度「声道数范围」更新——3/5/7 流在 Web 端不报错但按 N+1 偶数声道误解码（mono 子流当 stereo 解、末声道复制出幻影声道、界面误判多声道 ✓），结果不可信；依据 `Test/odd_channels_interop_test_20261012.md` §5 | Kimi Code |
| v0.4 | 2026-10-12 | 复审修订：场景8.1 判定准则补奇数声道误判警示（3/5/7 流不得用 Web 验证）；待确认问题2 状态改为「规划待确认；当前行为已实测」；quick-demo-multiopus 残留引用统一补移除注解（摘要/2.1/8.1/8.2）；6.4 表中 Multiopus.vue 行号引用改指 `Demo/web/src/Multiopus.vue` 现行位置 | Kimi Code |
