# Agora 音频多声道集成规格（Linux / Web / Native）

- 文档版本：v1.0（2026-11-17）
- 目标读者：使用 Agora SDK 集成音频多声道能力的客户研发工程师
- 适用范围：OPUSMC 音频多声道发布、订阅、声道选择、接收回调及跨端验证
- 关联材料：`Docs/audio_multichannel_test_summary.md`（测试结果与未闭环项）

## 1. 目标与范围

本文说明如何在 Linux、Web 和 Native（macOS / iOS）应用中集成音频多声道能力，并给出必须遵守的版本、参数、数据格式、生命周期和验收要求。

重点场景：

- Linux 服务器向 RTC 频道发布多声道音频，或接收多声道 PCM / 编码帧 / 混音 / 音视频。
- Web 发布和订阅多声道音频，按声道选择播放并读取逐声道音量。
- macOS / iOS 发布和接收多声道音频，进行逐声道控制与 PCM dump 验证。
- 4 条立体声音轨承载 8 声道内容时，在送入 SDK 前完成重采样和声道布局转换。

不包含：

- 云录制、云端转码、计费规则、私有部署或服务端定制方案。
- 严格的音画同步、主观音质评价、物理多声道输出设备映射和长期性能基线。
- Android / Windows Native、Safari / Firefox 的可用性承诺。
- 16 声道及以上能力、任意封装或任意帧格式的预编码 Opus 输入。

## 2. 平台与版本要求

| 平台 | 推荐或已验证版本 | 多声道发送 | 多声道接收 | 关键限制 |
| --- | --- | --- | --- | --- |
| Linux | 当前包 4.4.30.152；历史验证 4.4.30.149，x86_64 / aarch64 | 3～8 声道 | 3～8 声道 | 发送必须配置多声道私参；仅 48 kHz |
| Web | `agora-rtc-sdk-ng@4.24.10+` 主线版 | 4 / 6 / 8 声道 | 4 / 6 / 8 声道 | Chrome 96+，推荐 110+；自定义奇数发布不支持 |
| macOS | 发送 ≥4.5.3；接收已在 4.5.2、4.7.0 验证 | 3～8 声道 | 3～8 声道 | 4.5.2 的自定义音轨输入仅前 2 声道有内容，不可用于多声道发送 |
| iOS | 4.7.0 验证工程 | 已有 4/6/8ch 验证 | 已有 4/6/8ch 验证，但间歇性 1ch 未闭环 | 3/5/7ch 需单独验证；真机仍需回归 |

版本注意事项：

- Web 音频多声道自 **4.24.10** 起并入主线包，可直接使用 `agora-rtc-sdk-ng@4.24.10`，不再需要单独的多声道定制构建。
- 4.20.1-100 / 4.20.1-200、4.24.9 等 Web 特殊版本只用于历史验证。历史实测限制不能在未复测的情况下视为已被 4.24.10 修复。
- Linux 当前包为 4.4.30.152 ZIP（ARM64 位于 `Agora_Native_SDK_for_Linux_SERVER/sdk`，x86_64 位于 `Agora_Native_SDK_for_Linux_SERVER/rtc/sdk`），构建脚本同时兼容旧 TGZ。此前多声道回归主要使用 4.4.30.149，包名、压缩格式和目录结构变化不代表行为发生变化，正式集成前仍应完成目标版本冒烟验证。
- Native 私参名称和行为具有版本与平台依赖性，不应根据其他平台的经验直接推断可用性。正式集成前应在目标 SDK 上完成冒烟验证。
- 生产环境应从业务服务器签发 RTC Token，并确保 Token 中的频道和 UID 与客户端一致。无证书测试项目的空 Token 仅用于测试。

## 3. 通用协议与数据约定

### 3.1 OPUSMC 与采样率

- 多声道编码采用 Opus 多流（OPUSMC）。
- 编码和解码仅支持 **48 kHz**。
- PCM 外部输入通常使用 **16bit 有符号小端、逐样本交错布局**。
- 44.1 kHz 或其他采样率的素材必须在送入 SDK 前重采样到 48 kHz，不能依赖 sample 自动转换。
- Linux WAV sender 支持 16bit 直发和 24bit 转 16bit；其他位深应在连接前拒绝。

### 3.2 Payload type 与声道数

| Payload type | 实际声道数 | 默认声道数 | channel_mapping | coupled_streams | num_streams |
| --- | ---: | ---: | --- | ---: | ---: |
| 123 | 3 | 4 | 0,1,2 | 1 | 2 |
| 123 | 4 | 4 | 0,1,2,3 | 2 | 2 |
| 124 | 5 | 6 | 0,1,2,3,4 | 2 | 3 |
| 124 | 6 | 6 | 0,1,2,3,4,5 | 3 | 3 |
| 125 | 7 | 8 | 0,1,2,3,4,5,6 | 3 | 4 |
| 125 | 8 | 8 | 0,1,2,3,4,5,6,7 | 4 | 4 |

规则：

- 3/5/7 声道必须显式设置 `che.audio.custom_channel_num`，不能只设置 payload type。
- 发送端的 PCM 帧声道数、WAV 头声道数、`custom_channel_num` 和 payload type 必须一致。
- `coupled_streams + num_streams` 等于总声道数。
- 接收端在 123/124/125 协商机制下通常不需要设置发送侧 payload 私参，但 Linux PCM 回调参数仍须与发送端声道数一致，才能获得完整声道。

### 3.3 4×stereo → 8 声道布局

当源内容由 4 条立体声音轨组成时，最终送入 SDK 的 8 声道 PCM 必须按以下顺序**逐样本交错**：

```text
T1L, T1R, T2L, T2R, T3L, T3R, T4L, T4R
```

对应关系：

| 输出声道对 | 源音轨 |
| --- | --- |
| (0,1) | T1L / T1R |
| (2,3) | T2L / T2R |
| (4,5) | T3L / T3R |
| (6,7) | T4L / T4R |

注意：

- 按音轨分块排列的 `[T1L,T1R]×N [T2L,T2R]×N …` 不能直接交给 `sendAudioPcmData`，必须先重排。
- 重采样和重排都必须在调用发送 API 前完成。
- 20 ms 的源数据块需要拆分或重组为 Linux 所需的 10 ms PCM 帧。
- 该布局是 4 个独立立体声对，不应按 7.1 环绕声布局解释。

## 4. Linux 集成规格

参考实现：`Demo/linux/multiopus/`、`Demo/linux/receive/`。

### 4.1 发送 PCM / WAV

发送侧关键 API：

- `IAudioPcmDataSender::sendAudioPcmData`
- `service->createCustomAudioTrack`
- `connection->getLocalUser()->publishAudio`
- 私参 `che.audio.custom_payload_type`、`che.audio.custom_channel_num`、`che.audio.custom_sample_rate`

调用顺序要求：

1. 创建并初始化 Agora service。
2. 创建 RTC connection。
3. 在 `connect()` 前，通过 `connection->getAgoraParameter()` 设置上述三个多声道参数。
4. 建立连接并创建 PCM sender 与 custom audio track。
5. 发布音轨，确认连接成功后按 10 ms 节奏发送 PCM。

每帧要求：

```text
sampleRate = 48000
samplesPerChannel = sampleRate / 100 = 480
bytesPerSample = 2 bytes（16bit）
numOfChannels = 实际声道数
数据布局 = 逐样本交错
```

WAV 输入要求：

- 解析 RIFF/WAVE 头，并以头中的声道数、采样率和位深为准。
- 16bit 直接发送；24bit 逐样本转换为 16bit 后发送。
- 非 48 kHz、非法位深、声道数越界、WAV 头与 CLI 配置不一致、数据不足一帧时，应在连接前拒绝。
- `sample_send_wav_multiopus` 不做重采样，业务侧需提前准备 48 kHz 素材。

### 4.2 发送预编码 Opus

当前 sample 的已验证输入仅覆盖以下格式：

```text
8 声道、48 kHz、20 ms/帧、640 字节/帧的 Opus 裸帧流（非 Ogg 封装）
codec = AUDIO_CODEC_OPUSMC
numberOfChannels = 8
sampleRateHz = 48000
samplesPerChannel = 960
custom audio track = MIX_DISABLED
发送节奏 = 20 ms/帧
```

其他声道数、Ogg 封装或不同帧长需要另行约定和验证，不能直接复用该格式。

### 4.3 编码回退与强制不回退

- Linux 发送时，不支持多声道的端加入可能触发发送端回退为普通 Opus，接收端可观察到 codec 和声道数变化。
- 可设置 `"che.audio.codec_unfallback": [123,124,125]` 强制不回退。
- 该参数必须在 **`service->getAgoraParameter()` 上、`createRtcConnection()` 之前**设置；设在 connection 级不生效。
- 它与 payload 系列参数的设置层级不同：payload 系列参数在 connection 级、`connect()` 前设置。
- 使用多声道时仍应确保频道内所有参与者支持多声道，不能把强制不回退当作兼容不支持端的方法。

### 4.4 接收多声道

| 接收路径 | 关键配置 / API | 输出与限制 |
| --- | --- | --- |
| 按用户解码 PCM | `setPlaybackAudioFrameBeforeMixingParameters` + `onPlaybackAudioFrameBeforeMixing` | 48 kHz、N 声道、16bit 交错 PCM；回调声道数须与发送端一致 |
| OPUSMC 编码帧 | `audioRecvEncodedFrame=true` + `onEncodedAudioFrameReceived` | 直接获得编码帧，不解码、不播放；`enableAudioRecordingOrPlayout=false` |
| 混音 PCM | 播放/混音回调路径 | 输出恒为 2 声道；只能验证混音，不能证明每个源的 N 声道映射 |
| 音视频同收 | 音频 PCM 回调 + `IVideoFrameObserver2::onFrame` | 音频逐用户 PCM，视频 I420；不做本地播放或严格 AV 同步 |

编码帧接收的声道数或采样率应由协商或业务上下文获得，不应假设回调一定提供这些字段。音视频同收时，视频观察者应在订阅回调触发前完成注册。

### 4.5 Linux 配置生效检查

编码器日志可作为配置生效的辅助判据：

```text
MultistreamEncoder channel num:N rate:48000 coupled:M channel_mapping:... bitrate:...
```

但最终验收仍需接收端媒体证据，不能只看日志或进程退出码。

## 5. Web 集成规格

参考实现：`Demo/web/src/Multiopus.vue`、`MultiopusV2.vue`、`MultiopusV3.vue`。

### 5.1 创建 client 与加入频道

当前 Demo 使用：

```ts
const client = AgoraRTC.createClient({
  mode: "rtc",
  codec: "vp8",
  multiopus: true,
});

await client.join(appId, channel, token, uid);
```

早期特殊版本文档中的 `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)` 属于历史集成方式，不应与当前 `multiopus: true` 示例混用。目标版本应以实际包内类型声明和集成验证为准。

### 5.2 发布多声道音频

支持自定义 **4 / 6 / 8 声道**音轨：

```ts
const audioCtx = new AudioContext();
const buffer = await audioCtx.decodeAudioData(
  await (await fetch("8_Channel_ID.wav")).arrayBuffer()
);

const dest = audioCtx.createMediaStreamDestination();
dest.channelCount = 8;
dest.channelCountMode = "explicit";
dest.channelInterpretation = "discrete";

const source = audioCtx.createBufferSource();
source.buffer = buffer;
source.connect(dest);
source.start();

const track = await AgoraRTC.createCustomAudioTrack({
  mediaStreamTrack: dest.stream.getAudioTracks()[0],
  channelCount: 8,
});

await client.publish(track);
```

要求：

- 素材、AudioContext 输出和 `channelCount` 必须一致；该参数不会自动补齐或校正声道。
- 4ch 素材声明 8ch 时，额外路可能为静音或复制，均不是独立的 8ch 内容。需要 up-mix 时应在应用的音频图中显式完成。
- 自定义 3/5/7ch 发布属于 unsupported；即使出现 `publish onSuccess`，也不能证明形成了有效远端流。
- 改变声道数时，应完整停止发布、销毁旧 track，并重新创建和订阅，不能复用旧 Multiopus transformer。

### 5.3 订阅与声道选择

```ts
const track = await client.subscribe(user, "audio");
await track.play();
track.selectChannels([0, 1, 2, 3], "surround");
```

三种模式：

| 模式 | 行为 |
| --- | --- |
| `surround` | 按多声道协议输出所选声道 |
| `stereo` | 按零起声道序号的奇偶分路：0,2,4,6… 到左，1,3,5,7… 到右 |
| `mono` | 将所选声道混为单声道，输出使用立体声容器 |

`selectChannels()` 调用前必须验证：

```text
0 <= channelIndex < remoteChannelCount
```

还应确保输入为整数、非空并去重。发现任何非法项时，应拒绝整个选择并保留之前的有效配置。历史实测中，4ch 流选择 `[7]` 可以返回成功，但实际输出为数字静音；`getVolumeLevels()` 或 `selectChannels onSuccess` 都不能证明输出有效。

### 5.4 音量与输出验证

- `getVolumeLevels()` 返回选择/混音前的源声道音量，可用于辅助观察实际解码声道数。
- 音量是 dB 宽量程：`(dBFS_RMS + 100) / 100`。只有精确数字零显示 0%，约 -90 dB 的不可闻残余也可能显示约 10%。
- 判定选择后的播放结果时，应测实际输出 PCM 或试听，不能只看源音量条。
- 订阅初期音量数组可能从 1～2 路扩展到实际声道数，应在读数稳定后判定，避免把初始化窗口误判为单声道回退。

### 5.5 Web 奇数声道边界

必须区分：

1. **Web 自定义 3/5/7 发布**：不支持，可能发布成功但无有效远端流。
2. **Web 接收 Native/Linux 3/5/7 流**：历史版本会按 N+1 偶数声道误解码，末两个声道内容相同，页面可能误判多声道成功。

Demo 的 `oddChannelFix.ts` 仅用于验证：检测末两声道恒等后修正显示和播放。它不改变 SDP 或底层解码，也可能与末两个声道恰好同内容的真实偶数流冲突。生产集成不应把该补丁视为正式修复；在 4.24.10 上必须重新进行补丁开关对照和 Native 对照验证。

### 5.6 浏览器与清理要求

- Chrome 96+，推荐 110+；Safari / Firefox 不在支持范围内。
- 停止发布、取消订阅、离开和页面卸载时，应停止音源、关闭本地 track、取消订阅并清理旧状态。
- `user-unpublished` 与 `user-left` 可能连续到达，应使用 in-flight 防重避免重复退订。只忽略目标用户已经离开所导致的预期竞态，其他错误继续上报。
- 重发或换 UID 后，不应沿用旧 track、旧声道数或旧选择状态。

## 6. Native（macOS / iOS）集成规格

参考实现：`Demo/macOS/`、`Demo/iOS/`。

### 6.1 发送多声道

发送侧关键步骤：

1. 在 join 前设置 `che.audio.custom_payload_type`、`custom_channel_num`、`custom_sample_rate`。
2. 创建自定义音轨并挂接发布。
3. 使用 48 kHz / 16bit 多声道 PCM，以 10 ms 节奏调用 `pushExternalAudioFrameRawData`。

macOS 的 `samples` 参数表示**所有声道的总样本数**，与 Linux `sendAudioPcmData` 的每声道样本数语义不同，必须分别处理。

版本要求：

- macOS 4.5.2 的自定义音轨输入被钳制为 2 声道，不能用于多声道发送。
- 发送多声道要求 macOS SDK ≥4.5.3。
- iOS 4.7.0 已覆盖 4/6/8ch 验证，但 Web→iOS 的间歇性 1ch 未闭环，不能仅凭 join/publish 成功认定发送链路稳定。

### 6.2 接收与逐声道控制

- 以观众角色加入并自动订阅音频/视频，不发布本地轨道。
- Native 在 123/124/125 协商机制下自适应解码，接收端通常不需要设置发送侧 payload 私参。
- 仅发布音频或视频的远端才进入 host 列表；不发布的观众不占 host 名额。

逐声道音量私参示例：

```swift
kit.setParameters("""
{"che.audio.neteq.adjust_decoded_volume":{"remoteUid":12345,"index":3,"volume":100}}
""")
```

规则：

- `remoteUid` 为目标远端 UID。
- `index` 为 **1-based** 声道序号。
- `volume` 为 0～100，0 表示静音，100 表示原始音量。
- 该参数作用于本端解码后、播放前，不影响其他用户。
- 如需只听第 N 声道，将第 N 路设为 100，其余路设为 0。

音频帧 dump：

```swift
kit.setParameters("{\"che.audio.frame_dump\":true}")
```

dump 可用于逐声道 RMS、峰值和源声道相关性分析。macOS 已验证文件为 48 kHz / N 声道 / 16bit，约 120 MB 封顶；具体路径和命名随 SDK 约定，不应硬编码为跨平台契约。

### 6.3 Native 奇数声道与版本差异

- Native/Linux 的 3/5/7 奇数声道互通已在四类链路验证通过，接收端可保持真实声道数。
- Web 的奇数声道限制不能反向推断为 Native 也受限。
- iOS 的 3/5/7 奇数声道仍需独立验证；macOS / Linux 的结果不能直接外推到 iOS。
- 接收端不能依赖统一回调获得实际协商声道数，应通过 PCM dump、日志或其他实测手段确认。

## 7. 跨端互通与容量

### 7.1 互通注意事项

- Linux 发送时，不支持端加入可能触发回退；Web 发送时不触发回退，但不支持端听不到声音。
- 多声道源、payload、声道数和接收回调配置必须一致。
- 编码帧、混音和 PCM dump 是不同验证层级，不能相互替代。
- Web 的 4/6/8ch 结果与 Native 的 3/5/7ch 结果应分开记录，不能用一类链路覆盖另一类声道边界。

### 7.2 默认上行容量

未开启 50 人、128 人 hosts 扩容时：

| 场景 | 默认每频道同时发流上限 | 说明 |
| --- | ---: | --- |
| 音视频场景 | 17 名主播 | 第 18 名主播发视频时，不保证所有流可见，也不保证固定错误码 |
| 纯音频场景 | 32 名主播 | 第 18 路纯音频仍在上限内；第 33 路需要验证实际媒体 |

规则：

- 限制按**频道和发送端**计算，不是整个 App ID 合计。
- 仅作为观众加入不占发流名额。
- 一路 4/6/8 声道 OPUSMC 发布只占一个音频发送端名额，不按声道数倍增。
- 需要超过默认上限时，应提前申请扩容并在业务层控制上麦人数，不能依赖 `ERR_TOO_MANY_BROADCASTERS` 或 publish 报错控制容量。

混合端实测表明：31 个 Linux sender + 1 个 Web sender 的 32 路 4ch 纯音频上行可正常工作；第 33 路出现 join/publish 成功且本端 RTP 增长，但 receiver 无有效 track。停止一个既有 sender 后，同一路 Web 33 恢复。因此验收必须同时检查新流和既有流的真实媒体。

## 8. 异常输入与生命周期要求

| 场景 | 集成要求 |
| --- | --- |
| 非 48 kHz 素材 | 在连接前重采样；不直接发布 |
| 声道数与 payload / WAV 头 / PCM 帧不一致 | 在连接前拒绝并修正配置，不依赖 SDK 自动纠错 |
| Web 自定义 3/5/7ch | 创建音轨前拒绝 |
| 源实际声道数与声明不一致 | 不计为独立多声道内容；显式完成 up-mix 或更正声明 |
| `selectChannels` 越界或混合非法项 | 拒绝整个选择并保留之前有效配置 |
| 停止后重发、改变声道数、换 UID | 完整销毁旧 track 和订阅状态，再创建新链路 |
| 重复 join/publish/subscribe/unsubscribe | 使用按钮状态和 in-flight 防重，确保同一时刻只有一套操作 |
| publish 成功但无远端流 | 以 receiver 的 `user-published`、真实 track 和媒体内容判定，不视为有效发布 |
| 页面卸载或进程退出 | 停止音源、关闭 track、取消订阅、断开连接并清理定时器 |

## 9. 验收要求

### 9.1 基础媒体验收

- 4/6/8ch：分别验证发布、订阅、解码和逐声道内容；Native/Linux 需要时增加 3/5/7ch。
- 使用完整 Channel ID 素材循环，不能只取短窗口，避免尚未轮播到某声道而误判。
- Web 记录 codec、信令 `channelCount`、音量数组长度、逐声道峰值和实际选择后输出。
- Native/Linux PCM dump 应为 48 kHz、16bit、预期声道数，并检查逐声道峰值/RMS及与源声道的对应关系。
- 编码帧只验证编码回调和帧落盘；混音只验证 1/2ch 输出；AV 纯音频源不要求视频文件。
- 视频场景检查 readyState、宽高、`currentTime` / 解码帧持续前进以及非黑帧、非静止帧。

### 9.2 生命周期与异常验收

- 覆盖先加入后发布、先发布后加入、停止/重发、取消/重订、离开/重进和页面卸载。
- 覆盖快速重复操作、重复 UID、非法 URL、非法声道数、非法采样率和不匹配素材。
- 每轮检查旧 track 是否清理、是否出现重复退订、未处理 Promise 或持续静音。
- 结果应区分通过、功能限制、刻意错配、观察项、未执行和严格失败；自动化 runner 错误不能直接等同于 SDK 产品缺陷。

### 9.3 当前版本回归门禁

对于 Web SDK 4.24.10 主线版，至少重新执行：

1. 4/6/8ch 的 Web→Web、Web→Linux、Web→Native、Linux→Web P0 链路。
2. 奇数声道补丁开关与 Native 对照。
3. 停发/重发、取消/重订、离开和页面卸载的清理回归。
4. `selectChannels` 越界、不支持声道数发布、源与声明不匹配。
5. 目标业务所需的多 sender 和容量边界；不能把混合端结果外推为 32 个 Web 客户端的并发验证。

## 10. 结果解释与参考边界

- `Docs/audio_multichannel_test_summary.md` 汇总了历史实测结果、已闭环问题和未覆盖项。
- 大量媒体证据来自 Web 4.20.1 / 4.24.9 及 Linux、macOS 的指定版本；这些结果用于说明能力边界，不构成所有后续版本的自动保证。
- Web→iOS 的间歇性 1ch、Web→macOS 的部分集成失败、MS-16 / MS-28 等测试缺口仍需单独跟踪。
- 弱网、真实 Token 过期、iOS 真机、跨浏览器、2 小时以上长稳、主观试听和严格 lipsync 未包含在已有通过结论中。
