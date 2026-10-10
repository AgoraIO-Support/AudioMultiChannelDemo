# Linux SDK 音频多声道收发 产品需求文档（PRD 初稿）

## Linux SDK 音频多声道（发送/接收/多格式音频文件输入）需求

> 摘要：客户场景下，Linux SDK（Server 端，x86_64 / aarch64）需要支持 3~8 声道音频的发送与接收。发送侧支持三种音频文件输入路径（raw PCM、WAV 16/24bit、预编码多声道 Opus 裸流），并覆盖客户源端 4 条双声道音轨重排为 8 声道流的布局映射（声道顺序已双方确认：T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R 逐样本交错）；接收侧支持解码后 PCM 回调、编码帧回调与混音输出三种获取方式，并可通过 Web 端 demo 完成端到端声道级验证。多声道编码基于 Opus 多流（OPUSMC，payload type 123/124/125），仅支持 48 kHz 采样率。该能力已在定制版 v4.2.32（2024-08-27）与正式版 v4.4.30.130（2025-04-23，QA 验证通过）中交付验证，本文档将其能力事实、行为边界与验收口径收敛为需求基线。

---

## 一、文档基础信息

| 项目 | 内容 |
| --- | --- |
| PM | 待补充 |
| TO | 待补充 |
| PMO | 待补充 |
| 需求 Jira | 背景单：CSD-74687（音画不同步）、CSD-76350（Multipath 音频延迟）、CSD-76354（Linux SDK 4.2.32 Crash）、ASS-2272（Linux SDK 特殊版申请，含 MultiPath）；需求 Jira 待补充 |
| 需求版本 | v0.6 |

---

## 二、需求背景

### 2.1 客户原始需求

客户在 Linux 服务器端（含 ARM 架构）使用 Agora Linux SDK 收发多声道音频：发送端将多声道音频素材（4/6/8 声道 WAV、8 声道 Opus 等）注入频道；接收端订阅多声道音频流并按业务需要选择声道播放或做进一步处理。多声道能力已在定制版中分批交付并由客户印度研发团队参与验证，现需将发送、接收与音频文件输入相关能力整理为正式需求基线，作为后续版本回归与验收依据。

| 内容项 | 详情 | 备注 |
| --- | --- | --- |
| 客户(VID) | VID 待补充 | |
| 需求 Reporter | 客户印度研发团队（企业微信沟通记录）；Agora 侧 CSD/ASS 流程 | 见 CSD-74687 / ASS-2272 |
| 需求业务线 | 广电级音视频传输 / 云端制作 | 依据交付记录推断，待客户确认 |
| 客户业务场景 | Linux 服务器端注入多声道音频（最高 8 声道）到 RTC 频道，供 Web / Native / Linux 端订阅接收并按需选择声道播放或后处理；客户源端 8 声道内容以 **4 条双声道（stereo）音轨**承载（AC-3、44.1 kHz，参考 `VictoriaMulti.mkv`：1 路 mpeg4 视频 + 4 条 ac3 stereo 音频流） | |
| 客户碰到的问题 | 标准 SDK 音频仅支持单/双声道（Native 此前最多 4 声道、Linux 此前仅双声道），无法传输 4/6/8 声道素材；客户通过 4 条双声道音轨承载 8 声道，送入 Agora Linux SDK 时需重排为 RTC 频道中的 8 声道流，存在 **audio layout 声道映射问题**——2025-04 联调中接收端出现杂音，根因为发送端把逐样本交错（LRLR…）的源数据按平面块序（LLL…RRR…）读取，最终声道映射经双方确认（客户侧与 Agora 侧声道映射图），2025-04-14 客户确认问题解决；交付过程中曾反馈音画不同步（CSD-74687）、多声道功能异常、Multipath 下音频延迟（CSD-76350）、4.2.32 Crash（CSD-76354） | |
| 该问题解决对客户的价值 | Linux 服务器可直接收发 3~8 声道音频，接收端可按声道选择播放/处理，支撑广电多声道制作链路 | |
| 当前客户在用方案 | Linux SDK `example/multiopus` 系列 sample：raw PCM 发送、WAV（16/24bit）发送、预编码 Opus（8ch）发送；本仓库 Demo 对应 `sample_receive_pcm_multiopus`（解码 PCM 回调）与 `sample_receive_encoded_audio`（编码帧回调）；Web 端使用 quick-demo-multiopus 验证 | 旧 SDK reference sample 名称见 6.4，当前 Demo 名称见 8.1 |
| 客户做了哪些尝试 | 基于 v4.2.32（0715）与 v4.4.30.130（verified0805）两个版本的多声道 sample 完成收发验证 | |
| 是否有竞品在 pk 中 | 无明确竞品信息 | |
| 竞品方案 | 无明确竞品线路 | |
| 客户在用版本 | Agora-RTC-x86_64/aarch64-linux-gnu v4.2.32-20240827（定制版）；v4.4.30.130-20250409（正式版，QA 验证通过）；后续修复版本 4.4.30.148 / 4.4.30.149 | 来源：Linux SDK ARM Version 交付记录（ExT-1574700707） |
| 当前使用的平台/语言/框架 | Linux Server（x86_64、aarch64），C++；验证端为 Web（Chrome，agora-rtc-sdk-ng 4.20.1-200 多声道定制版） | |
| 期望交付版本 | release/4.4.30.130 交付分支后续版本（当前最新 4.4.30.149） | |
| 期望交付的平台/框架 | Linux Server x86_64 + aarch64（C++ SDK） | |

### 2.2 当前产品方案

| 方案 | API | 功能描述 | 方案定位 | 方案成熟度 | 方案支持当前需求存在的 Gap |
| --- | --- | --- | --- | --- | --- |
| 多声道编码发送（OPUSMC） | `IAudioPcmDataSender::sendAudioPcmData` / `IAudioEncodedFrameSender::sendEncodedAudioFrame` + 私参 `che.audio.custom_payload_type`、`che.audio.custom_channel_num` | Linux SDK 将多声道音频编码为 OPUSMC（payload 123/124/125）发送至 SD-RTN | 定制版能力 | 已在 v4.2.32 / v4.4.30.130 验证交付 | 依赖私有参数配置，无公开 API；声道数 >4 时收发两端声道数配置需一致；对端不支持时触发编解码回退 |
| 多声道接收（解码 PCM） | `IAudioFrameObserverBase::onPlaybackAudioFrameBeforeMixing` + `ILocalUser::setPlaybackAudioFrameBeforeMixingParameters` | 按远端用户回调解码后的多声道 PCM，可落盘或后处理 | 定制版能力 | 已验证 | 回调声道数需与发送端一致；播放混音输出恒为 2 声道 |
| 多声道接收（编码帧） | `RtcConnectionConfiguration::audioRecvEncodedFrame` + `IAudioEncodedFrameReceiver::onEncodedAudioFrameReceived` | 订阅端直接获取 OPUSMC 编码帧（不解码、不播放） | 定制版能力 | 已验证 | 需业务侧自行解码/转发 |
| Web 端多声道收发验证 | `ENABLE_MULTIOPUS_CODEC`、`createCustomAudioTrack({channelCount})`、`selectChannels`、`getVolumeLevels` | Web 端发布/订阅多声道流，按声道选择播放并显示各声道音量 | 验证链路（quick-demo-multiopus） | 已验证（Chrome 96+） | Safari / Firefox 不支持；Web 仅支持偶数声道解码 |

---

## 三、同类历史需求

- Linux SDK ARM 版本交付线（release/4.4.30.130 交付分支）：v4.4.30.130 工程验证版（2025-01-08、2025-03-20）→ 正式版（2025-04-23，QA 验证通过）→ 4.4.30.148（2025-08-28）→ 4.4.30.149（2026-02-05，含 CSD-76354 Crash 修复、CSD-76350 Multipath 音频延迟修复，正式版需设置私参 `che.audio.drop_frames_when_delay:true`）。
- Native SDK / Web SDK 多声道需求（2024-03）：同一多声道项目的产品线落地，Web SDK v4.20.1-100/200 支持多声道发布与订阅（ExT-1485178492）。
- Ambisonic 空间音频项目：多声道编解码能力来源于该项目（复用 Opus Ambisonic 多流模式）。

## 四、需求类型和价值

| 内容项 | 详情 | 备注 |
| --- | --- | --- |
| 是否通用需求 | 否（客户交付驱动）；能力本身具备复用性，可向有类似多声道诉求的客户推广 | |
| 需求类型 | 新需求（功能落地与验收基线收敛） | |
| 需求价值 | 客户：Linux 服务器端完成 3~8 声道音频收发，支撑广电多声道制作与分发链路；产品线：Linux SDK 与 Native/Web 拉齐多声道能力，形成端到端多声道解决方案 | |

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
| API 接口（描述性语言） | 发送：PCM 原始数据发送接口（`sendAudioPcmData`）、编码音频帧发送接口（`sendEncodedAudioFrame`，本仓库 v4.4.30.149 Demo 使用 `AUDIO_CODEC_OPUSMC`）；通过连接级私有参数设置多声道 payload type 与声道数。接收：按用户的解码前混音 PCM 回调（`onPlaybackAudioFrameBeforeMixing` + 回调参数设置）、编码音频帧接收回调（`audioRecvEncodedFrame` + `onEncodedAudioFrameReceived`）、混音音频回调 | 旧 reference 中的 `AUDIO_CODEC_OPUS8C` 仅作为历史版本资料，不可作为当前 Demo 接口契约 |
| 功能点概述 | ① 多声道 PCM 发送（3~8 声道、48 kHz、16bit、10ms 帧）；② 多声道 WAV 文件发送（RIFF 解析，16bit 直发、24bit 转 16bit）；③ 预编码多声道 Opus 发送（8 声道、20ms 帧、640 字节/帧裸流）；④ 多声道编码配置与能力协商（payload 123/124/125、回退与强制不回退）；⑤ 接收端解码 PCM 回调（按用户、声道数可配）；⑥ 接收端编码帧回调；⑦ 混音输出（恒 2 声道）；⑧ 端到端验证手段（Web demo、音量、日志与 dump）；⑨ 发送侧 4×双声道音轨 → 8 声道布局映射（声道顺序已确认，重排在客户侧完成） | |
| 客户使用逻辑概述 | 发送端：准备 48 kHz 多声道素材 → 创建服务/连接 → connect 前设置 payload type 与声道数 → 创建对应 sender 与 custom audio track → publish → 按帧节奏喂数据。接收端：创建连接（按需开启编码帧接收或设置 PCM 回调参数）→ 订阅远端音频 → 在回调中获取多声道 PCM / 编码帧 → 落盘、播放或后处理；或用 Web demo 订阅并逐声道验证 | |

### 6.2 体验性需求

| 内容项 | 描述 | 备注 |
| --- | --- | --- |
| API 接口（描述性语言） | 可观测性依赖 SDK 日志与 dump 文件（编码器建联日志、编码输入/解码输出/混音输出 PCM dump）及 Web 端各声道音量接口 | 现成能力 |
| 体验指标概述 | 端到端各声道内容与发送素材逐声道一致（Channel ID 素材逐声道可辨）；接收端各声道音量可逐路观测 | 阈值见第七章与 8.1 |
| 性能指标概述 | 发送节奏：PCM 10 ms/帧、Opus 20 ms/帧，与实际墙钟偏差在 sample pacer 精度内；多声道码率随声道数上升（编码器日志示例：4 声道 48 kHz bitrate 128000），弱网下依赖拥塞控制适配 | 详细指标待性能测试报告补充 |

### 6.3 客户/用户场景技术拆解

**RTC 场景覆盖**

- Linux Server（主播角色）向频道发布多声道音频流（OPUSMC，payload 123/124/125）。
- Linux Server（观众角色）订阅频道内多声道音频流，获取解码 PCM 或编码帧。
- Web（Chrome）作为验证/监听端订阅多声道流，按声道选择播放（surround/stereo/mono）。
- 频道内存在不支持多声道端时的能力协商与编解码回退（含强制不回退配置）。

**用户场景覆盖**

- 客户运维/研发在 Linux 服务器运行 sample，注入 4/6/8 声道 Channel ID 素材或 8 声道 Opus 素材。
- 验证人员在 Web demo 输入频道与目标 UID，订阅后查看各声道音量条与 codec 类型（multiopus），逐声道试听确认声道顺序与内容。
- 客户后端服务通过 PCM 回调拿到多声道数据做混流/转码/AI 处理，或通过编码帧回调转发 OPUSMC 流。

**技术维度问题清单**

| # | 技术维度 | 问题 | 答案 | 确认人 |
| --- | --- | --- | --- | --- |
| 1 | 3A 处理 | 多声道自定义音频源是否需要 AEC/AGC/ANS？ | 自定义 PCM/编码帧发送路径不经过 3A；预编码 Opus 发送创建 track 时使用 `MIX_DISABLED` | 代码事实 |
| 2 | 采样率 | 多声道是否支持非 48 kHz？ | 否，多声道编解码仅支持 48 kHz（编码/解码均是） | 设计文档 |
| 3 | 声道数范围 | 支持哪些声道数？ | 3/4/5/6/7/8；3/5/7 需额外设置 `che.audio.custom_channel_num`；16 声道及以上不支持 | 设计文档 |
| 4 | 能力协商 | 对端不支持多声道时行为？ | Linux 发送时回退为非多声道编码；Web 发送时不回退且不支持端无声；可用 `che.audio.codec_unfallback` 强制不回退 | 设计文档（回退到单声道还是双声道两份文档表述不一致，见待确认问题） |
| 5 | 混音策略 | 多声道混音输出什么布局？ | 奇数声道混一路、偶数声道混一路，混音输出恒为 2 声道 | 设计文档 |
| 6 | 输入素材 | 44.1 kHz 多声道 WAV 能否直接发送？ | 不能。本仓库 WAV sender 不做重采样，并在连接前拒绝非 48 kHz 素材；业务侧必须先重采样 | 当前 Demo 代码事实 |

### 6.4 现有 API 关联梳理

| API / 能力 | 事实来源 | 职责 | 能力范围 | 与本需求的关系 |
| --- | --- | --- | --- | --- |
| `IAudioPcmDataSender::sendAudioPcmData` | `References/LinuxSampleCodes/0715_v4.2.32/example/multiopus/sample_send_h264_pcm_multiopus.cpp:149` | 发送原始 PCM（10ms 帧，16bit） | 声道数/采样率由入参指定；多声道需配合私参 | 可复用 |
| `IAudioEncodedFrameSender::sendEncodedAudioFrame` + `AUDIO_CODEC_OPUSMC` | `Demo/linux/multiopus/sample_send_opus8.cpp` | 发送预编码多声道 Opus 帧 | 发送侧 `EncodedAudioFrameInfo` 设置 codec/channels/sampleRate/samplesPerChannel；不解析 ogg 封装，按固定帧长裸流喂入 | 可复用 |
| 私参 `che.audio.custom_payload_type` / `che.audio.custom_channel_num` / `che.audio.custom_sample_rate` | `sample_send_h264_pcm_multiopus.cpp:294-295`；Linux 多声道设计文档（ExT-1485178709） | 设置多声道 payload type（123/124/125）、具体声道数、采样率 | 须在 `connect` 之前对 `connection->getAgoraParameter()` 设置；接收端（123/124/125 协商下）无需设置 | 可复用 |
| 私参 `che.audio.codec_unfallback` | Linux / Web 多声道设计文档；CSE 实测 | 强制全频道使用多声道能力、禁止编解码回退 | 取值如 `[123, 124, 125]`；**必须在 `service->getAgoraParameter()` 上、`createRtcConnection()` 之前设置，设在 connection 级不生效** | 可复用 |
| `IAudioFrameObserverBase::onPlaybackAudioFrameBeforeMixing` + `ILocalUser::setPlaybackAudioFrameBeforeMixingParameters` | `References/LinuxSampleCodes/0715_v4.2.32/example/yuv_pcm/sample_receive_yuv_pcm.cpp:106,280` | 按远端用户回调解码后 PCM（16bit 交错） | 回调声道数/采样率可设置，需与发送端声道数一致以拿到完整多声道 | 可复用 |
| `RtcConnectionConfiguration::audioRecvEncodedFrame` + `IAudioEncodedFrameReceiver::onEncodedAudioFrameReceived` | `References/LinuxSampleCodes/v4.4.30.130_verified0805/agora_rtc_sdk/example/encoded_audio/sample_receive_encoded_audio.cpp:139,43` | 订阅端直接接收编码音频帧 | 需同时 `enableAudioRecordingOrPlayout=false`；不解码不播放 | 可复用 |
| 混音音频回调（`onMixedAudioFrame` 路径） | `References/LinuxSampleCodes/0715_v4.2.32/example/mixed_audio/sample_receive_mixed_audio.cpp` | 获取频道混音后音频 | 多声道混音输出恒为 2 声道 | 可复用 |
| Web 端 `ENABLE_MULTIOPUS_CODEC` / `selectChannels` / `getVolumeLevels` | `References/AlexGongs/quick-demo-multiopus/src/Multiopus.vue`（该参考 demo 已于 2026-10 移除，继承者为 `Demo/web/`）；Web 多声道设计文档（ExT-1485178492） | Web 端多声道解码、声道选择播放、逐声道音量 | 需 Chrome 96+；仅偶数声道解码 | 可复用（验证链路） |

**API 间关系**：发送两条路径（PCM 原帧 / 编码帧）互斥，同一条 custom audio track 二选一；多声道私参是发送路径生效的前提（决定 OPUSMC 编码与 payload 协商）；接收三条路径（按用户 PCM、编码帧、混音）可按需独立开启，编码帧路径与播放路径互斥（`enableAudioRecordingOrPlayout=false`）。

**能力差距**：多声道能力完全依赖私有参数开启，无公开 API 与官方文档化；接收端无法从回调中直接获知实际协商声道数（Web 端通过 `getVolumeLevels` 数组长度/getStats 判断，Linux 端依赖日志与 dump 确认）；现有 sample 不做重采样与 ogg/opus 封装解析；SDK/sample 均不提供 4×双声道音轨 → 8 声道 PCM 的布局重排工具，客户源端布局（track 块序）到 Agora 8 声道流的映射需在客户侧集成代码中完成（见需求点9）。

### 6.5 流程图（Optional）

发送链路（以 8 声道 WAV 为例）：

```
8ch WAV 文件 → RIFF 头解析(16/24bit→16bit) → 10ms PCM 帧 → sendAudioPcmData
  → SDK OPUSMC 编码(payload 125, channel_mapping 0..7, 4 streams) → SD-RTN
  → 接收端 OPUSMC 解码 → onPlaybackAudioFrameBeforeMixing(8ch PCM) / Web 逐声道播放
```

---

## 七、详细需求规格

### 需求点1：多声道 PCM 音频发送

系统应支持 Linux SDK 将多声道原始 PCM 音频（3~8 声道、48 kHz、16bit 有符号小端、交错布局）经 OPUSMC 编码后发布到频道，发送节奏为 10 ms/帧。

#### 场景1.1：按声道数发送多声道 PCM

- 前置条件：已创建服务与连接；`connect` 之前已设置 `che.audio.custom_payload_type` 与 `che.audio.custom_channel_num`；已创建 `IAudioPcmDataSender` 与 custom audio track 并 publish
- 当：以 10 ms 间隔调用 `sendAudioPcmData`，每帧 `samplesPerChannel = sampleRate/100`，声道数与 `custom_channel_num` 一致
- 则：远端可收到 OPUSMC 多声道流，解码后声道数与发送端一致
- 且：payload type 与声道数须满足映射：123 对应 3/4 声道、124 对应 5/6 声道、125 对应 7/8 声道；3/5/7 声道必须显式设置 `che.audio.custom_channel_num`

#### 场景1.2：多声道 PCM 与视频同发

- 前置条件：同一连接同时创建视频编码帧 sender 与视频 track
- 当：音频 PCM 线程与视频线程并行发送
- 则：音频多声道与视频均正常发布，互不阻塞
- 且：历史参考实现为 `sample_send_h264_pcm_multiopus`（音频 10ms pacer、视频按帧率 pacer 各自独立）；本仓库当前 Demo 的音视频同发由 `sample_send_mkv_multiopus` 覆盖——音频线程以 10ms/帧发送 8 声道 PCM，视频线程按 MKV 内嵌 mpeg4 的 PTS 节奏转 I420 发送，两线程独立循环、不做严格 AV 同步

#### 场景1.3：非法/不匹配配置

- 前置条件：发送端 PCM 帧声道数与 `custom_channel_num` 不一致，或素材声道数与配置不一致
- 则：编码器输入与配置不符，产生错误编码结果或回退行为（设计文档要求素材声道数必须与 `custom_channel_num` 一致）；sample 层不要求做参数纠错
- 且：采样率非 48000 时不属于多声道支持范围（多声道编解码仅支持 48 kHz）

### 需求点2：多声道 WAV 文件发送

系统应支持从多声道 WAV 文件读取音频并发送：解析标准 RIFF/WAVE 头获取声道数、采样率、位深；16bit PCM 直接发送；24bit PCM 转换为 16bit 后发送；不支持的位深应报错并终止读取。

#### 场景2.1：16bit 多声道 WAV 发送

- 前置条件：WAV 文件为 pcm_s16le、48 kHz、N 声道（N∈[3,8]），多声道私参已按 N 配置
- 当：sample 读取 WAV 头后按 10 ms 帧读取数据并调用 `sendAudioPcmData`（声道数/采样率取 WAV 头值）
- 则：远端收到 N 声道 OPUSMC 流，各声道内容与文件一致

#### 场景2.2：24bit 多声道 WAV 发送

- 前置条件：WAV 文件为 24bit PCM
- 当：sample 读取数据后逐样本做 24bit→16bit 转换（小端读取、符号扩展、右移 8bit）再发送
- 则：远端正常收到多声道流
- 且：输出给 SDK 的位深恒为 16bit（`TWO_BYTES_PER_SAMPLE`）

#### 场景2.3：不支持位深与非 48 kHz 素材

- 当：WAV 位深既非 16bit 也非 24bit
- 则：sample 记录错误日志并停止读取该文件，不发送
- 且：WAV 采样率非 48000（如现有 44.1 kHz 的 4/6 声道 Channel ID 素材）时，sample 不做重采样，并在连接前拒绝该素材；客户须先重采样到 48 kHz

### 需求点3：预编码多声道 Opus 音频发送

系统应支持 Linux SDK 直接发送预编码的多声道 Opus 帧流（绕过 SDK 编码器），用于发送已编码的 8 声道 Opus 素材。

#### 场景3.1：8 声道 Opus 裸流发送

- 前置条件：输入为约定格式的 Opus 裸帧流（非 ogg 封装）：每帧 640 字节 = 20 ms @ 48 kHz 8 声道（参考素材 `8ch_2.opus`）；已设置 `che.audio.custom_payload_type=125`、`che.audio.custom_channel_num=8`；custom audio track 以 `MIX_DISABLED` 创建
- 当：以 20 ms 间隔调用 `sendEncodedAudioFrame`，`EncodedAudioFrameInfo` 为 `codec=AUDIO_CODEC_OPUSMC`、`numberOfChannels=8`、`sampleRateHz=48000`、`samplesPerChannel=960`
- 则：远端收到 OPUSMC 8 声道流，可正常解码播放
- 且：发送节奏由 20 ms pacer 保证；参考实现读到 EOF 后关闭并重新打开文件，循环发送直至退出（PCM/WAV 发送 sample 同为循环读取行为）

#### 场景3.2：输入文件格式约束

- 当：输入文件不是按 640 字节/帧组织的裸 Opus 流（如 ogg 封装或帧长不符）
- 则：sample 不具备解析能力，发送结果未定义；客户须保证素材帧格式与 `EncodedAudioFrameInfo` 严格一致
- 且：sample 仅覆盖 8 声道预编码场景；其他声道数的预编码发送需按对应 payload type/声道数/帧格式另行约定

### 需求点4：多声道编码配置与能力协商

系统应支持通过连接级私有参数开启多声道编码，并通过能力协商确定 OPUSMC 是否可用；在对端不支持时按既定规则回退或强制不回退。

#### 场景4.1：payload type 与声道映射

- 当：设置 `che.audio.custom_payload_type` 为 123/124/125
- 则：默认声道数分别为 4/6/8；3/5/7 声道需在对应 payload type 下额外设置 `che.audio.custom_channel_num`；编码为 Opus 多流：声道映射与流数满足下表

| payload type | 声道数 | 采样率 | channel_mapping | coupled_streams | num_streams |
| --- | --- | --- | --- | --- | --- |
| 123 | 3 | 48000 | 0,1,2 | 1 | 2 |
| 123 | 4 | 48000 | 0,1,2,3 | 2 | 2 |
| 124 | 5 | 48000 | 0,1,2,3,4 | 2 | 3 |
| 124 | 6 | 48000 | 0,1,2,3,4,5 | 3 | 3 |
| 125 | 7 | 48000 | 0,1,2,3,4,5,6 | 3 | 4 |
| 125 | 8 | 48000 | 0,1,2,3,4,5,6,7 | 4 | 4 |

- 且：编码器创建成功日志形如 `MultistreamEncoder channel num:4 rate:48000 coupled:2 channel_mapping:0 1 2 3 bitrate:128000`，可作为配置生效判据

#### 场景4.2：参数设置时机

- 当：设置多声道私参
- 则：`che.audio.custom_payload_type` / `che.audio.custom_channel_num` / `che.audio.custom_sample_rate` 必须在 `connection->connect()` 之前设置（sample 实现在 `createRtcConnection` 之后、connect 之前对 `connection->getAgoraParameter()` 设置）；connect 之后设置不生效
- 且：`che.audio.codec_unfallback` 设置层级不同——必须在 `service->getAgoraParameter()` 上、`createRtcConnection()` 之前设置才生效（见场景4.4）；发送端必须设置 payload type 系列参数；接收端在 123/124/125 协商机制下无需设置 `che.audio.custom_payload_type`

#### 场景4.3：对端不支持多声道时的回退

- 前置条件：Linux 端正以 OPUSMC 发送多声道流
- 当：不支持多声道（未携带 OPUSMC 能力）的端加入频道
- 则：发送端编解码回退到普通 Opus（OPUSFB），多声道能力失效（设计文档记为回退到单声道；Web 侧文档记为回退到双声道，见待确认问题）
- 且：当收到 `PUnsupportedAudioCodec` 广播时，多声道 codec 不执行回退（仅用于关闭 DTX）；Web 端发送时不支持端加入不触发回退，该端听不到声音

#### 场景4.4：强制全频道多声道

- 前置条件（设置位置，硬性要求）：`che.audio.codec_unfallback` 必须通过 `service->getAgoraParameter()` 设置，且在 `service->createRtcConnection()` 之前完成；设置在 `connection->getAgoraParameter()` 上不生效（CSE 实测结论，客户交付验证）
- 当：按 JSON 数组形式设置 `"che.audio.codec_unfallback": [123, 124, 125]`
- 则：整个频道强制使用多声道能力，即使个别参与者不支持多声道，其余参与者仍保持多声道收发
- 且：使用多声道能力时应保证频道内所有参与者均支持多声道，或显式开启该强制配置

#### 场景4.5：声道数一致性

- 当：声道数大于 4
- 则：发送端与接收端的声道数配置必须一致（payload 123 单值时代约束；123/124/125 协商后接收端免配私参，但接收 PCM 回调参数仍需与发送端声道数一致才能拿到完整声道）
- 且：Web 端仅支持偶数声道解码；3/5/7 声道流对 Web 端不适用

### 需求点5：接收多声道音频（解码 PCM 回调）

系统应支持 Linux SDK 订阅远端多声道音频流，解码后按远端用户回调多声道 PCM 数据，供落盘或后处理。

#### 场景5.1：按用户接收多声道 PCM

- 前置条件：已创建连接并订阅远端音频；已通过 `setPlaybackAudioFrameBeforeMixingParameters(numOfChannels, sampleRate)` 设置回调参数
- 当：远端多声道流到达并解码
- 则：`onPlaybackAudioFrameBeforeMixing(channelId, userId, audioFrame)` 按 10 ms 帧回调，`audioFrame.channels` 与发送端声道数一致，`buffer` 为 16bit 交错 PCM
- 且：回调声道数与发送端不一致时无法还原完整多声道（>4 声道约束见场景4.5）

#### 场景5.2：接收 PCM 落盘验证

- 当：sample 将回调 PCM 追加写入文件（单文件达到 100 MB 自动切换新文件）
- 则：落盘文件可用标准工具（如 ffprobe/Audacity 按 48 kHz/N 声道/16bit 导入）逐声道核对内容
- 且：落盘为 sample 层行为，非 SDK 能力承诺

### 需求点6：接收多声道音频（编码帧回调）

系统应支持 Linux SDK 订阅端不解码、直接接收远端 OPUSMC 编码音频帧，用于转发或自定义解码。

#### 场景6.1：编码帧接收

- 前置条件：连接配置 `audioRecvEncodedFrame=true`、`enableAudioRecordingOrPlayout=false`，角色为观众，调用 `subscribeAllAudio()` 或订阅指定流
- 当：远端编码音频帧到达
- 则：`IAudioEncodedFrameReceiver::onEncodedAudioFrameReceived(packet, length, info)` 回调原始编码帧；本仓库 v4.4.30.149 SDK 的 `info` 可用字段为 codec 与 sendTs，业务侧不得依赖回调提供声道数或采样率
- 且：该路径不做播放；与播放路径互斥（`enableAudioRecordingOrPlayout=false`）

### 需求点7：多声道混音接收（双声道输出）

系统应支持对多声道流进行混音输出，混音结果恒为 2 声道：奇数声道混合为一路、偶数声道混合为一路。

#### 场景7.1：混音播放/回调

- 前置条件：接收端使用播放或混音回调（`onPlaybackAudioFrame` / `onMixedAudioFrame`）路径
- 当：收到 N 声道（N≥3）多声道流
- 则：混音输出为 2 声道，奇数声道混合为一路、偶数声道混合为一路（设计文档未约定奇/偶与左/右的对应关系）
- 且：不支持将 N 声道直接渲染为 N 声道播放输出；需逐声道数据时使用需求点5/6 的回调路径

### 需求点8：多声道收发验证与可观测性

系统应支持发送端与接收端的声道级验证手段：编码配置日志、PCM dump 文件、Web 端逐声道音量与 codec 识别。

#### 场景8.1：编码与解码 dump

- 当：在日志目录放置名为 `multi_channel.pcm` 的素材（声道数与 `custom_channel_num` 一致）
- 则：该素材被喂入编码器；日志目录可生成 `before_encoded.pcm`（解码后数据，声道数与编码前一致）与 `mixed_output.pcm`（混音后数据，恒 2 声道）
- 且：dump 文件用于问题定位，声道数以配置为准

#### 场景8.2：Web 端端到端验证

- 前置条件：Web demo 在创建 client 前设置 `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)`，使用 Chrome 96+
- 当：订阅 Linux 端发布的多声道流
- 则：`getVolumeLevels()` 返回数组长度等于实际解码声道数；底层 codec 显示为 `multiopus`；可通过 `selectChannels(channels, "surround"|"stereo"|"mono")` 选择任意声道组合播放
- 且：stereo 模式将 0,2,4,6… 声道混入左、1,3,5,7… 声道混入右；mono 模式全部混为单声道（输出为立体声容器）；Safari/Firefox 加入多声道频道行为未定义（可能无声或仅立体声）

#### 场景8.3：Channel ID 素材逐声道核对

- 当：发送 4/6/8 声道 Channel ID 素材（各声道内容可区分）
- 则：接收端逐声道播放或落盘核对，各声道内容与发送文件对应声道一致、无串声道、无声道丢失
- 且：48 kHz 素材（`8_Channel_ID_signed_16_bit_pcm.wav`）可直接使用；44.1 kHz 素材（`4_Channel_ID.wav`、`6_Channel_ID.wav`）须先重采样

### 需求点9：发送侧 4×双声道音轨到 8 声道流的布局映射（客户源端适配）

系统应支持将客户源端的 4 条双声道（stereo）音轨重排为 Agora 8 声道流后发送；声道顺序以双方确认的布局约定为准，重排工作在送入 SDK 前由客户侧集成代码完成。

#### 场景9.1：客户当前发送布局（事实记录）

- 前置条件：客户源内容为 4 条 stereo 音轨（如 `VictoriaMulti.mkv`：4 × ac3、44.1 kHz、stereo，每轨 L/R 各 16bit）
- 当：客户当前以单次写入 15360 字节（覆盖全部 4 轨）的方式组织 PCM：按 track 块序排列，每轨内部 L/R 逐样本交错（`[T1L,T1R]×N [T2L,T2R]×N [T3…] [T4…]`，每轨 3840 字节，即 15360/4）
- 则：该布局与 Agora 8 声道流的声道顺序约定不一致，不能直接喂给 `sendAudioPcmData`
- 且：15360 字节 = 8 声道 × 16bit × 960 样本 = 20 ms @ 48 kHz；`sendAudioPcmData` 按 10 ms 帧发送，单次写入需拆分/重组为 10 ms 帧

#### 场景9.2：已确认的最终声道映射（2025-04 联调结论）

- 前置条件：双方已通过客户侧声道映射图（客户研发团队提供）与 Agora 侧声道映射图（Agora 侧同步）对齐，沟通记录 `audio_mutil_channel_mapping.txt` 明确结论 "Confirmed that it should be correct."（原文件已随 `References/沟通记录/` 于 2026-10-09 删除，内容存档见 `References/communication_records_backup.md` §2.2）
- 当：将 4 条 stereo 轨映射进 8 声道流
- 则：8 声道帧为**逐样本交错**布局，声道顺序为 CH1=T1L、CH2=T1R、CH3=T2L、CH4=T2R、CH5=T3L、CH6=T3R、CH7=T4L、CH8=T4R；即缓冲区按 `[T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R]` 每样本重复排列；声道对 (0,1)(2,3)(4,5)(6,7) 分别对应轨 1~4 的左右声道（客户 8 声道为 4 个独立立体声对，非 7.1 环绕布局）
- 且：该声道配对与 8 声道 OPUSMC 编码 4 个 coupled stream 的配对 (0,1)(2,3)(4,5)(6,7) 一致（场景4.1 映射表）；`Agora_Order_Expectation.xlsx` 中按声道分块（LLLL…RRRR…）的画法为排查过程中的中间产物，不是最终约定

#### 场景9.3：源采样率适配

- 前置条件：源为 44.1 kHz AC-3 解码后的 PCM
- 当：送入多声道编码前
- 则：必须先重采样到 48 kHz（多声道编解码仅支持 48 kHz，见场景2.3）
- 且：重采样与布局重排的先后顺序不影响正确性，但两者都必须在 `sendAudioPcmData` 之前完成

#### 场景9.4：端到端声道一致性回归

- 前置条件：完成布局映射集成
- 当：发送 8 声道 Channel ID 类素材（或 4 条内容可区分的 stereo 轨）
- 则：接收端逐声道验证（场景8.2/8.3），声道 i 的内容与源端约定轨道一致，无串轨、无左右颠倒
- 且：该回归用例应覆盖客户实际源端（4×stereo）而非仅覆盖现成 8 声道 WAV

---

## 八、需求

### 8.1 需求汇总及验收标准

| 需求项 | 需求描述 | 性能要求 | 体验要求 | 优先级 | 交付时间 | Jira（Optional） | 安全合规审查 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 多声道 PCM 音频发送 | 3~8 声道 48 kHz 16bit PCM 经 OPUSMC 发布，10ms/帧 | 发送节奏 10 ms/帧；性能报告须按 3~8 声道给出测量环境、p95 帧间隔偏差、CPU 和内存基线 | 远端解码声道数与发送一致 | P0 | 已交付（v4.2.32 / v4.4.30.130），后续版本回归 | — | 不涉及 | 当前 Demo：sample_send_pcm_multiopus |
| 多声道 WAV 文件发送 | RIFF 解析，16bit 直发、24bit 转 16bit；非 48 kHz、非法位深和声道数不一致在连接前报错 | 同 PCM 发送 | 日志打印 WAV 头信息（采样率/声道/位深） | P0 | 同上 | — | 不涉及 | 当前 Demo：sample_send_wav_multiopus |
| 预编码多声道 Opus 发送 | 8 声道 Opus 裸流（640B/20ms）经编码帧接口发布 | 20 ms/帧 | 远端可解码播放 | P0 | 同上 | — | 不涉及 | sample：sample_send_opus8；素材 8ch_2.opus |
| 4×双声道音轨→8 声道布局映射 | 客户源端 4 条 stereo 轨（track 块序）重排为 Agora 8 声道逐样本交错流（声道顺序 T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R，已双方确认），44.1 kHz 源重采样至 48 kHz，20 ms 写入拆分为 10 ms 帧 | 重排不引入额外端到端可感知延迟 | 端到端逐声道内容一致、无串轨、无杂音 | P0 | 已验证（2025-04-14 客户确认解决） | — | 不涉及 | 最终映射见场景9.2 |
| 编码配置与能力协商 | payload 123/124/125 与声道映射、回退/强制不回退规则生效 | — | 编码器日志可确认配置生效 | P0 | 同上 | — | 不涉及 | 含 codec_unfallback |
| 接收多声道（PCM 回调） | 按用户回调多声道解码 PCM，可落盘 | 10 ms 帧回调 | 落盘文件逐声道可核对 | P0 | 同上 | — | 不涉及 | 当前 Demo：sample_receive_pcm_multiopus |
| 接收多声道（编码帧） | 订阅端接收 OPUSMC 编码帧 | — | 回调的 codec 与原始帧可落盘核对；声道数/采样率由协商或业务上下文提供 | P1 | 同上 | — | 不涉及 | 当前 Demo：sample_receive_encoded_audio |
| 多声道混音接收 | 混音输出恒 2 声道（奇/偶声道分路） | — | 混音内容可听辨 | P1 | 同上 | — | 不涉及 | sample：sample_receive_mixed_audio |
| 验证与可观测性 | 日志、dump、Web demo 逐声道验证链路可用 | — | getVolumeLevels 数组长度=声道数，codec=multiopus | P0 | 同上 | — | 不涉及 | quick-demo-multiopus |

### 8.2 产出物

| 产出物 | 产出物描述 | Jira（可选） | 是否必须 | 交付时间 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 开发包/服务 | Agora-RTC-x86_64-linux-gnu 与 Agora-RTC-aarch64-linux-gnu，release/4.4.30.130 交付分支正式版（当前最新 4.4.30.149），含 debug 符号包 | ASS-2272 | 必须 | 已交付，后续按修复节奏 | 内网 repo 与 CDN 双通道 |
| 性能测试报告 | 多声道码率/CPU/内存基线（按声道数分档） | 待补充 | 【待明确】 | 待补充 | 弱网 CC 适配结论待补 |
| 体验测试报告 | 多声道收发主观/客观验证报告（逐声道一致性、回退场景表现） | 待补充 | 必须 | 已有 QA 验证（v4.4.30.130，2025-04-23） | Web SDK 4.20.1-200 Test Report 已有 |
| API 文档 | 多声道私有参数与 sample 使用说明 | 待补充 | 必须 | 待补充 | 当前仅有内部设计文档（ExT-1485178709/ExT-1485178492） |
| 最佳实践文档 | 发送/接收集成命令与参数组合（见 FAQ） | 待补充 | 必须 | 待补充 | |
| Demo | Linux sample（multiopus 系列 + receive 系列）与 Web quick-demo-multiopus | — | 必须 | 已交付 | |
| 安全合规审查结论 | 不涉及新增对外数据采集 | — | 【待明确】 | — | |

### 8.3 平台/语言/框架支持需求

| 大项 | 子项 | 是否需要 |
| --- | --- | --- |
| 平台 | iOS | 否 |
| 平台 | Android | 否 |
| 平台 | Windows | 否 |
| 平台 | Mac | 否 |
| 平台 | Harmony OS | 否 |
| 平台 | 通信OS/麒麟 | 否 |
| 平台 | Linux Server | 是（x86_64 + aarch64，C++） |
| 跨平台开发框架 | Electron | 否 |
| 跨平台开发框架 | Flutter | 否 |
| 跨平台开发框架 | React Native | 否 |
| 跨平台开发框架 | Unity | 否 |
| 跨平台开发框架 | Cocos Creator | 否 |
| 跨平台开发框架 | Uniapp | 否 |

（验证链路依赖 Web SDK：agora-rtc-sdk-ng 4.20.1-100/200 多声道版本，Chrome 96+。）

### 8.4 协同需求

| 内容项 | 描述 | Jira（Optional） | 优先级 | 交付时间 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 计量计费需求 | 多声道当前按普通双声道音频计费，未来可能调整 | — | 【待明确】 | — | 来源：设计文档 Pricing 章节 |
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
| VOS/服务端 | OPUSMC 能力协商与 `PUnsupportedAudioCodec` 广播处理依赖服务端配合（已在线上生效） | — | P0 | 已交付 | Web 端加入触发广播、多声道 codec 不回退 |

---

## 九、待确认问题

| # | 问题 | 维度 | 建议确认人 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | 能力协商回退的目标格式：Linux 设计文档记"回退到单声道（mono）"，Web 设计文档记"回退到双声道（dual-channel）"，两处表述不一致，需以实际版本行为为准 | 兼容性 | 研发 / QA | 待确认 |
| 2 | 多声道流在云录制、水晶球等云端链路上的表现与计费影响 | 协同 | PM / 服务端 | 待确认 |
| 3 | 客户 VID、需求 Reporter 与期望交付版本号需在需求 Jira 中补齐 | 流程 | PM | 待确认 |
| 4 | 3/5/7 奇数声道在 Native 端可自适应解码，但客户实际业务是否使用奇数声道（当前素材与 sample 均按 4/6/8 验证） | 场景 | 客户 / PM | 待确认 |
| 5 | 预编码 Opus 发送是否需支持 8 声道以外的预编码素材及 ogg 封装解析 | 功能边界 | 客户 / PM | 待确认 |
| 6 | ~~布局粒度解释待确认~~ **已确认**：最终布局为 8 声道逐样本交错，声道顺序 T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R（见场景9.2）；2025-04 排查的接收端杂音问题根因即发送端 audio layout 错误（源数据逐样本交错 LRLR…，客户侧按平面块序 LLL…RRR… 读取），2025-04-14 客户确认问题解决。另：客户布局约定表中"每轨每写 120 样本"标注与 15360 字节总量（= 960 样本/声道/写）的换算出入不影响结论，以实际集成为准 | 集成约定 | Agora 侧与客户侧双方研发 | 已确认（2025-04-14） |

---

## 十、FAQ

| 问题 | 回复 |
| --- | --- |
| 如何发送 8 声道 WAV？ | `./sample_send_wav_multiopus --channelId <name> --token <token> --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000`（在本仓库 `Demo/linux/out/` 目录运行） |
| 4/6 声道如何选 payload type？ | 4 声道 → 123，6 声道 → 124，8 声道 → 125；3/5/7 声道在对应 payload type 下加 `--custom_channel_num`。设计文档示例：`--audioFile 4_Channel_ID.wav --numOfChannels 4 --custom_channel_num 4 --custom_payload_type 123 --sampleRate 48000` |
| 接收端需要设置多声道私参吗？ | payload 123/124/125 协商机制下不需要；但用 PCM 回调拿完整多声道时，`setPlaybackAudioFrameBeforeMixingParameters` 的声道数须与发送端一致 |
| 44.1 kHz 的 Channel ID 素材能直接发吗？ | 不能按原样发送。多声道编解码仅支持 48 kHz，sample 不做重采样；需先重采样到 48 kHz（如 `8_Channel_ID_signed_16_bit_pcm.wav` 即 48 kHz 版本） |
| 如何判断多声道配置已生效？ | 看日志 `MultistreamEncoder channel num:N rate:48000 coupled:M channel_mapping:... bitrate:...`；或 Web 端订阅后 `getVolumeLevels()` 数组长度等于 N、codec 显示 multiopus |
| 频道里进来一个不支持多声道的端会怎样？ | Linux 发送时触发编解码回退，多声道失效；可用 `"che.audio.codec_unfallback": [123,124,125]` 强制不回退。Web 发送时该端无声 |
| 设置了 `che.audio.codec_unfallback` 但没生效？ | 检查设置位置：该参数必须设在 `service->getAgoraParameter()` 上，且在 `service->createRtcConnection()` 之前；设在 `connection->getAgoraParameter()` 上不生效。注意与 payload type 系列参数（connect 前、connection 级）的设置位置不同 |
| 8ch_2.opus 是什么格式？ | 8 声道 48 kHz Opus 裸帧流（非 ogg 封装），固定 640 字节/帧（20 ms），供 `sample_send_opus8` 直接读帧发送 |
| 为什么发送端能听到声音但接收端只有 2 声道？ | 大概率走了混音/播放路径（混音恒 2 声道）或能力协商回退；检查是否使用 `onPlaybackAudioFrameBeforeMixing` 且回调声道数配置与发送端一致，并确认无回退发生 |
| 客户的 4 条立体声轨怎么接入 8 声道发送？ | 不能直接把 4 轨 track 块序 buffer 喂给 SDK。需先在客户侧完成：① 4×stereo 解码 PCM 重采样到 48 kHz（源为 44.1 kHz AC-3，经 shared memory 中转）；② 按已确认映射重排为 8 声道**逐样本交错**布局：声道顺序 T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R（即声道对 (0,1)(2,3)(4,5)(6,7) 对应轨 1~4）；③ 将 15360 字节（20 ms）写入拆分为 10 ms 帧调用 `sendAudioPcmData`（`numOfChannels=8`、`sampleRate=48000`，配合 payload 125 + `custom_channel_num=8`）。映射正确性用 Channel ID 素材按场景9.4 回归验证 |

---

## 附录

### 研发准入自检

| # | 检查项 | 状态 | 备注 |
| --- | --- | --- | --- |
| 1 | 功能点有明确的输入/输出/边界行为 | ✅ | 发送 3 路径 + 4×双声道布局映射、接收 3 路径、协商规则均有场景化描述 |
| 2 | 平台差异已逐条说明 | ✅ | Linux 发送回退 vs Web 发送不回退；Web 仅偶数声道、Chrome 96+；奇数声道仅 Native/Linux |
| 3 | 有可量化的验收标准（性能+体验） | [待澄清] | 帧节奏/声道数一致性可量化；码率/CPU/内存基线待性能报告 |
| 4 | 客户在用版本和预期交付版本已明确 | ✅ | v4.2.32-20240827、v4.4.30.130-20250409、4.4.30.148/149 |
| 5 | 客户/用户场景已拆解为技术维度问题 | ✅ | 见 6.3 技术维度问题清单 |
| 6 | 现有 API 关联关系已梳理 | ✅ | 见 6.4，均附本地代码/文档来源 |

### 参考资料

- `References/LinuxSampleCodes/description_for_linux_demo.txt`（demo 指引）
- `References/LinuxSampleCodes/0715_v4.2.32/example/multiopus/`（sample_send_h264_pcm_multiopus.cpp、sample_send_h264_pcm_multiopus_8ch_wav.cpp、sample_send_opus8.cpp）
- `References/LinuxSampleCodes/v4.4.30.130_verified0805/agora_rtc_sdk/example/multiopus/`（同上演化版本，默认素材 `public/8_Channel_ID.wav`）
- `References/LinuxSampleCodes/sample_send_opus8.cpp` 与 `References/LinuxSampleCodes/8ch_2.opus`
- 接收 sample：`example/yuv_pcm/sample_receive_yuv_pcm.cpp`、`example/encoded_audio/sample_receive_encoded_audio.cpp`、`example/mixed_audio/sample_receive_mixed_audio.cpp`
- 设计文档：`References/Docs/ExT-1485178709-100726-0959-56_en.md`（Linux SDK 多声道）、`References/Docs/ExT-1485178492-100726-1000-58_en.md`（Web SDK 多声道）
- 交付记录：`References/Docs/ExT-1574700707-170726-0613-20.pdf`（Linux SDK ARM Version 交付记录）
- Web 验证 demo：`References/AlexGongs/quick-demo-multiopus/`（USAGE.md、src/Multiopus.vue）——已于 2026-10 移除，功能由 `Demo/web/` 继承
- 沟通记录：原 `References/沟通记录/` 已于 2026-10-09 删除；全部内容已文字化存档于 `References/communication_records_backup.md` / `communication_records_backup_en.md`（含 2025-04 杂音排查与布局确认全过程、最终确认的声道映射、双方布局表内容、VictoriaMulti.mkv 元数据）
- 音频素材元数据：`References/LinuxSampleCodes/0715_v4.2.32/example/out/public/*_metadata.txt`（ffprobe 实测：8_Channel_ID_signed_16_bit_pcm.wav = 48 kHz/8ch/16bit；6_Channel_ID.wav = 44.1 kHz/6ch(5.1)；4_Channel_ID.wav = 44.1 kHz/4ch）

### 音频素材清单（已验证元数据）

| 素材文件 | 封装/编码 | 采样率 | 声道数 | 位深 | 时长 | 用途 | 来源 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `8ch_2.opus` | Opus 裸帧流（非 ogg），640 字节/帧 | 48000 | 8 | — | 约 112 s（3,575,680 字节 ÷ 640 字节/帧 × 20 ms） | 需求点3 预编码 Opus 发送 | `References/LinuxSampleCodes/8ch_2.opus` |
| `8_Channel_ID_signed_16_bit_pcm.wav` | WAV pcm_s16le | 48000 | 8 | 16bit | 约 14 s | 需求点2 八声道 WAV 发送主验证素材 | ffprobe 元数据（`*_metadata.txt`） |
| `8_Channel_ID_signed_16_bit_pcm.raw.pcm` | raw PCM s16le 交错 | 48000 | 8 | 16bit | 约 14 s | 需求点1 PCM 发送默认素材（由同名 WAV 提取，`Demo/linux/public/`） | 本仓库生成 |
| `8_Channel_ID.wav` | WAV | 48000（Web demo 直发，浏览器解码） | 8 | 16bit（本仓库 `Demo/web/public` 拷贝实测为 24bit，浏览器解码不受影响） | — | Web demo 发布素材；v4.4.30.130 sample 默认音频文件 | `quick-demo-multiopus/public/`（已随 demo 移除；素材见 `Demo/web/public`）、`example/out/public/` |
| `8_Channel_ID_24bit.wav` | WAV pcm_s24le | 48000 | 8 | 24bit | — | 需求点2 场景2.2（24bit→16bit）验证素材（拷贝自 `Demo/web/public/8_Channel_ID.wav`，`Demo/linux/public/`） | 本仓库 |
| `4_Channel_ID_48k.wav` / `6_Channel_ID_48k.wav` | WAV pcm_s16le | 48000 | 4 / 6 | 16bit | 约 5.8 s | Linux sample 4/6 声道发送验证（由 44.1 kHz 源重采样，`Demo/linux/public/`） | 本仓库生成 |
| `6_Channel_ID.wav` | WAV pcm_s16le | 44100 | 6（5.1 布局） | 16bit | 约 5.8 s | 六声道素材；发送前须重采样至 48 kHz | ffprobe 元数据 |
| `4_Channel_ID.wav` | WAV pcm_s16le | 44100 | 4 | 16bit | 约 5.8 s | 四声道素材；发送前须重采样至 48 kHz | ffprobe 元数据 |
| `send_audio_16k_1ch.pcm` | raw PCM | 16000 | 1 | 16bit | — | 普通单声道基线（非多声道） | SDK 包 `test_data/`（sync-data.sh 下载） |
| `VictoriaMulti.mkv` | MKV：1 × mpeg4 视频 + 4 × ac3 音频流 | 44100 | 每轨 2（stereo），共 8 | fltp | 约 4 min | 客户源端 4×双声道承载 8 声道的实际内容样例（布局映射用） | 原 `References/沟通记录/VictoriaMulti.mkv`（已于 2026-10-09 随目录删除；ffprobe 元数据见 `References/communication_records_backup.md` §2.6） |
| 客户布局约定表 / `Agora_Order_Expectation.xlsx` | 布局约定文档（非音频，排查过程中间产物） | — | — | — | — | 客户当前布局（track 块序）与排查中期望布局的对比材料；**最终确认映射以双方确认的声道映射图为准**（逐样本交错，T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R） | 原 `References/沟通记录/`（已于 2026-10-09 删除；内容存档见 `References/communication_records_backup.md` §2.3/§2.4） |

注：Channel ID 系列素材各声道内容可区分，用于接收端逐声道核对；44.1 kHz 素材仅作素材格式参考，多声道发送必须使用 48 kHz 版本。

### 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
| --- | --- | --- | --- |
| v0.1 | 2026-08-12 | 初稿：基于 multiopus 系列 sample 与多声道设计文档收敛发送/接收/音频文件需求 | Kimi Code |
| v0.2 | 2026-08-12 | 特别说明：`che.audio.codec_unfallback` 必须在 `service->getAgoraParameter()` 上且 `createRtcConnection()` 之前设置才生效（场景4.2/4.4、6.4、FAQ） | Kimi Code |
| v0.3 | 2026-08-12 | 新增需求点9：客户 4×双声道音轨（track 块序）→ Agora 8 声道流的布局映射，含客户/Agora 双公布局事实、48 kHz 重采样、10 ms 帧拆分与端到端声道回归；同步更新 2.1 客户原始需求、6.4 能力差距、8.1 验收表、待确认问题6、FAQ、附录素材清单（依据 `References/沟通记录/`） | Kimi Code |
| v0.4 | 2026-08-12 | 依据 `audio_mutil_channel_mapping.txt` 与两张映射图更正最终声道映射：8 声道逐样本交错、声道顺序 T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R（场景9.2 改写）；补充杂音问题根因（交错数据按平面块序读取）与 2025-04-14 已解决结论；待确认问题6 关闭；FAQ、8.1、附录同步更正 | Kimi Code |
| v0.5 | 2026-08-12 | 全文一致性对齐：摘要与 6.1 功能点概述补入 4×双声道布局映射（第⑨项），文档基础信息需求版本更新为 v0.5，研发准入自检表述对齐 | Kimi Code |
| v0.6 | 2026-10-10 | 场景1.2 更新：本仓库已包含音视频同发 sample（`sample_send_mkv_multiopus`，音频 10ms/帧 + 视频按 PTS 节奏双线程独立发送），替换"未包含该音视频同发 sample"的过时表述 | Kimi Code |
