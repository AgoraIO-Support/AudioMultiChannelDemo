# iOS 多声道验证 App（Demo/iOS）

本目录包含两个自包含的 iOS 验证 app（SwiftUI + AgoraRtcEngine_iOS 4.7.0，CocoaPods 集成，
iOS 14.0+，中/英文 UI 默认英文），由 `Demo/macOS/` 两个 app 移植而来，需求基线见
`Spec/native_sdk_audio_multichannel_spec.md`：

| App | 角色 | 用途 |
| --- | --- | --- |
| [`MultiChannelAudioReceiverApp/`](MultiChannelAudioReceiverApp/README.md) | 观众（纯接收） | 订阅远端多声道/立体声流：逐声道选择收听与音量调节、远端视频显示、音/视频接收码率走势图、音频帧 dump |
| [`MultiChannelAudioSenderApp/`](MultiChannelAudioSenderApp/README.md) | 主播（纯发布） | 将内置 4/6/8 声道 Channel ID WAV 与 3/5/7 奇数声道 WAV 经自定义音轨循环发布（多声道私参协商；选中奇数声道素材时 UI 显示「Web 会误解码」警示条） |

两个 app 均为独立工程（各自含 Podfile / project.yml / README），构建方式：

```bash
cd MultiChannelAudioSenderApp   # 或 MultiChannelAudioReceiverApp
xcodegen                        # 生成 xcodeproj（已 gitignore）
pod install                     # 生成 xcworkspace
open MultiChannelAudioSenderApp.xcworkspace
```

> 平台提示：iOS 版本的多声道行为尚未做真机/模拟器实测——macOS 侧的实测结论
> （4.5.2 发送侧自定义音轨输入钳制为 2 声道、4.5.3 起解除；接收侧自适应解码含 3/5/7
> 奇数声道）来自 macOS 回归（`Docs/audio_multichannel_test_summary.md`），
> iOS 行为以实测为准。两个 app 的 iOS 音频 dump 落在 app 沙盒容器内（可用
> Xcode → Devices and Simulators 导出容器查看），文件名规则待实测。

[English Documentation](README.en.md)
