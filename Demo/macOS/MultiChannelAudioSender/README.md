# macOS 音频多声道发送 Demo（MultiChannelAudioSender）

基于 Agora Native SDK macOS 版（`AgoraRtcEngine_macOS` 4.5.2，CocoaPods 集成）的
**发送端** 验证 app：以主播（broadcaster）角色加入频道，将多声道 WAV 素材
（48 kHz / 16bit / N 声道）经自定义音轨循环发布到频道。与接收端
`MultiChannelAudioReceiver`（`Demo/macOS/MultiChannelAudioReceiver/`）配套使用。

[English Documentation](README.en.md)

## 界面语言

支持 English / 中文，默认英文，顶部 Language 选择器切换并持久化（`@AppStorage("uiLanguage")`）。

## 功能说明

- **主播发布**：`.liveBroadcasting` + broadcaster 角色加入，`disableVideo()`，不发布麦克风/摄像头，
  通过 `publishCustomAudioTrack` 发布自定义音轨。
- **内置素材**：`Resources/` 内置三个 Channel ID 素材（各声道内容可区分，来自 `Demo/linux/public/`）：
  - `8_Channel_ID_signed_16_bit_pcm.wav`（8ch，payload 125）
  - `6_Channel_ID_48k.wav`（6ch，payload 124）
  - `4_Channel_ID_48k.wav`（4ch，payload 123）
- **WAV 解析**：极简 RIFF 解析（`WavReader.swift`），支持 PCM（format=1）与
  WAVE_FORMAT_EXTENSIBLE（format=0xFFFE，读取 SubFormat GUID 判定 PCM）；仅接受 48 kHz / 16bit，
  其他格式在加载时报错（与 Linux WAV sender 同口径）。
- **多声道私参**：join 之前设置 `che.audio.custom_payload_type`（≤4ch→123，≤6ch→124，否则→125）、
  `che.audio.custom_channel_num`、`che.audio.custom_sample_rate`。
- **发布节奏**：10 ms/帧 pacer 循环推送（`pushExternalAudioFrameRawData`），界面显示已发送帧数/时长。
- **音频帧 Dump**：`che.audio.frame_dump` 开关（加入频道时应用）。

## 环境要求与构建

- macOS 12.0+，Xcode 14+，CocoaPods，[XcodeGen](https://github.com/yonsm/XcodeGen)（`brew install xcodegen`）

```bash
cd Demo/macOS/MultiChannelAudioSender
xcodegen generate   # 由 project.yml 生成 MultiChannelAudioSender.xcodeproj
pod install         # 集成 AgoraRtcEngine_macOS 4.5.2，生成 xcworkspace
open MultiChannelAudioSender.xcworkspace
```

命令行构建：

```bash
xcodebuild -workspace MultiChannelAudioSender.xcworkspace \
    -scheme MultiChannelAudioSender -configuration Debug build
```

工程使用 ad-hoc 签名（`CODE_SIGNING_ALLOWED=NO`），无沙盒、无 entitlements。

## 使用流程

1. 选择 WAV 素材（加入后不可切换，切换需先 Leave）；
2. 填 App ID（默认预填）/频道名，UID 0 自动分配，点 Join；
3. 点 Publish 开始循环发布；Stop 停止；
4. 接收端验证：用 `MultiChannelAudioReceiver` 或 `Demo/web/` 订阅该频道，
   核对 codec（multiopus）、声道数与逐声道内容。

## 已知限制（2026-10-10 实测结论）

**AgoraRtcEngine_macOS 4.5.2（CocoaPods 公开版）的发送侧自定义音轨输入最高按 2 声道读取**：

- 私参协商正常生效——对端解出对应声道数的 multiopus 流（8ch/6ch/4ch 均验证通过）；
- 但只有前 2 声道携带内容，第 3 声道起静音（接收端逐声道音量与 dump 双重实证）；
- `pushExternalAudioFrameRawData` 与 `pushExternalAudioFrameSampleBuffer`
  （ASBD 自描述 8ch）两种推入方式结论相同；
- 即：多声道**发送**在公开版 macOS SDK 上需要 SDK 侧支持（参照 Linux 定制版的做法），
  本 app 可作为该能力回归验证的现成工具。

另注意 `pushExternalAudioFrameRawData` 的 `samples` 是**全部声道的总样本数**
（官方 API-Examples CustomAudioSource 同样用法），与 Linux `sendAudioPcmData`
的每声道语义不同；macOS 接收侧的多声道解码（含 3/5/7 奇数声道）不受影响，见 receiver README。
