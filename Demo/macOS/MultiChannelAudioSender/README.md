# macOS 音频多声道发送 Demo（MultiChannelAudioSender）

基于 Agora Native SDK macOS 版（`AgoraRtcEngine_macOS` 4.7.0，CocoaPods 集成；Podfile 可切换版本）的
**发送端** 验证 app：以主播（broadcaster）角色加入频道，将多声道 WAV 素材
（48 kHz / 16bit / N 声道）经自定义音轨循环发布到频道。与接收端
`MultiChannelAudioReceiver`（`Demo/macOS/MultiChannelAudioReceiver/`）配套使用。

[English Documentation](README.en.md)

## 界面语言

支持 English / 中文，默认英文，顶部 Language 选择器切换并持久化（`@AppStorage("uiLanguage")`）。

## 功能说明

- **主播发布**：`.liveBroadcasting` + broadcaster 角色加入，`disableVideo()`，不发布麦克风/摄像头，
  通过 `publishCustomAudioTrack` 发布自定义音轨。
- **内置素材**：`Resources/` 内置六个素材（各声道内容可区分，来自 `Demo/linux/public/`）：
  - `8_Channel_ID_signed_16_bit_pcm.wav`（8ch，payload 125）
  - `6_Channel_ID_48k.wav`（6ch，payload 124）
  - `4_Channel_ID_48k.wav`（4ch，payload 123）
  - `odd_3ch.wav` / `odd_5ch.wav` / `odd_7ch.wav`（3/5/7 奇数声道，payload 123/124/125 + `custom_channel_num`，
    由 8ch 素材经 ffmpeg channelmap 截取生成）
- **WAV 解析**：极简 RIFF 解析（`WavReader.swift`），支持 PCM（format=1）与
  WAVE_FORMAT_EXTENSIBLE（format=0xFFFE，读取 SubFormat GUID 判定 PCM）；仅接受 48 kHz / 16bit，
  其他格式在加载时报错（与 Linux WAV sender 同口径）。
- **多声道私参**：join 之前设置 `che.audio.custom_payload_type`（≤4ch→123，≤6ch→124，否则→125）、
  `che.audio.custom_channel_num`、`che.audio.custom_sample_rate`。
- **发布节奏**：10 ms/帧 pacer 循环推送（`pushExternalAudioFrameRawData`），界面显示已发送帧数/时长。
- **音频帧 Dump**：`che.audio.frame_dump` 开关（加入频道时应用）。
- **奇数声道警示**：选中 3/5/7 声道素材（下标 3/4/5，条目带 ⚠）时发送面板显示橙色警示条——
  奇数声道流仅 Native/Linux 端可验证，Web SDK 会误解码（按 N+1 声道、末声道复制幻影、界面误判
  多声道 ✓），结果不可信。实测与补丁边界见 `Docs/audio_multichannel_test_summary.md` §4.2。

## 环境要求与构建

- macOS 12.0+，Xcode 14+，CocoaPods，[XcodeGen](https://github.com/yonsm/XcodeGen)（`brew install xcodegen`）

```bash
cd Demo/macOS/MultiChannelAudioSender
xcodegen generate   # 由 project.yml 生成 MultiChannelAudioSender.xcodeproj
pod install         # 集成 AgoraRtcEngine_macOS（版本见 Podfile），生成 xcworkspace
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

## 命令行自动化（回归测试用）

通过启动参数（映射进 `UserDefaults`）免去 UI 操作，App 日志同时无缓冲镜像到 stdout：

```bash
./MultiChannelAudioSender.app/Contents/MacOS/MultiChannelAudioSender \
    -autoChannel <频道> -autoUid <uid> -autoWav 0 \
    -autoJoin YES -autoPublish YES -autoQuitAfter 300
```

- `autoWav`：素材下标（0=8ch, 1=6ch, 2=4ch, 3=3ch, 4=5ch, 5=7ch）；`autoWavPath`：自定义 WAV 文件路径（优先于 autoWav，
  须 48 kHz / 16bit / 1~8 声道）；`autoJoin`/`autoPublish`：启动后自动加入并在 join 成功后自动发布；
  `autoQuitAfter`：秒数，>0 时到点自动退出。
- `autoUid` 仅接受数字；非法值（非数字）静默回退为 0（自动分配 uid）。
- 配合 Web 端 `?channel=<频道>&uid=<webuid>&subTo=<autoUid>` 打开订阅页即可做版本回归
  （采样 `window.__mcRemoteTrack.getVolumeLevels()` 逐声道峰值判定，见下文矩阵）。

## 发送侧多声道能力：版本实测矩阵（2026-10-12 更新）

方法：本 app 以 8 声道 Channel ID 素材（内容逐声道轮播，循环 14 s）自动发布，Web 验证端
（`Demo/web/`）订阅后以 200 ms 间隔对 `getVolumeLevels()` 连续采样 20 s 取各声道峰值判定——
内容级 ≈ 56~85%（有内容）；~10% 为 16bit 素材静音段的 dither 底噪（无声的正常读数）；
数字零 = 0%（内容在发送端被钳制丢失）。

| AgoraRtcEngine_macOS | 协商 | Web 端逐声道内容 | 结论 |
| --- | --- | --- | --- |
| 4.5.2 | multiopus 8ch 正常 | 仅 CH0/CH1 有内容（且内容雷同），CH2-7 恒为数字零 | ✗ 2 声道钳制 |
| 4.5.3 | multiopus 8ch 正常 | 8 声道均出现内容级峰值（74~83%），底噪 ~10% | ✓ 已修复（**修复点**） |
| 4.6.0 | multiopus 8ch 正常 | 8 声道均出现内容级峰值（75~84%），底噪 ~10% | ✓ 已修复 |
| 4.6.2 | multiopus 8ch 正常 | 8 声道均出现内容级峰值（75~85%），底噪 ~10% | ✓ 已修复 |
| 4.7.0 | multiopus 8ch 正常 | 8 声道均出现内容级峰值（71~83%），底噪 ~10%；4ch/6ch 素材同验全通 | ✓ 已修复 |

各版本实测数据与声道 1:1 证据见 `Docs/audio_multichannel_test_summary.md` §4.6。
注：macOS 版 SDK 不存在 4.6.7（CocoaPods 与发版说明中 4.6.2 之后为 4.7.0；iOS 侧另有 4.6.4）。
4.5.3 起的峰值按序出现在不同声道，与素材逐声道轮播一致；声道序号与素材声道的逐一对应
关系已于 2026-10-12 闭环（4.7.0↔4.7.0 原生链路 dump 与源 WAV 包络互相关，同序号 NCC 0.67~0.78、
跨声道 ≈0，无重排）。完整测试汇总见 `Docs/audio_multichannel_test_summary.md`。

**4.5.2 的历史结论（2026-10-10 实测）**：公开发送侧自定义音轨输入最高按 2 声道读取——
私参协商正常生效（对端解出对应声道数的 multiopus 流，8ch/6ch/4ch 均验证通过），但只有
前 2 声道携带内容，第 3 声道起静音（接收端逐声道音量与 dump 双重实证）；
`pushExternalAudioFrameRawData` 与 `pushExternalAudioFrameSampleBuffer`（ASBD 自描述 8ch）
结论相同；`setExternalAudioSource(channels: 8)` 与私参 `che.audio.input_channels: 8`
均返回 0 但不解除钳制。该限制自 4.5.3 起已解除（见上表）。

另注意 `pushExternalAudioFrameRawData` 的 `samples` 是**全部声道的总样本数**
（官方 API-Examples CustomAudioSource 同样用法），与 Linux `sendAudioPcmData`
的每声道语义不同；macOS 接收侧的多声道解码（含 3/5/7 奇数声道）不受影响，见 receiver README。
