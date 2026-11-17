# iOS 音频多声道接收 Demo（MultiChannelAudioReceiverApp）

基于 Agora Native SDK iOS 版（`AgoraRtcEngine_iOS` 4.7.0，CocoaPods 集成，XcodeGen 生成工程）的
**纯接收端** 验证 app：以观众（audience）角色加入频道，接收远端主播的多声道
（OPUSMC，payload type 123/124/125，48 kHz，3~8 声道）或普通立体声/单声道音频流，
并通过 Native SDK 私参 **逐声道选择收听、逐声道调节音量**。需求背景与验收基线见
`Docs/audio_multichannel_spec.md`。

本 app 自 macOS 版（`Demo/macOS/MultiChannelAudioReceiver`）移植，功能逐项对齐；
界面按 iPhone 竖屏调整为**上下分区**（macOS 版为「控制 | 监控」左右双栏，详见「与 macOS 版的差异」）。

> 配套发送端：`Demo/macOS/MultiChannelAudioSender/`（macOS 主播角色发布内置多声道 WAV 素材），
> 或 `Demo/linux` 的 `sample_send_wav_multiopus`。

[English Documentation](README.en.md)

## 界面语言

界面支持 **English / 中文** 双语，默认英文。通过主界面顶部的 Language 选择器即时切换
（界面控件文案立即重新渲染；状态栏等按事件发生时的语言生成的文案，待下一次状态更新后刷新），
选择通过 `@AppStorage("uiLanguage")` 持久化，下次启动保持。
语言实现在 `MultiChannelAudioReceiverApp/L10n.swift`（查表 + String(format:) 插值），
日志与状态文案按切换时的语言输出（已输出的历史日志行不回溯切换）。

## 功能说明

- **纯接收**：`.liveBroadcasting` 频道模式 + 观众角色加入，不发布麦克风/摄像头，
  自动订阅远端音频与视频。
- **布局**：顶部语言栏与「加入频道」区固定；中部可滚动，上为「远端多声道控制」、下为
  「Monitor — UID xxx」监控区；底部日志区固定 160pt。
- **远端视频显示（按需出现）**：监控区在所选 host 发布视频时渲染其画面
  （`UIViewRepresentable` 包 `UIView` + `setupRemoteVideo`，renderMode=fit，
  宽高比按 `remoteVideoStats` 流实际值、拿不到按 16:9），纯音频 host 只显示码率走势与"仅音频"提示。
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
- **音频帧 Dump**：`che.audio.frame_dump` 开关。**iOS 实测（2026-10-13，iPhone 16 Pro 模拟器
  iOS 18.3，SDK 4.7.0）落盘位置为 App 沙盒容器的 `Library/Caches/`**：本次冒烟测试
  （频道内无远端 host）产生了 `audio_dump_play_0.wav`（播放输出）与
  `audio_dump_playback_mixer_0.wav`（混音输出）；按远端用户解码输出的
  `audio_dump_playback_before_mixed_0_<时间戳>_<uid>.wav` 命名规则**待真实多声道频道实测确认**
  （macOS 4.5.2/4.7.0 的完整命名规则见 macOS 版 README，iOS 不照搬其结论）。
  dump 不落在 Documents/（UIFileSharingEnabled 对它无效），导出方式：
  - 模拟器：`xcrun simctl get_app_container booted com.agora.demo.MultiChannelAudioReceiverApp data`
    直接得到容器路径；
  - 真机：Xcode → Window → Devices and Simulators → 选中设备 → Installed Apps →
    选中本 app → 齿轮菜单 → Download Container，导出后查看 `AppData/Library/Caches/`。
- **日志区**：界面底部滚动显示加入/离开、远端进出、连接状态、SDK 错误等日志（1000 行上限，
  同时无缓冲镜像到 stdout）。

## 环境要求

- iOS 14.0+（app target 部署目标 14.0，原因见下），Xcode 14+（开发使用 Xcode 16.2 验证）
- CocoaPods（`pod` 命令可用）、XcodeGen（`xcodegen` 命令可用）

> **部署目标说明**：本工程规划时下限为 iOS 13.0（Podfile 的 `platform :ios, '13.0'` 保留，
> 作为 Pods 的编译下限），但 app 本体使用的 SwiftUI 生命周期（`@main App`）、`@AppStorage`、
> `GroupBox`、`onChange(of:)` 均要求 iOS 14+，故 app target 的部署目标定为 **14.0**。

## 构建步骤

工程文件（`MultiChannelAudioReceiverApp.xcodeproj`）由 XcodeGen 从 `project.yml` 生成，
不进版本库；CocoaPods 集成要求 xcodeproj 已存在，因此顺序固定为：

```bash
cd Demo/iOS/MultiChannelAudioReceiverApp
xcodegen           # 由 project.yml 生成 MultiChannelAudioReceiverApp.xcodeproj
pod install        # 集成 AgoraRtcEngine_iOS 4.7.0，生成 MultiChannelAudioReceiverApp.xcworkspace
open MultiChannelAudioReceiverApp.xcworkspace   # 用 Xcode 打开 workspace（不是 xcodeproj）
```

命令行构建（模拟器，免证书）：

```bash
xcodebuild -workspace MultiChannelAudioReceiverApp.xcworkspace \
    -scheme MultiChannelAudioReceiverApp -configuration Debug \
    -destination 'generic/platform=iOS Simulator' \
    CODE_SIGNING_ALLOWED=NO build
```

## 签名说明（重要）

- 工程默认 `CODE_SIGNING_ALLOWED=NO`（面向模拟器/CI 构建，无需开发者证书）。
- **Agora 预置动态框架的模拟器切片出厂未签名**，而 Apple Silicon 的 arm64 模拟器要求加载的
  dylib 必须（至少 ad-hoc）签名；这些框架在链接时是 weak 引用，加载失败不崩溃，运行时表现为
  `NSClassFromString("AgoraRtcEngineKit") == nil`、`sharedEngine` 返回 nil（引擎创建失败）。
  为此 `Podfile` 的 `post_install` 钩子会对 `Pods/` 内全部 vendored `.framework` 统一执行
  ad-hoc 签名（`codesign --sign -`），嵌入 app 后签名保持有效；用正式签名构建真机包时，
  Embed 阶段会用开发者证书重签，不受该钩子影响。
- **真机运行**：请在 Xcode 中把 target 的 Signing 改为有效开发团队（并去掉
  `CODE_SIGNING_ALLOWED=NO` 覆盖），否则无法安装到设备。

## 使用流程

1. **启动多声道发送端**（例如 Demo/linux 的 8 声道 WAV 发送）：

   ```bash
   cd Demo/linux/out
   ./sample_send_wav_multiopus --channelId <频道名> --token <token> \
       --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
       --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
   ```

2. **本 app 加入同一频道**：App ID 默认留空，请填写你自己的 Agora App ID（输入后通过
   `@AppStorage` 持久化，下次启动保持）；填写 Token（无证书项目留空）、频道 ID、UID（0 = 自动分配），
   点击「Join」。状态栏显示连接状态与本端 UID。
3. **选择远端用户**：「Remote Multi-Channel Control」区的下拉框中选择发送端 uid（通常会自动选中第一个）。
   选中后会立即把当前 8 行声道状态全部下发到该 uid。
4. **逐声道试听**：用开关静音/启用某个声道，或拖动滑杆调节该声道音量百分比。
   发送 Channel ID 素材（各声道内容可区分）时，可逐声道核对是否串声道。
5. 验证普通立体声流时把「Channels」选为 2，只出现声道 1/2 两行控制（纯显示过滤，
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

开启后 SDK 将解码后的音频帧落盘，用于离线核对声道内容。iOS 落盘位置见上方「功能说明」
（**沙盒 `Library/Caches/`，模拟器实测；逐远端用户文件命名待实测**）。

## 命令行自动化（回归测试用）

启动参数（映射进 `UserDefaults` 的 argument domain）与 macOS 版完全一致：
`-autoChannel` / `-autoJoin` / `-autoDump` / `-autoQuitAfter`。App 日志同时无缓冲镜像到 stdout。

iOS 模拟器上的传参与日志采集方式：

```bash
xcrun simctl install booted <路径>/MultiChannelAudioReceiverApp.app
xcrun simctl launch --console booted com.agora.demo.MultiChannelAudioReceiverApp \
    -autoChannel <频道> -autoJoin YES -autoDump YES -autoQuitAfter 120
# --console 把 app 的 stdout/stderr 接到当前终端（对应 macOS 直接运行的 stdout 镜像）
```

- `autoJoin`：启动后自动以观众角色加入（uid 0 自动分配）；`autoDump`：join 时开启
  `che.audio.frame_dump`；`autoQuitAfter`：秒数，>0 时到点自动退出——**iOS 无公开的
  "退出 App" API，实现为 `exit(0)`**（macOS 版为 `NSApplication.terminate`）。
- 真机无法从命令行直接传启动参数（可在 Xcode scheme 的 Arguments 中配置，或靠 UI 操作）。
- 2026-10-13 已在 iPhone 16 Pro 模拟器（iOS 18.3）完成冒烟验证：自动加入频道成功
  （观众角色、真实 Agora 网络）、dump 开关下发返回 0、到时自动退出。

## 注意事项

- **接收端无需任何多声道私参**：Native SDK 在 123/124/125 协商机制下自适应解码 OPUSMC
  多声道流（包括 3/5/7 奇数声道），接收普通 stereo/mono 流也无需特殊处理。payload type /
  `custom_channel_num` 等参数只需在**发送端**设置（见 `Demo/linux/README.md`）。
  **注意：3/5/7 奇数声道结论来自 macOS / Linux 端实测（见
  `Docs/audio_multichannel_test_summary.md` §4.1）；iOS 的 4/6/8ch 已有 Web→iOS 实测，
  但间歇性 1ch 未闭环，3/5/7 仍未单独验证（见汇总 §3、§5.1）。** 已在 iOS 模拟器验证的部分：引擎创建、观众加入、自动订阅、
  frame_dump 落盘（2026-10-13）。
- **声道数选择器只是 UI 行数控制**：切换它不下发任何 SDK 参数；SDK 对超出流实际声道数的
  `index` 设置不产生效果；切换远端 uid 时会把全部 8 行（含隐藏行）状态重新下发，保证行为确定。
- **音量私参作用在解码后、播放前**：只影响本端听到的声音，不影响频道内其他用户。
- iOS 上 `AVAudioSession` 由 Agora SDK 自管理，app 不做音频会话配置；观众角色不发布音频轨，
  不申请麦克风/摄像头权限（Info.plist 无对应 UsageDescription）。已声明
  `UIBackgroundModes = audio` 以支持后台继续播放频道音频。
- iOS 4.7.0 SDK 头文件（`AgoraRtcEngineDelegate.h`）经核对同样只有 `didOccurError`、
  无 `didOccurWarning`（与 macOS 一致）；界面日志只上报 `didOccurError` 及连接状态变化。

## 与 macOS 版的差异

- **布局**：「控制 | 监控」左右双栏 → 上下分区（中部可滚动），适配 iPhone 竖屏宽度；
  Join 区的状态/本端 UID 文本独立成行（避免窄屏挤压）。
- **日志区不支持长按框选复制**：`.textSelection` 需 iOS 15+，本工程部署目标 14.0；
  日志可通过 stdout 镜像（`simctl launch --console` 或 Xcode 控制台）获取。
- 声道音量百分比标签用等宽字体（`.font(.system(.body, design: .monospaced))`）替代
  `.monospacedDigit()`（后者需 iOS 15+）。
- `autoQuitAfter` 实现为 `exit(0)`（macOS 为 `NSApplication.terminate`）。
- App 入口去掉了 macOS 的窗口最小尺寸约束（`.frame(minWidth:minHeight:)`）。
- dump 落盘位置不同：iOS 为沙盒 `Library/Caches/`（模拟器实测），macOS 为 `~/Library/Logs/`。
