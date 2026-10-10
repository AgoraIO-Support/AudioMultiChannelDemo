# macOS 音频多声道接收 Demo（MultiChannelAudioReceiver）

基于 Agora Native SDK macOS 版（`AgoraRtcEngine_macOS` 4.5.2，CocoaPods 集成）的
**纯接收端** 验证 app：以观众（audience）角色加入频道，接收远端主播的多声道
（OPUSMC，payload type 123/124/125，48 kHz，3~8 声道）或普通立体声/单声道音频流，
并通过 Native SDK 私参 **逐声道选择收听、逐声道调节音量**。需求背景与验收基线见
`Spec/native_sdk_audio_multichannel_spec.md`。

> 配套发送端验证 app 见 `MultiChannelAudioSender/`（主播角色发布内置多声道 WAV 素材）。

[English Documentation](README.en.md)

## 界面语言

界面支持 **English / 中文** 双语，默认英文。通过主界面顶部的 Language 选择器即时切换
（所有可见文案立即重新渲染），选择通过 `@AppStorage("uiLanguage")` 持久化，下次启动保持。
语言实现在 `MultiChannelAudioReceiver/L10n.swift`（查表 + String(format:) 插值），
日志与状态文案按切换时的语言输出（已输出的历史日志行不回溯切换）。

## 功能说明

- **纯接收**：`.liveBroadcasting` 频道模式 + 观众角色加入，不发布麦克风/摄像头，
  自动订阅远端音频与视频。
- **远端视频显示（按需出现）**：界面为「控制 | 监控」双栏——左栏选择远端用户并做逐声道控制，
  右栏「Monitor — UID xxx」监控所选用户：host 发布视频时画面等比撑满栏宽实时渲染
  （`setupRemoteVideo`，宽高比按流实际值），纯音频 host 只显示码率走势与"仅音频"提示。
- **接收码率走势图**：所选远端用户的音频与视频接收码率各有一条带 Y 轴刻度（最大值/0）的
  实时 sparkline（音频绿色、视频蓝色，各取最近 60 个 `remoteAudioStats`/`remoteVideoStats`
  样本，约 2 分钟窗口）；视频图仅在 host 发布视频时出现。
- **远端用户列表**：由远端音频**或视频**发布状态驱动（音频/视频 `remote*StateChangedOfUid` 回调中
  处于 starting/decoding 时收录，`didOfflineOfUid` 时移除），**只有真正在发布音视频的主播（host）
  才会出现，观众（audience）角色的端永远不会列出**。覆盖三种 host：音频+视频、仅音频、仅视频——
  仅视频 host 选中后控制栏显示"仅视频"提示且不下发音量参数；仅音频 host 监控栏显示"仅音频"提示。
- **声道数选择器（1~8，默认 8）**：**纯显示控制**，只决定下方渲染几行声道控制，
  不改变任何音频行为。存在原因：Native SDK 接收端没有回调告知远端流的实际声道数，
  需按发送端素材手动对齐界面——验证普通立体声流选 2（只显示声道 1/2），验证 8 声道流
  保持 8，验证 3/5/7 选对应数字。两个注意点：未选择远端用户时切换它没有可见效果
  （此时声道行不渲染）；被隐藏声道的音量状态保持原值继续生效（默认全部启用 100%），
  即选 2 并不会把 8 声道流的 3~8 声道静音——要静音请用对应行的开关。
- **逐声道控制**：每个声道一行，包含启用开关（Toggle）与音量滑杆（0~100%），改动立即通过私参
  `che.audio.neteq.adjust_decoded_volume` 生效。默认全部声道启用、音量 100。
- **音频帧 Dump**：`che.audio.frame_dump` 开关。macOS 4.5.2 实测 dump 落在 **`~/Library/Logs/`**：
  按远端用户解码输出为 `audio_dump_playback_before_mixed_0_<yyyy_MM_dd_HHmmss>_<uid>.wav`
  （48 kHz / N 声道 / 16bit，约 120 MB 封顶后停止写入，Leave 重进会按新时间戳另建）；
  混音输出 `audio_dump_playback_mixer_0.wav`（恒 2 声道）；播放输出 `audio_dump_play_0.wav`；
  远端混合输入 `far_in_48000hz_2ch.wav`（2 声道）。
- **日志区**：界面底部滚动显示加入/离开、远端进出、连接状态、SDK 错误等日志。

## 环境要求

- macOS 12.0+（部署目标 12.0），Xcode 14+（开发使用 Xcode 16.2 验证）
- CocoaPods（`pod` 命令可用）

## 构建步骤

```bash
cd Demo/macOS
pod install        # 集成 AgoraRtcEngine_macOS 4.5.2，生成 MultiChannelAudioReceiver.xcworkspace
open MultiChannelAudioReceiver.xcworkspace   # 用 Xcode 打开 workspace（不是 xcodeproj）
```

命令行构建：

```bash
xcodebuild -workspace MultiChannelAudioReceiver.xcworkspace \
    -scheme MultiChannelAudioReceiver -configuration Debug \
    CODE_SIGNING_ALLOWED=NO build
```

工程使用 ad-hoc 签名（`CODE_SIGN_IDENTITY = "-"`），无 entitlements、无沙盒，
本地构建无需开发者证书。

## 使用流程

1. **启动多声道发送端**（例如 Demo/linux 的 8 声道 WAV 发送）：

   ```bash
   cd Demo/linux/out
   ./sample_send_wav_multiopus --channelId <频道名> --token <token> \
       --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
       --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
   ```

2. **本 app 加入同一频道**：App ID 默认预填 `43f818097c124394821bd44598b7ca9d`（手动修改后通过
   `@AppStorage` 持久化，下次启动保持）；填写 Token（无证书项目留空）、频道 ID、UID（0 = 自动分配），
   点击「加入频道」。状态栏显示「已加入频道」与本端 UID。
3. **选择远端用户**：「远端多声道控制」区的下拉框中选择发送端 uid（通常会自动选中第一个）。
   选中后会立即把当前 8 行声道状态全部下发到该 uid。
4. **逐声道试听**：用开关静音/启用某个声道，或拖动滑杆调节该声道音量百分比。
   发送 Channel ID 素材（各声道内容可区分）时，可逐声道核对是否串声道。
5. 验证普通立体声流时把「声道数」选为 2，只出现声道 1/2 两行控制（纯显示过滤，
   不影响听到的声音；若 3~8 声道仍有声音，是隐藏行保持默认启用 100% 的缘故，
   需要静音时请显式关闭对应声道开关）。

## 私参说明

### 逐声道解码音量 `che.audio.neteq.adjust_decoded_volume`

```json
{"che.audio.neteq.adjust_decoded_volume":{"remoteUid": 109016691, "index": 1, "volume": 100}}
```

- `remoteUid`：目标远端用户的 uid；
- `index`：**1-based** 声道序号（1~8）；
- `volume`：0~100，百分比。`100` = 原始音量（可听），`0` = 静音。
- 「选择收听某些声道」= 把要听的声道设 100、其余设 0。

### 音频帧 Dump `che.audio.frame_dump`

```json
{"che.audio.frame_dump":true}
```

开启后 SDK 将解码后的音频帧落盘，用于离线核对声道内容。macOS 4.5.2 实测落盘位置为
`~/Library/Logs/`（文件名规则见上方「功能说明」）。

## 注意事项

- **接收端无需任何多声道私参**：Native SDK 在 123/124/125 协商机制下自适应解码 OPUSMC
  多声道流（包括 3/5/7 奇数声道），接收普通 stereo/mono 流也无需特殊处理。payload type /
  `custom_channel_num` 等参数只需在**发送端**设置（见 `Demo/linux/README.md`）。
- **奇数声道（3/5/7）**：Native 端可自适应解码；Web 端（`Demo/web`）仅支持偶数声道解码，
  3/5/7 声道流不适用。
- **声道数选择器只是 UI 行数控制**：切换它不下发任何 SDK 参数；SDK 对超出流实际声道数的
  `index` 设置不产生效果；切换远端 uid 时会把全部 8 行（含隐藏行）状态重新下发，保证行为确定。
- **音量私参作用在解码后、播放前**：只影响本端听到的声音，不影响频道内其他用户。
- macOS 4.5.2 SDK 无 `didOccurWarning` 回调，界面日志只上报 `didOccurError` 及连接状态变化。
- 本 app 无沙盒、无麦克风采集（观众角色不发布音频轨），因此不申请麦克风权限。
