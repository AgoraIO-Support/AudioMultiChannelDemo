# macOS 多声道验证 App（Demo/macOS）

本目录包含两个自包含的 macOS 验证 app（SwiftUI + AgoraRtcEngine_macOS 4.5.2，CocoaPods 集成，
macOS 12.0+，中/英文 UI 默认英文），需求基线见 `Spec/native_sdk_audio_multichannel_spec.md`：

| App | 角色 | 用途 |
| --- | --- | --- |
| [`MultiChannelAudioReceiver/`](MultiChannelAudioReceiver/README.md) | 观众（纯接收） | 订阅远端多声道/立体声流：逐声道选择收听与音量调节、远端视频显示、音/视频接收码率走势图、音频帧 dump |
| [`MultiChannelAudioSender/`](MultiChannelAudioSender/README.md) | 主播（纯发布） | 将内置 4/6/8 声道 Channel ID WAV 经自定义音轨循环发布（多声道私参协商） |

两个 app 均为独立工程（各自含 Podfile / xcodeproj / README），分别 `pod install` 后打开
各自的 `.xcworkspace` 构建。

> 实测提示：公开版 macOS 4.5.2 的发送侧自定义音轨输入被钳制为 2 声道（协商正常但
> ch3+ 静音），详见 [sender README 的已知限制](MultiChannelAudioSender/README.md#已知限制2026-10-10-实测结论)；
> 接收侧多声道解码（含 3/5/7 奇数声道）不受影响。

[English Documentation](README.en.md)
