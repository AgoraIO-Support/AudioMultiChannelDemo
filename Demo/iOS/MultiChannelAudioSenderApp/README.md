# iOS 音频多声道发送 Demo（MultiChannelAudioSenderApp）

基于 Agora Native SDK iOS 版（`AgoraRtcEngine_iOS` 4.7.0，CocoaPods 集成；Podfile 可切换版本）的
**发送端** 验证 app：以主播（broadcaster）角色加入频道，将多声道 WAV 素材
（48 kHz / 16bit / N 声道）经自定义音轨循环发布到频道。本工程由 macOS 版
`Demo/macOS/MultiChannelAudioSender/` 完整移植，功能与行为逐项对齐；可与接收端
`MultiChannelAudioReceiver`（macOS）、Linux 接收端或 `Demo/web/` 配套验证。

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
- **发布节奏**：10 ms/帧 pacer 循环推送（`pushExternalAudioFrameRawData`，
  `samples` = 全部声道总样本数），界面显示已发送帧数/时长。
- **音频帧 Dump**：`che.audio.frame_dump` 开关（加入频道时应用）。
- **奇数声道警示**：选中 3/5/7 声道素材（下标 3/4/5，条目带 ⚠）时发送面板显示橙色警示条——
  奇数声道流仅 Native/Linux 端可验证，Web SDK 会误解码（按 N+1 声道、末声道复制幻影、界面误判
  多声道 ✓），结果不可信。边界说明：2026-10-12 起 `Demo/web` 内置 `oddChannelFix.ts` 运行时补丁，
  可修正 demo 层的判定与播放（开关默认开），但 **SDK 层面奇数声道仍不支持**，
  Native 侧「以 Native/Linux 验证奇数声道」的结论不变（见 `Test/odd_channels_interop_test_20261012.md` §6）。
- **后台发布**：`Info.plist` 配置 `UIBackgroundModes = audio`，app 退到后台时 RTC 音频发布保持
  （`AVAudioSession` 由 Agora SDK 自管理，app 侧无需额外音频会话代码）。
- **不采集本地设备**：本 app 仅推送 WAV 文件，不使用麦克风/摄像头，
  因此 `Info.plist` 不含 `NSMicrophoneUsageDescription` / `NSCameraUsageDescription`。

## 环境要求与构建

- iOS 14.0+（SwiftUI `@main App` 生命周期、`@AppStorage`、`onChange`、`ScrollViewReader` 均要求
  iOS 14 起；macOS 版最低 macOS 12.0 同为 SwiftUI App 起点），Xcode 16，CocoaPods，
  [XcodeGen](https://github.com/yonsm/XcodeGen)（`brew install xcodegen`）

```bash
cd Demo/iOS/MultiChannelAudioSenderApp
xcodegen generate   # 由 project.yml 生成 MultiChannelAudioSenderApp.xcodeproj
pod install         # 集成 AgoraRtcEngine_iOS（版本见 Podfile），生成 xcworkspace
open MultiChannelAudioSenderApp.xcworkspace
```

命令行构建（模拟器，免签名）：

```bash
xcodebuild -workspace MultiChannelAudioSenderApp.xcworkspace \
    -scheme MultiChannelAudioSenderApp -configuration Debug \
    -destination 'generic/platform=iOS Simulator' \
    CODE_SIGNING_ALLOWED=NO build
```

工程配置免签名（`CODE_SIGNING_ALLOWED=NO`），用于模拟器构建与运行；**真机运行需自行配置
开发证书与签名**（Xcode 打开 xcworkspace 后在 Signing & Capabilities 选择 Team 即可）。

> 关于模拟器免签名构建的一个 iOS 特有细节：Apple Silicon 上的 iOS 模拟器运行时要求
> 所有被加载的代码都带签名（至少 ad-hoc），而 `CODE_SIGNING_ALLOWED=NO` 会让 CocoaPods
> 跳过对内嵌 frameworks 的签名——未签名的 AgoraRtcKit.framework 会被 dyld 拒绝加载，
> 由于该框架是 weak-linked，症状为 `AgoraRtcEngineKit` 类绑定为 0、`sharedEngine` 静默返回
> nil（SDK 无任何日志）。本工程在 Podfile 的 `post_integrate` 钩子中追加了一个构建阶段
> （位于 `[CP] Embed Pods Frameworks` 之后），在免签名模拟器构建时自动对内嵌 frameworks
> 补 ad-hoc 签名，因此上述构建命令的产物可直接安装到模拟器运行。

## 使用流程

1. 选择 WAV 素材（加入后不可切换，切换需先 Leave）；
2. 填 App ID（默认预填）/频道名，UID 0 自动分配，点 Join；
3. 点 Publish 开始循环发布；Stop 停止；
4. 接收端验证：用 `MultiChannelAudioReceiver`（macOS）、Linux 接收端或 `Demo/web/` 订阅该频道，
   核对 codec（multiopus）、声道数与逐声道内容。

## 命令行自动化（回归测试用）

通过启动参数（映射进 `UserDefaults`）免去 UI 操作，App 日志同时无缓冲镜像到 stdout。
模拟器上用 `xcrun simctl launch --console` 直接采集日志：

```bash
# 先安装到已启动的模拟器
xcrun simctl install booted <DerivedData>/MultiChannelAudioSenderApp.app
# 带自动化参数启动（--console 把 app stdout 接到当前终端）
xcrun simctl launch --console booted com.agora.demo.MultiChannelAudioSenderApp \
    -autoChannel <频道> -autoUid <uid> -autoWav 0 \
    -autoJoin YES -autoPublish YES -autoQuitAfter 300
```

- `autoWav`：素材下标（0=8ch, 1=6ch, 2=4ch, 3=3ch, 4=5ch, 5=7ch）；
  `autoWavPath`：自定义 WAV 文件路径（优先于 autoWav，须 48 kHz / 16bit / 1~8 声道）。
  **沙盒限制**：真机上仅可读取 app 容器内路径；模拟器上主机绝对路径通常可直接读取。
- `autoJoin`/`autoPublish`：启动后自动加入并在 join 成功后自动发布；
  `autoQuitAfter`：秒数，>0 时到点自动退出——先停止推流并离开频道，0.5 s 后
  `exit(EXIT_SUCCESS)`（避免进程清理阶段 engine 先销毁导致 pacer 打出一串
  -7/NOT_INITIALIZED 噪音日志）。
- `autoUid` 仅接受数字；非法值（非数字）静默回退为 0（自动分配 uid）。
- 配合 Web 端 `?channel=<频道>&uid=<webuid>&subTo=<autoUid>` 打开订阅页即可做版本回归
  （采样 `window.__mcRemoteTrack.getVolumeLevels()` 逐声道峰值判定，方法学同 macOS 版 README）。

## 音频帧 Dump 路径（iOS）

`che.audio.frame_dump` 开启后，dump 文件落在 **app 沙盒容器内**（与 macOS 的 `~/Library/Logs/`
不同）。可用 Xcode → Window → Devices and Simulators → 选中设备/模拟器 → 下载或浏览 app 容器
（模拟器容器位于 `~/Library/Developer/CoreSimulator/Devices/<UDID>/data/Containers/Data/Application/<UUID>/`）。
**具体文件名规则在 iOS 平台尚未实测**，以实测为准；命名可参考 macOS 实测口径（`audio_dump_*` 系列），
但两者不保证一致。

## 发送侧多声道能力：版本注意事项（iOS 尚未实测）

**macOS 实测结论（2026-10-12 回归，详见 `Demo/macOS/MultiChannelAudioSender/README.md` 完整矩阵）**：
公开发送侧 4.5.2 存在自定义音轨输入 2 声道钳制（协商正常、仅前 2 声道有内容、CH2+ 静音），
**自 4.5.3 起修复**（4.5.3 / 4.6.0 / 4.6.2 / 4.7.0 实测 8 声道全通，4.7.0 另验证 4ch/6ch 素材）。

> ⚠ **iOS 平台发送侧尚未实测**：上述 4.5.2 钳制与 4.5.3 修复结论全部来自 macOS 实测；iOS 平台
> 同样存在 4.5.2 发送侧 2ch 钳制的**可能性**未排除（Native 各端实现同源，理论上行为接近），
> 但 **iOS 行为以实测为准**。本 app 即作为 iOS 发送侧的回归工具：按上文 CLI 自动化逐版本
> 切换 Podfile（`AgoraRtcEngine_iOS` 版本号）发布 8ch 素材，用 Web 端逐声道峰值采样即可产出
> iOS 版实测矩阵。另注意 iOS 与 macOS 版本号不完全对齐（如 iOS 侧有 4.6.4，macOS 无 4.6.7）。

另注意 `pushExternalAudioFrameRawData` 的 `samples` 是**全部声道的总样本数**
（官方 API-Examples CustomAudioSource 同样用法），与 Linux `sendAudioPcmData`
的每声道语义不同。
