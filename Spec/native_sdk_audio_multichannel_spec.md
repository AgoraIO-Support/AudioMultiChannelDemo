# Native SDK 音频多声道收发 产品需求文档（PRD 初稿）

## Native SDK（macOS）音频多声道接收验证与发送验证需求

> 摘要：Native SDK（macOS）需要支持多声道音频的接收与发送验证。**接收侧**：以观众（audience）角色加入频道，接收远端主播的 OPUSMC 多声道流（payload type 123/124/125，48 kHz，3~8 声道）或普通立体声/单声道流，通过私参逐声道选择收听与调节音量（`che.audio.neteq.adjust_decoded_volume`），支持远端视频显示、音/视频接收码率走势与音频帧 dump（`che.audio.frame_dump`）。**发送侧**：以主播（broadcaster）角色将多声道 WAV 经自定义音轨循环发布（`createCustomAudioTrack` + `pushExternalAudioFrameRawData` + 多声道私参）；公开版 4.5.2 曾存在自定义音轨输入钳制为 2 声道的限制，**4.5.3 起已解除**（2026-10-12 多版本回归实测，见需求点7 与待确认问题1）。Native SDK 与 Linux SDK 多声道实现同源，接收端在 123/124/125 协商机制下自适应解码（含 3/5/7 奇数声道），无需任何多声道私参。配套验证 app 为本仓库 `Demo/macOS/`（`MultiChannelAudioReceiver` 接收 + `MultiChannelAudioSender` 发送，SwiftUI + AgoraRtcEngine_macOS，CocoaPods 集成，当前集成 4.7.0，中/英文 UI 默认英文），本文档将其能力事实、行为边界与验收口径收敛为需求基线。

---

## 一、文档基础信息

| 项目 | 内容 |
| --- | --- |
| PM | 待补充 |
| TO | 待补充 |
| PMO | 待补充 |
| 需求 Jira | 待补充（多声道项目背景单见 Linux 侧 spec） |
| 需求版本 | v1.6 |

---

## 二、需求背景

### 2.1 客户原始需求

多声道项目场景下，发送侧（Linux SDK，3~8 声道 OPUSMC）与 Web 验证链路已交付（见 `Spec/linux_sdk_audio_multichannel_spec.md`、`Spec/web_sdk_audio_multichannel_spec.md`），Native 侧需要配套的可视化验证手段：一款 macOS app 以观众角色纯接收远端多声道流，验证 Native SDK 能正常听到、能按声道选择收听并调节音量、能同步观看远端视频，且兼容普通双声道流；另配一款 macOS 发送端 app 用于验证 Native 发送路径（公开版 4.5.2 发送输入曾钳制为 2 声道，**4.5.3 起解除**，见需求点7 场景7.2）。

| 内容项 | 详情 | 备注 |
| --- | --- | --- |
| 客户(VID) | （客户名称已隐去） | |
| 需求业务线 | 广电级音视频传输 / 云端制作 | 依据交付记录推断 |
| 客户业务场景 | macOS 上以观众角色订阅远端多声道流，逐声道试听核对声道内容与顺序 | |
| 当前客户在用方案 | 本仓库 `Demo/macOS/`：`MultiChannelAudioReceiver`（接收验证）+ `MultiChannelAudioSender`（发送验证） | |
| 客户做了哪些尝试 | 基于 Agora API-Examples macOS `JoinChannelAudio` 示例中的私参用法（`che.audio.frame_dump`、`che.audio.neteq.adjust_decoded_volume`）完成验证 | |
| 客户在用版本 | AgoraRtcEngine_macOS 4.5.2（CocoaPods） | |

### 2.2 当前产品方案

| 方案 | API | 功能描述 | 方案定位 | 方案成熟度 | 方案支持当前需求存在的 Gap |
| --- | --- | --- | --- | --- | --- |
| 多声道接收与播放 | 观众角色加入 + 自动订阅音频（默认开启） | Native SDK 自适应解码 OPUSMC 多声道流并播放 | Native 现成能力 | 已在 macOS 4.5.2 构建验证 | 无公开的多声道开关/事件；接收端无法从回调获知实际协商声道数 |
| 逐声道选择收听/音量 | 私参 `che.audio.neteq.adjust_decoded_volume` | 按远端 uid + 声道序号（1-based）调节解码后音量（0~100%），实现逐声道开关 | Native 私参能力 | 已在 API-Examples 参考代码中出现 | 依赖私有参数，无公开 API 与文档化 |
| 音频帧 dump | 私参 `che.audio.frame_dump` | 解码后音频帧落盘，用于离线逐声道核对 | Native 私参能力 | 同上 | dump 文件位置/命名随 SDK 内部约定 |

---

## 三、同类历史需求

- Linux SDK 音频多声道收发需求：同一多声道项目的服务端落地（见 `Spec/linux_sdk_audio_multichannel_spec.md` 第三章）。
- Web SDK 多声道需求（2024-03）：Web 端发布/订阅验证链路（见 `Spec/web_sdk_audio_multichannel_spec.md`）。
- Ambisonic 空间音频项目：多声道编解码能力来源于该项目（复用 Opus Ambisonic 多流模式）；Native SDK 与 Linux SDK 多声道实现同源（设计文档 ExT-1485178709 明确 Linux 侧仅做简单平台适配）。

## 四、需求类型和价值

| 内容项 | 详情 |
| --- | --- |
| 是否通用需求 | 否（验证工具驱动）；能力本身为 Native SDK 现成能力，可向有类似多声道诉求的集成用户推广 |
| 需求类型 | 新需求（接收验证工具落地与验收基线收敛） |
| 需求价值 | 提供 Native 侧逐声道收听验证手段，与 Linux 发送端、Web 验证端形成发送—接收—验证闭环；3/5/7 奇数声道流只能由 Native/Linux 端验证（Web 仅偶数声道解码） |

---

## 五、竞品分析 Competitive Analysis

无明确竞品线路（验证工具，材料中无竞品 PK 信息）。

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
| API 接口（描述性语言） | 引擎创建与频道加入（`AgoraRtcEngineKit`、`joinChannel`，观众角色、liveBroadcasting）；远端音频状态回调（`remoteAudioStateChangedOfUid`）；逐声道解码音量私参（`che.audio.neteq.adjust_decoded_volume`）；音频帧 dump 私参（`che.audio.frame_dump`）；远端音频统计回调（`remoteAudioStats`） | 私参均无公开文档 |
| 功能点概述 | 接收侧：① 观众角色纯接收加入频道（不发布麦克风/摄像头、自动订阅音频与视频）；② 多声道/立体声/单声道流接收播放（自适应解码，接收端免私参）；③ 远端 host 列表（按音频或视频发布状态收录，观众不入列；覆盖 音频+视频 / 仅音频 / 仅视频 三种 host）；④ 逐声道选择收听与音量调节（1-based 声道序号、0~100%，仅对有音频的 host 下发）；⑤ 音频帧 dump 开关；⑥ 声道数显示选择（纯 UI 行数控制，对齐发送端声道数）；⑦ 远端视频显示（host 发布视频时渲染所选 uid 画面）+ 音/视频接收码率走势图；⑧ 中/英文 UI（默认英文）。发送侧：⑨ 主播角色多声道 WAV 循环发布（内置 4/6/8 声道 Channel ID 素材，payload 123/124/125 私参协商，10ms pacer；4.5.2 曾输入钳制为 2 声道，4.5.3 起解除，见需求点7） | |
| 客户使用逻辑概述 | 接收验证：启动发送端（Linux sample / macOS sender / Web publisher）→ receiver app 填 App ID/频道名以观众加入 → 选择远端 host uid → 逐声道开关/音量试听核对 → 需要时开启 dump 落盘核对。发送验证：sender app 选择内置 WAV → 主播加入 → Publish 循环发布 → 用 receiver/Web 端订阅核对 | |

### 6.2 体验性需求

| 内容项 | 描述 | 备注 |
| --- | --- | --- |
| API 接口（描述性语言） | 可观测性依赖界面日志区（加入/离开、远端进出、连接状态、SDK 错误）、音频+视频两路远端接收码率的数值与实时走势图（各取最近 60 个 `remoteAudioStats`/`remoteVideoStats` 样本的 sparkline，音频绿色、视频蓝色）及 dump 文件 | 现成能力 |
| 体验指标概述 | 逐声道内容与发送素材逐声道一致（Channel ID 素材逐声道可辨、无串声道）；声道开关/音量改动即时生效 | 阈值见第八章 |
| 性能指标概述 | 音量私参调用即时生效（人耳可感知层面无延迟）；接收码率随声道数上升（8 声道 OPUSMC 约 250~280 kbps，可作声道数旁证） | 详细指标待性能测试补充 |

### 6.3 客户/用户场景技术拆解

**RTC 场景覆盖**

- macOS（观众角色）订阅 Linux 端发布的 OPUSMC 多声道流（payload 123/124/125）。
- macOS（观众角色）订阅普通立体声/单声道流（兼容性场景）。
- macOS（主播角色）发布多声道 WAV 到频道，供 Web / Native / Linux 端订阅验证（4.5.3 起全声道可用；4.5.2 存在 2 声道钳制，见需求点7）。
- 频道内多个远端 host 并存时，按 uid 分别做逐声道控制。

**用户场景覆盖**

- 验证人员在 macOS receiver app 选择远端 host，逐声道开关试听，核对声道顺序与内容（Channel ID 素材），并排观看远端视频与音/视频码率走势。
- 验证人员开启音频帧 dump，落盘后离线逐声道核对。
- 验证人员在 macOS sender app 选择 4/6/8 声道 WAV 发布，用 Web/receiver 端核对协商声道数与逐声道内容。
- 演示场景下切换中/英文界面。

**技术维度问题清单**

| # | 技术维度 | 问题 | 答案 | 确认人 |
| --- | --- | --- | --- | --- |
| 1 | 采样率 | 多声道是否支持非 48 kHz？ | 否，多声道编解码仅支持 48 kHz（编码/解码均是） | 设计文档 |
| 2 | 声道数范围 | Native 接收端支持哪些声道数？ | 3/4/5/6/7/8 自适应解码（含奇数声道）；16 声道及以上不支持 | 设计文档 |
| 3 | 接收端私参 | 接收多声道需要设置 payload type / channel num 吗？ | 不需要。123/124/125 协商机制下接收端免配；私参只需发送端设置 | 设计文档 |
| 4 | 声道序号 | `adjust_decoded_volume` 的 `index` 从 0 还是 1 开始？ | 1-based（1~8），参考实现与用户确认用法一致 | 参考代码 |
| 5 | 音量语义 | `volume` 取值范围与含义？ | 0~100 百分比；100 = 原始音量，0 = 静音；作用于解码后、播放前，仅影响本端 | 参考代码 |
| 6 | 声道数感知 | 接收端能否从回调获知远端流实际声道数？ | 不能。SDK 无此回调；demo 以声道数选择器手动对齐 UI 行数（纯显示控制） | 代码事实 |
| 7 | host 识别 | 远端列表如何只包含主播？ | 以远端音频发布状态为准：`remoteAudioStateChangedOfUid` 音频 starting/decoding 时收录，`didOfflineOfUid` 移除；观众不发布音频故永不入列；host 静音不移除 | 代码事实 |
| 8 | 能力协商回退 | 频道内存在不支持端时 Native 接收侧表现？ | 回退发生在发送侧（Linux 发送时回退为非多声道编码）；Native 接收端被动跟随协商结果；可用 `che.audio.codec_unfallback` 在发送侧强制不回退 | 设计文档 |

### 6.4 现有 API 关联梳理

| API / 能力 | 事实来源 | 职责 | 能力范围 | 与本需求的关系 |
| --- | --- | --- | --- | --- |
| `AgoraRtcEngineKit.sharedEngine(with:delegate:)` + `joinChannel` | `Demo/macOS/MultiChannelAudioReceiver/MultiChannelAudioReceiver/AgoraManager.swift` | 引擎创建与加入频道 | 观众角色 + `.liveBroadcasting`，`enableVideo()`（仅接收）、`publishMicrophoneTrack = false`、`publishCameraTrack = false`、`autoSubscribeAudio = true`、`autoSubscribeVideo = true` | 可复用 |
| `remoteAudioStateChangedOfUid` / `didOfflineOfUid` | `Demo/macOS/.../AgoraManager.swift`；SDK 头文件 `AgoraRtcEngineDelegate.h` | 远端 host 列表维护 | 音频 starting/decoding 收录、offline 移除；`didJoinedOfUid` 仅记日志不入列 | 可复用 |
| 私参 `che.audio.neteq.adjust_decoded_volume` | Agora API-Examples macOS `JoinChannelAudio` 示例；`Demo/macOS/.../AgoraManager.swift` | 逐声道解码音量 | `{"remoteUid":…, "index":1~8, "volume":0~100}`，逐声道下发 | 可复用 |
| 私参 `che.audio.frame_dump` | 同上 | 音频帧 dump | `true/false`；dump 到 SDK 默认位置 | 可复用 |
| `remoteAudioStats` / `remoteVideoStats` | `Demo/macOS/.../AgoraManager.swift` | 远端音/视频接收码率显示与走势图 | 按 uid 刷新；音频码率可旁证流是否存活与声道数档位；视频 stats 同时提供流宽高（驱动视频等比渲染） | 可复用 |
| `createCustomAudioTrack(.mixable)` + `publishCustomAudioTrackId` + `pushExternalAudioFrameRawData` | `Demo/macOS/MultiChannelAudioSender/.../AgoraSenderManager.swift`；官方 API-Examples macOS `CustomAudioSource` | 自定义音轨多声道发布 | `samples` 为全部声道总样本数（≠ Linux 每声道语义）；4.5.2 输入钳制 2 声道，4.5.3 起解除（2026-10-12 实测） | 可复用（4.5.3 起无限制） |
| 私参 `che.audio.custom_payload_type` / `custom_channel_num` / `custom_sample_rate` | sender app；Linux 侧 spec | 发送端多声道编码协商 | RtcEngine 上 `setParameters`、join 之前设置；协商在公开版 4.5.2 实测生效 | 可复用 |
| 混音输出（恒 2 声道，奇/偶声道分路） | 设计文档 ExT-1485178709（Native/Linux 同源） | 多声道流的默认播放混音 | 本 demo 用逐声道音量私参替代混音验证，不依赖混音布局 | 可复用（背景事实） |

**API 间关系**：频道加入是所有能力的前置；逐声道音量与 dump 私参在引擎创建后即可设置（建议在加入频道前后均可，demo 在加入成功后按 uid 下发）；host 列表驱动 UI 的声道控制对象选择；发送侧私参与自定义音轨必须在 join 之前设置/创建，`publishCustomAudioTrackId` 把音轨挂进发布。

**能力差距**：多声道能力完全依赖私有参数，无公开 API 与文档化；接收端无法获知实际协商声道数（无回调，无 getStats 声道字段暴露到界面）；dump 文件位置/命名随 SDK 内部约定（macOS 4.5.2 实测见 FAQ）；macOS 4.5.2 无 `didOccurWarning` 回调（仅 `didOccurError`）；公开版 4.5.2 发送侧自定义音轨输入曾钳制为 2 声道，**4.5.3 起已解除**（2026-10-12 多版本回归实测，见需求点7/待确认问题1）。

### 6.5 流程图（Optional）

接收链路（以 8 声道为例）：

```
Linux 发送端 OPUSMC(125, 8ch) → SD-RTN
  → macOS app 观众加入（autoSubscribeAudio）→ Native 自适应解码（8ch PCM）
  → adjust_decoded_volume 按 uid+声道序号调节（解码后、播放前）
  → 播放（仅启用声道可闻）/ frame_dump 落盘核对
```

---

## 七、详细需求规格

### 需求点1：观众角色纯接收加入频道

系统应支持 macOS app 以观众角色加入频道并自动订阅远端音频/视频，不发布任何本地音频/视频。

#### 场景1.1：加入与离开

- 前置条件：已填写 App ID（默认预填，可修改并持久化）、频道名；Token 可选（无证书项目留空）；UID 填 0 自动分配
- 当：点击加入频道
- 则：以 `.liveBroadcasting` + 观众角色加入，`enableVideo()`（接收视频需要，不发布摄像头：`publishCameraTrack=false`）、`publishMicrophoneTrack=false`、自动订阅音频与视频；界面显示连接状态与本端 UID
- 且：远端多声道流到达后自动解码播放（默认全部声道启用 100%）；远端视频流到达后在「远端视频」区渲染（需求点6）

#### 场景1.2：兼容普通立体声/单声道流

- 当：远端为普通 stereo/mono 流（非 OPUSMC）
- 则：同样自动解码播放，无需任何额外配置；声道数选择器选 2（或 1）对齐 UI 行数即可逐声道控制

### 需求点2：逐声道选择收听与音量调节

系统应支持对指定远端 host 的音频流按声道调节解码音量，实现逐声道选择收听。

#### 场景2.1：逐声道开关与音量

- 前置条件：已加入频道且远端列表中存在 host
- 当：对声道 N（1-based）拨动开关或拖动音量滑杆（0~100%）
- 则：立即下发 `{"che.audio.neteq.adjust_decoded_volume":{"remoteUid":<uid>,"index":N,"volume":V}}`；该声道按 V% 音量播放（0 = 静音）
- 且：「选择收听第 1、2、3 声道」= 声道 1/2/3 设 100，其余设 0；该私参只影响本端听到的声音，不影响频道内其他用户

#### 场景2.2：切换控制对象与状态一致性

- 当：切换选中的远端 uid
- 则：把当前全部 8 行声道状态（含隐藏行）重新下发到新 uid，保证行为确定
- 且：声道数选择器只控制 UI 行数，切换它不下发任何 SDK 参数；对超出流实际声道数的 `index` 设置在 SDK 侧不产生效果

### 需求点3：音频帧 dump

系统应支持开启/关闭解码音频帧落盘，用于离线逐声道核对。

#### 场景3.1：dump 开关

- 当：打开「Dump 音频帧」开关
- 则：下发 `{"che.audio.frame_dump":true}`，SDK 将解码后音频帧写入其默认位置（随 SDK 内部约定）
- 且：开关状态在加入频道时一并应用；关闭下发 `false`

### 需求点4：远端 host 列表

系统应支持只列出真正在发布音频**或视频**的远端主播，观众角色的端不入列；仅视频 host 也可被收录并选中观看。

#### 场景4.1：host 收录与移除

- 当：远端音频状态回调中某 uid 音频进入 starting/decoding，或远端视频状态回调中某 uid 视频进入 starting/decoding
- 则：该 uid 收录进远端列表（自动选中第一个）；有音频的 host 显示实时音频接收码率，有视频的 host 渲染画面并显示视频接收码率
- 且：host 静音（音频 stopped）不移除（静音 host 仍是 host，可能取消静音）；`didOfflineOfUid`（含 host 转观众）移除；`didJoinedOfUid` 仅记日志、不作为收录依据

#### 场景4.2：仅视频 host

- 当：host 只发布视频、不发布音频
- 则：该 host 经视频状态回调收录入列表；选中后控制栏显示"仅视频（无音频声道可控）"提示，不下发任何 `adjust_decoded_volume` 参数；监控栏正常显示视频与视频码率走势
- 且：该 host 后续开始发布音频时（音频 starting/decoding），控制栏自动切换为逐声道控制

### 需求点5：验证与可观测性

系统应支持接收侧的声道级验证手段：逐声道试听、接收码率、日志与 dump。

#### 场景5.1：Channel ID 素材逐声道核对

- 前置条件：发送端播放 Channel ID 素材（各声道内容可区分）
- 当：逐声道单独启用收听
- 则：声道 i 的内容与发送端约定声道一致，无串声道、无声道丢失
- 且：接收码率可作声道数旁证（8 声道 OPUSMC 约 250~280 kbps；普通单声道约 40~80 kbps）

### 需求点6：远端视频显示

系统应支持在远端 host 发布视频时，于右侧「监控」（Monitor — UID xxx）栏渲染所选远端用户的视频画面；监控栏与控制栏（左栏：选择远端 + 逐声道控制）分离，视频等比撑满栏宽（按 `remoteVideoStats` 上报的流实际宽高比，默认 16:9）；纯音频 host 的监控栏仅显示码率走势与"仅音频"提示，不出现视频占位。

#### 场景6.1：视频接收与渲染

- 前置条件：以观众角色加入频道（`enableVideo()` + `autoSubscribeVideo=true`，仍不发布本地任何轨道）
- 当：所选远端 host 发布视频（`remoteVideoStateChangedOfUid` 进入 starting/decoding）
- 则：监控栏渲染该 uid 的画面（`setupRemoteVideo`，renderMode=fit，宽度撑满栏宽、高度按流宽高比）；日志记录"开始发布视频"
- 且：切换选中的远端 uid 时，同一视图重新绑定新 uid，监控栏标题同步切换；host 停止发布视频（stopped/failed）时视频消失，监控栏仅余码率走势

#### 场景6.2：无视频 host

- 当：所选远端 host 只发布音频
- 则：监控栏不出现视频（不占布局空间），仅显示"仅音频"提示与码率走势；音频逐声道控制不受影响

### 需求点7：多声道 WAV 发送（macOS sender app）

系统应支持 macOS app 以主播角色将多声道 WAV 素材经自定义音轨循环发布，供 Web / Native / Linux 端订阅核对协商声道数与逐声道内容。

#### 场景7.1：多声道 WAV 发布

- 前置条件：选定内置 WAV 素材（4/6/8 声道 Channel ID，48 kHz / 16bit；RIFF 解析含 WAVE_FORMAT_EXTENSIBLE）；join 之前已按声道数设置 `che.audio.custom_payload_type`（≤4→123，≤6→124，否则→125）/ `custom_channel_num` / `custom_sample_rate` 并创建 mixable 自定义音轨
- 当：以 10 ms/帧 pacer 调用 `pushExternalAudioFrameRawData`（`samples` = 全部声道总样本数；读到 EOF 循环），`publishCustomAudioTrackId` 挂接发布
- 则：频道内产生 OPUSMC 流，对端解出的声道数与素材一致（Web 端 codec=multiopus、channelCount=N；已实测 8/6/4ch）
- 且：发布节奏实时（100 帧/秒），界面显示已发送帧数/时长

#### 场景7.2：发送侧声道数能力的版本边界（2026-10-10 初测 / 2026-10-12 多版本回归）

- 当：按场景7.1 在公开版 AgoraRtcEngine_macOS **4.5.2**（CocoaPods）上发布 N>2 声道
- 则：协商正常（对端解出 N 声道 multiopus），但只有前 2 声道携带内容、ch3+ 静音（Web 逐声道音量与 receiver dump 双重实证；raw push 与 CMSampleBuffer 两种推入路径、`setExternalAudioSource(channels: N)`、`che.audio.input_channels` 私参均不改变结果）
- 且：**该钳制自 4.5.3 起解除**——2026-10-12 回归（sender app 命令行自动化发布 8ch Channel ID 素材 + Web 端 `getVolumeLevels()` 20 s 逐声道峰值采样，判据：内容级 ≈56~85% / 素材 dither 底噪 ~10% / 数字零 =0%）：4.5.2 对照组 CH2-7 恒 0（复现钳制）；4.5.3 / 4.6.0 / 4.6.2 / 4.7.0 的 8 声道均出现内容级峰值（71~85%），发送端全程无 push 报错；4.7.0 另补测 4ch/6ch 素材（payload 123/124），全声道内容送达。macOS SDK 无 4.6.7 版本（4.6.2 之后为 4.7.0）
- 汇总文档：`Test/macos_sender_multi-channel_audio_test_result.md`；sender app 保留为该能力的回归工具（命令行自动化用法见其 README）

### 需求点8：双语界面

系统应支持中/英文界面切换，默认英文，选择持久化。

#### 场景8.1：语言切换

- 当：通过顶部 Language 选择器切换
- 则：所有可见文案立即以目标语言重新渲染；选择持久化到下次启动
- 且：已输出的历史日志行保持输出时的语言，不回溯切换

---

## 八、需求

### 8.1 需求汇总及验收标准

| 需求项 | 需求描述 | 性能要求 | 体验要求 | 优先级 | 交付时间 | Jira（Optional） | 安全合规审查 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 观众纯接收加入 | 观众角色加入、不发布、自动订阅 | — | 状态与本端 UID 可见 | P0 | 已交付（Demo/macOS） | — | 不涉及 | |
| 逐声道选择收听/音量 | `adjust_decoded_volume` 1-based、0~100%、按 uid | 改动即时生效 | 逐声道内容可辨、无串声道 | P0 | 同上 | — | 不涉及 | |
| 音频帧 dump | `frame_dump` 开关落盘 | — | 落盘文件可离线核对 | P1 | 同上 | — | 不涉及 | dump 位置随 SDK 约定 |
| host 列表 | 仅收录发布音频或视频的 host（含仅视频 host） | — | 观众永不入列；仅视频 host 选中后不下发音量参数 | P0 | 同上 | — | 不涉及 | |
| 立体声/单声道兼容 | 普通流免配置接收与逐声道控制 | — | 声道数选择器对齐行数 | P0 | 同上 | — | 不涉及 | |
| 远端视频显示 | host 发布视频时在右侧监控栏渲染所选 uid 画面（等比撑满栏宽），纯音频 host 不出现视频 | — | 按需出现；切换 uid 即时重绑、监控栏标题联动；停发即回收 | P1 | 同上 | — | 不涉及 | enableVideo + autoSubscribeVideo，仍不发布本地轨道 |
| 双语 UI | 中/英文切换，默认英文，持久化 | — | 切换即时渲染 | P1 | 同上 | — | 不涉及 | |
| 多声道 WAV 发送 | 主播角色经自定义音轨循环发布 4/6/8 声道 WAV，私参协商（payload 123/124/125） | 10ms/帧实时推送 | 对端解出对应声道数 multiopus 且逐声道内容齐全 | P1 | 已交付（Demo/macOS/MultiChannelAudioSender） | — | 不涉及 | 4.5.2 输入钳制为 2 声道（ch3+ 静音），**4.5.3 起解除**（2026-10-12 回归），见需求点7 场景7.2 |

### 8.2 产出物

| 产出物 | 产出物描述 | Jira（可选） | 是否必须 | 交付时间 | 备注 |
| --- | --- | --- | --- | --- | --- |
| Demo | macOS 验证 app 两个（Demo/macOS 下 `MultiChannelAudioReceiver` 接收 + `MultiChannelAudioSender` 发送，SwiftUI + AgoraRtcEngine_macOS，CocoaPods，当前集成 4.7.0） | — | 必须 | 已交付 | 均构建验证通过；receiver 真频道收听验证已完成（2026-10-10，见变更记录 v0.2）；sender 真频道发布验证已完成（同日，含 2 声道钳制结论，见变更记录 v1.2）；sender 发送侧 4.5.2/4.5.3/4.6.0/4.6.2/4.7.0 版本回归 + 4ch/6ch 素材与双端 4.7.0 原生互通均完成（2026-10-12，见变更记录 v1.5/v1.6） |
| API 文档 | 两个私参的用法说明（Demo/macOS README） | 待补充 | 必须 | 已交付（README 形式） | 未官方文档化 |

### 8.3 平台/语言/框架支持需求

| 大项 | 子项 | 是否需要 |
| --- | --- | --- |
| 平台 | iOS | 否（设计同源，理论可复用，未验证） |
| 平台 | Android | 否（同上） |
| 平台 | Windows | 否（同上） |
| 平台 | Mac | 是（macOS 12.0+，SwiftUI，CocoaPods） |
| 平台 | Harmony OS | 否 |
| 平台 | 通信OS/麒麟 | 否 |
| 平台 | Linux Server | 否（见 Linux 侧 spec 与 demo） |
| 跨平台开发框架 | Electron / Flutter / React Native / Unity / Cocos / Uniapp | 否 |

（验证链路配合方：Linux 发送端见 `Demo/linux/`；Web 验证端见 `Demo/web/`。）

### 8.4 协同需求

| 内容项 | 描述 | Jira（Optional） | 优先级 | 交付时间 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 计量计费需求 | 多声道当前按普通双声道音频计费，未来可能调整 | — | 【待明确】 | — | 来源：设计文档 Pricing 章节 |
| 云录制需求 | 不涉及 | — | — | — | |
| 水晶球 | 多声道通话的 codec/声道数展示未确认 | — | 【待明确】 | — | |
| VOS/服务端 | OPUSMC 能力协商依赖服务端配合（已在线上生效） | — | P0 | 已交付 | |

---

## 九、待确认问题

| # | 问题 | 维度 | 建议确认人 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | ~~Native 发送侧多声道：设计与 Linux 同源（理论支持 3~8 声道 OPUSMC 发送），但本仓库未含 Native 发送 demo，发送路径未实测~~ **已实测（2026-10-10，新增 `Demo/macOS/MultiChannelAudioSender`）**：私参协商在公开版 macOS 4.5.2 上正常生效（对端解出对应声道数的 multiopus 流，8/6/4ch 均验证），但**自定义音轨输入最高按 2 声道读取**——只有前 2 声道携带内容，第 3 声道起静音；`pushExternalAudioFrameRawData` 与 `pushExternalAudioFrameSampleBuffer`（ASBD 自描述 8ch）结论一致；`setExternalAudioSource(channels: 8)` 与私参 `che.audio.input_channels: 8` 均返回 0 但不解除钳制（二进制 strings 可见 `che.audio.input_channels`/`output_channels` 存在）。结论：多声道发送需要 SDK 侧支持（参照 Linux 定制版），sender app 可作为该能力的回归工具。**更新（2026-10-12）**：4.5.3 / 4.6.0 / 4.6.2 / 4.7.0 回归全部通过——8 声道内容完整送达（Web 端逐声道峰值 71~85%，4.5.2 对照组 CH2-7 恒 0 复现钳制），钳制自 4.5.3 起解除（修复点定位为 4.5.2→4.5.3）；4.7.0 补测 4ch/6ch 素材全声道送达。~~声道序号映射~~ 已闭环（2026-10-12：4.7.0 原生链路 dump 与源 WAV 包络互相关，dumpCHi↔srcCHi 同序号，NCC 0.67~0.78、跨声道 ≈0） | 功能边界 | 研发 / QA | 已关闭（4.5.3 起可用） |
| 2 | `che.audio.neteq.adjust_decoded_volume` 在 iOS/Android/Windows Native SDK 上是否同名可用（macOS 4.5.2 已用） | 兼容性 | 研发 / QA | 待确认 |
| 3 | 接收端获知实际协商声道数的正式手段（回调或 getStats 字段），当前 UI 只能手动对齐 | 能力差距 | 研发 / PM | 待确认 |
| 4 | ~~dump 文件的具体位置与命名规则~~ **已实测（2026-10-10，macOS 4.5.2）**：dump 落在 `~/Library/Logs/`；按远端用户解码输出 `audio_dump_playback_before_mixed_0_<yyyy_MM_dd_HHmmss>_<uid>.wav`（48 kHz / N 声道 / 16bit，约 120 MB 封顶停止，Leave 重进按新时间戳另建）；混音 `audio_dump_playback_mixer_0.wav`（恒 2 声道）；另有 `audio_dump_play_0.wav`、`far_in_48000hz_2ch.wav`。其他平台/版本的位置未逐一验证 | 可观测性 | 研发 | 已确认（macOS 4.5.2） |
| 5 | 奇数声道（3/5/7）Native 解码为设计支持，本项目实际联调仅覆盖 4/6/8；3/5/7 端到端验证待补 | 场景 | QA | 待确认 |

---

## 十、FAQ

| 问题 | 回复 |
| --- | --- |
| 接收端要设多声道私参吗？ | 不需要。payload 123/124/125 协商机制下 Native 接收端自适应解码（含 3/5/7）；payload type / `custom_channel_num` 只需发送端设置（见 `Demo/linux/README.md`） |
| 如何只听第 3 声道？ | 声道 3 开关打开、音量 100，其余声道关闭（或设 0）；等价于 `{"che.audio.neteq.adjust_decoded_volume":{"remoteUid":<uid>,"index":3,"volume":100}}` + 其余 index 设 0 |
| 「声道数」选择器改了没反应？ | 它是纯显示控制，只决定 UI 渲染几行声道控制：SDK 无回调告知远端流实际声道数，需按发送端素材手动对齐。未选远端用户时切换无可见效果；隐藏声道保持原音量状态继续生效（选 2 不会静音 3~8 声道，静音请用对应行开关） |
| 远端列表里为什么没有某个端？ | 列表只收录正在发布音频的 host（音频 starting/decoding）；观众角色的端不发布音频，永不入列 |
| dump 文件在哪里？ | macOS 4.5.2 实测在 `~/Library/Logs/`：按远端用户解码输出 `audio_dump_playback_before_mixed_0_<时间戳>_<uid>.wav`（48 kHz/N 声道/16bit，约 120 MB 封顶），混音输出 `audio_dump_playback_mixer_0.wav`（恒 2 声道）。可直接用 ffprobe/Audacity 按声道数核对 |
| 频道里进来不支持多声道的端会怎样？ | 回退发生在发送侧（Linux 发送回退为非多声道编码）；Native 接收端被动跟随。需强制不回退时在发送侧设 `che.audio.codec_unfallback`（设置层级要求见 Linux 侧 spec 场景4.4） |

---

## 附录

### 研发准入自检

| # | 检查项 | 状态 | 备注 |
| --- | --- | --- | --- |
| 1 | 功能点有明确的输入/输出/边界行为 | ✅ | 加入/逐声道控制/dump/host 列表/双语均有场景化描述 |
| 2 | 平台差异已逐条说明 | ✅ | Native 自适应解码（含奇数声道）vs Web 仅偶数声道；接收免私参 vs 发送需私参 |
| 3 | 有可量化的验收标准（性能+体验） | [待澄清] | 逐声道一致性可验证；码率/CPU/内存基线待性能报告 |
| 4 | 客户在用版本和预期交付版本已明确 | ✅ | AgoraRtcEngine_macOS 4.5.2（在用）；发送侧多声道需 ≥4.5.3（4.5.2 钳制 2 声道，4.5.3 起解除，2026-10-12 回归） |
| 5 | 客户/用户场景已拆解为技术维度问题 | ✅ | 见 6.3 技术维度问题清单 |
| 6 | 现有 API 关联关系已梳理 | ✅ | 见 6.4，均附代码/文档来源 |

### 参考资料

- `Demo/macOS/`（两个自包含 app 工程：`MultiChannelAudioReceiver/` 接收验证 app、`MultiChannelAudioSender/` 发送验证 app；各自含 Podfile、xcodeproj、源码与 README）
- Agora API-Examples macOS `JoinChannelAudio` 示例（两个私参用法来源）
- 设计文档：`References/Docs/ExT-1485178709-100726-0959-56_en.md`（Native/Linux 多声道同源实现）、`References/Docs/ExT-1485178492-100726-1000-58_en.md`（Web SDK 多声道）
- Linux 侧规格：`Spec/linux_sdk_audio_multichannel_spec.md`（发送、协商、布局映射）；Web 侧规格：`Spec/web_sdk_audio_multichannel_spec.md`

### 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
| --- | --- | --- | --- |
| v0.1 | 2026-10-08 | 初稿：基于 Demo/macOS 接收验证 app 与 Native/Linux 同源设计文档，收敛 Native SDK（macOS）多声道接收与逐声道收听验证需求 | Kimi Code |
| v0.2 | 2026-10-10 | 真频道收听验证完成：Linux `sample_send_mkv_multiopus`（AWS，8 声道 OPUSMC payload 125 + 视频，uid 777001）→ macOS app 观众加入 `mkvav1010a`。结果：① 观众加入/host 收录/码率显示正常（接收码率 ~262 kbps，符合 8 声道 OPUSMC 250~280 kbps 区间旁证）；② 逐声道开关/音量私参下发全部返回 0，且 dump 实证即时生效（ch1 置 0 后 dump 中 ch1 立即无声、恢复 100 后约 1 s 内回升，静音窗口与操作时刻秒级吻合）；③ `che.audio.frame_dump` 落盘 `~/Library/Logs/audio_dump_playback_before_mixed_0_<时间戳>_777001.wav`，ffprobe 实测 48 kHz/8 声道/16bit，astats 八声道 RMS -13.6~-15.5 dB 各不相同（无串声道、无丢声道），混音 dump 恒 2 声道；④ dump 位置/命名/封顶行为写入 README 与 FAQ，待确认问题 4 关闭（macOS 4.5.2 范围）。证据截图：`Test/macos_receiver_8ch_verified_20261010.png` | Kimi Code |
| v0.3 | 2026-10-10 | 新增需求点6（远端视频显示）：加入流程由 `disableVideo()` 改为 `enableVideo()` + `autoSubscribeVideo=true`（仍不发布本地轨道），`remoteVideoStateChangedOfUid` 跟踪 host 视频发布状态，所选 uid 的画面经 `setupRemoteVideo`（renderMode=fit）渲染到「远端视频」区；无视频 host 显示占位提示。场景1.1、6.1 功能点概述、8.1 验收表同步更新 | Kimi Code |
| v0.4 | 2026-10-10 | UI 布局改版：① 视频区不再常驻，仅在所选 host 发布视频时内嵌于「远端多声道控制」区右侧，宽度按 `remoteVideoStats` 流宽高比适配（消除大黑边），纯音频 host 只显示"仅音频"标记；② 声道滑杆限制最大宽度；③ 接收码率增加实时走势图（最近 60 个 `remoteAudioStats` 样本 sparkline）。需求点5/6 与 8.1 同步更新 | Kimi Code |
| v0.5 | 2026-10-10 | UI 细化：① 远端用户下拉 UID 改为 `UID 777001` 直显（修复 SwiftUI `Text` 插值自动加千分位导致的 "777,001" 误读）；② 控制区按"选择远端 → 监控（码率图+视频）→ 逐声道播放控制"三层分组，组间加 Divider 与小组标题；③ 码率走势图增加 Y 轴刻度（最大值/0）与"当前值 · 最近约 2 分钟"标题 | Kimi Code |
| v0.6 | 2026-10-10 | 信息架构改版为「控制 | 监控」双栏：左栏（固定 430pt）为操作区（选择远端 + 声道数 + 逐声道播放控制），右栏（弹性宽）为监控区——标题即 `Monitor — UID xxx` 标明监控对象，视频等比撑满栏宽（流宽高比驱动），码率走势图在视频下方通栏；纯音频 host 监控栏仅显示码率图与"仅音频"提示。消除此前右半屏大面积空白与分组层级过弱的问题 | Kimi Code |
| v0.7 | 2026-10-10 | 监控栏增加视频接收码率走势图：`remoteVideoStats` 携带 receivedBitrate/宽/高，视频图（蓝色，紧邻视频画面下方）与音频图（绿色）并列，均带 Y 轴（最大值/0）与"当前值 · 最近约 2 分钟"标题；音频码率标题由 "Recv bitrate" 更名为 "Audio recv" 以消歧 | Kimi Code |
| v0.8 | 2026-10-10 | UI 微调：「声道数」选择器从控制栏顶部移至 "Channel playback" 标题行右侧，与其控制的声道行直接同组，关联关系可见 | Kimi Code |
| v0.9 | 2026-10-10 | UI 对齐修正：① 视频由水平居中改为左对齐，与下方码率图共用左边缘基准（消除"漂浮感"）；② Join 区 App ID/Token/Channel ID 输入框限宽 520pt，不再拉满全宽 | Kimi Code |
| v1.0 | 2026-10-10 | host 媒体类型全覆盖：`RemoteUser` 拆分为 hasAudio/hasVideo 独立跟踪，视频状态回调也可收录 host——修复此前"仅视频 host 永远进不了列表"的盲区。三种 host 形态：音频+视频（逐声道控制 + 视频 + 双码率图）、仅音频（逐声道控制 + "仅音频"提示 + 音频码率图）、仅视频（"仅视频"提示不下发音量参数 + 视频 + 视频码率图）。需求点4 改写并新增场景4.2。实测：AV 双全（mkvav1010a）与仅音频（audonly1010a，AWS `sample_send_wav_multiopus`，257 kbps 8 声道）两路径通过；仅视频路径本仓库无对应发送端，按代码走查确认 | Kimi Code |
| v1.1 | 2026-10-10 | 启动空状态布局修复：中区去掉为视频预留的 minHeight（空态不再出现三段式大空洞），Log 面板固定 160pt（ScrollView 不再贪婪扩张吃掉全部余量），底部 Spacer 吸收空白，内容顶部聚拢；空态提示去重——控制栏只显示 "No remote host yet" 短提示，详细引导文案（先启动发送端）仅保留在监控栏 | Kimi Code |
| v1.2 | 2026-10-10 | 新增 Native 发送侧 demo `Demo/macOS/MultiChannelAudioSender`（broadcaster 角色发布内置 4/6/8 声道 Channel ID WAV，`createCustomAudioTrack(.mixable)` + `pushExternalAudioFrameRawData`，10ms pacer 循环）。实测关闭待确认问题 1：公开版 macOS 4.5.2 私参协商正常（对端解出对应声道数 multiopus），但自定义音轨输入被钳制为 2 声道（ch1/2 有内容、ch3+ 静音，raw push 与 CMSampleBuffer 两种路径一致），多声道发送需 SDK 侧支持。另记录 API 差异：push 的 `samples` 为全部声道总样本数（Linux 为每声道） | Kimi Code |
| v1.3 | 2026-10-10 | Demo/macOS 目录重组：receiver 的 Podfile/xcodeproj/README 从目录顶层收进 `MultiChannelAudioReceiver/` 工程根，与 `MultiChannelAudioSender/` 平级对称；顶层新增目录级 README（双 app 总览）。6.4 表格与参考资料路径同步更新；receiver 在新位置 pod install + 构建验证通过 | Kimi Code |
| v1.4 | 2026-10-10 | 系统性收编：标题与摘要由"纯接收"扩为收发双侧；新增需求点7（多声道 WAV 发送 + 场景7.2 公开版 2 声道钳制实测限制），双语界面顺延为需求点8；6.1 功能点概述加第⑨项、6.3 场景覆盖加发送验证、6.4 加自定义音轨发送 API 行与"能力差距"钳制条目；8.1/8.2 验收表与产出物加 sender app | Kimi Code |
| v1.5 | 2026-10-12 | 发送侧多版本回归（4.5.2 对照 / 4.6.0 / 4.6.2 / 4.7.0）：sender app 新增命令行自动化（`-autoChannel/-autoUid/-autoWav/-autoJoin/-autoPublish/-autoQuitAfter`，日志实时镜像 stdout），逐版本切换 Podfile 后发布 8ch Channel ID 素材，Web 端 `getVolumeLevels()` 20 s 逐声道峰值采样判定（内容级 ≈56~85% / dither 底噪 ~10% / 数字零 =0%）。结果：4.5.2 复现 2 声道钳制（CH2-7 恒 0），4.6.0/4.6.2/4.7.0 八声道全通（峰值 71~85%）——发送侧多声道自 4.6.0 起可用（v1.6 修正为 4.5.3）；macOS SDK 无 4.6.7 版本。摘要、2.1、6.1/6.3/6.4、需求点7 场景7.2、8.1/8.2、待确认问题1（关闭）、自检#4 同步更新；汇总文档 `Test/macos_sender_multi-channel_audio_test_result.md` | Kimi Code |
| v1.6 | 2026-10-12 | 遗留项补测：① **4.5.3 实测 8 声道全通**（峰值 [79,79,74,83,77,78,78,75]），发送侧修复点由 4.6.0 修正为 **4.5.3**（修复点为 4.5.2→4.5.3，已用 Podfile.lock 与 framework 版本号双重核实）；② 4.7.0 补测 4ch（payload 123，峰值 [78,78,79,84]）与 6ch（payload 124，峰值 [76,77,76,84,78,80]）素材，全声道内容送达；③ 同日上午已完成 sender+receiver 双端 4.7.0 升级与原生互通回归（receiver 命令行自动化 + dump NCC 声道 1:1 核对）。全部"4.6.0 起"表述修正为"4.5.3 起"；待确认问题1 残留项清零 | Kimi Code |
