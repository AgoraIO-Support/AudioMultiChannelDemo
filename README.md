# Agora 音频多声道（Multiopus）Demo

[English](README.en.md)

音频多声道（multiopus）项目交付仓库，包含：

- Agora Linux SDK 3~8 声道音频收发 C++ demo
- Web 端多声道验证 demo（OPUSMC，payload type 123/124/125，仅 48 kHz）
- macOS 多声道验证 app（Native SDK）：MultiChannelAudioReceiver（观众角色纯接收，逐声道选择收听与音量调节）与 MultiChannelAudioSender（主播角色发布多声道 WAV，中/英文 UI，默认英文）
- 配套文档站与部署资料

![Agora multi-channel audio (multiopus) overview](References/assets/multiopus-repo-overview.svg)

## 声道数支持说明

- Linux SDK（Native 同）：支持 3/4/5/6/7/8 声道收发；3/5/7 需额外设置私参 `che.audio.custom_channel_num`（本仓库 demo 已按声道数自动处理）。
- Web 验证端：仅支持 4/6/8 偶数声道解码。3/5/7 奇数声道流在 Web 端**不报错但会被误解码**：按 N+1 偶数声道配置解码（mono 子流当 stereo 解，末声道内容复制出幻影声道），音量条全有量且误判「多声道 ✓」，结果不可信（2026-10-12 实测，见 `Docs/audio_multichannel_test_summary.md` §5）；`selectChannels` 等声道选择能力对奇数声道流同样不可用。2026-10-12 起 `Demo/web` 内置 `oddChannelFix.ts` 运行时补丁（三个 Live Demo 均有开关、默认开），可检测「末两声道恒等」幻影签名并修正 demo 层的判定显示与播放输出；SDK 层奇数声道仍不支持，见汇总 §4.2。
- 实际互通验证：Web↔Native、Web↔Linux 覆盖 4/6/8 声道；3/5/7 奇数声道已于 2026-10-12 在 Native↔Linux 全链路实测通过（Linux↔Linux、Linux→macOS、macOS↔macOS、macOS→Linux，逐声道内容正确且同序号，见 `Docs/audio_multichannel_test_summary.md`）。
- 若 3/5/7 声道内容需要触达 Web 端，建议发送端补一路静音声道凑成偶数声道（3→4、5→6、7→8）后发送，Web 端忽略该静音声道即可。
- 逐声道收听验证端：Web 用 `Demo/web/`（仅偶数声道）；Native/macOS 用 `Demo/macOS/` 接收 app（基于私参 `che.audio.neteq.adjust_decoded_volume` 逐声道开关与音量，Native 自适应解码，含 3/5/7）。

## 目录导航

| 目录 | 内容 |
| --- | --- |
| `Spec/` | 需求规格（PRD）：Linux SDK、Web SDK 与 Native SDK（macOS）音频多声道 |
| `Demo/linux/` | Linux C++ demo：发送 4 路径（raw PCM / WAV 16bit/24bit / 预编码 Opus / MKV 音视频同发）+ 接收 4 路径（解码 PCM / 编码帧 / 混音 / 音视频同收），x86_64 + aarch64 |
| `Demo/web/` | Web 验证 demo（Vue3 + Vite，agora-rtc-sdk-ng 4.20.1-200）：多声道发布/订阅、声道选择播放、逐声道音量 |
| `Demo/macOS/` | macOS 验证 app（SwiftUI + AgoraRtcEngine_macOS，CocoaPods）：`MultiChannelAudioReceiver` 观众角色纯接收多声道/立体声流（逐声道控制、远端视频显示、码率走势图、音频帧 dump）；`MultiChannelAudioSender` 主播角色发布内置 4/6/8 声道 Channel ID WAV（10ms pacer + 多声道私参协商；发送侧 2 声道钳制仅存在于 4.5.2，4.5.3 起已修复） |
| `Demo/iOS/` | iOS 验证 app（SwiftUI + AgoraRtcEngine_iOS 4.7.0，CocoaPods，iOS 14.0+）：`MultiChannelAudioReceiverApp` 与 `MultiChannelAudioSenderApp`，由 macOS 双 app 移植（功能逐项对齐；iOS 平台多声道行为以实测为准） |
| `Demo/docs/` | 多声道接入文档站（随 Web 构建发布到 `dist/docs/`） |
| `Docs/` | 部署与运维文档（AWS 测试服务器部署、前端缓存管理） |
| `LinuxSDKFolder/` | Agora Linux SDK v4.4.30.149（X86_64 / Aarch64，含符号包） |
| `References/` | 参考材料（历史 sample、设计文档、沟通记录的文字化备份，不入 Docker 构建上下文） |

## 快速开始

- Linux demo 构建与运行：见 `Demo/linux/README.md`（[English](Demo/linux/README.en.md)）
- Web demo 启动与双标签页验证：见 `Demo/web/README.md`（[English](Demo/web/README.en.md)）
- macOS 验证 app 构建与逐声道试听/发送：见 `Demo/macOS/README.md`（[English](Demo/macOS/README.en.md)）与 `Demo/macOS/MultiChannelAudioSender/README.md`（[English](Demo/macOS/MultiChannelAudioSender/README.en.md)）
- iOS 验证 app（Sender/Receiver）构建：见 `Demo/iOS/README.md`（[English](Demo/iOS/README.en.md)）
- 端到端验证（Linux 发送 → Web 订阅逐声道核对）：见上述两份 README 与 `Demo/docs/` 文档站
