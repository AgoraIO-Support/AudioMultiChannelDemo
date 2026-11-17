# iOS Multi-Channel Verification Apps (Demo/iOS)

This directory contains two self-contained iOS verification apps (SwiftUI + AgoraRtcEngine_iOS
4.7.0 via CocoaPods, iOS 14.0+, bilingual UI with English as default), ported from the two
`Demo/macOS/` apps. Requirements baseline: `Spec/native_sdk_audio_multichannel_spec.md`.

| App | Role | Purpose |
| --- | --- | --- |
| [`MultiChannelAudioReceiverApp/`](MultiChannelAudioReceiverApp/README.md) | Audience (receive-only) | Subscribes to remote multi-channel/stereo streams: per-channel listen/volume control, remote video display, audio/video receive bitrate charts, audio frame dump |
| [`MultiChannelAudioSenderApp/`](MultiChannelAudioSenderApp/README.md) | Broadcaster (publish-only) | Loops the bundled 4/6/8-channel Channel ID WAVs and 3/5/7 odd-channel WAVs over a custom audio track (multi-channel private-parameter negotiation; selecting an odd-channel asset shows an orange "Web mis-decodes" warning banner) |

Both apps are independent projects (each with its own Podfile / project.yml / README). Build:

```bash
cd MultiChannelAudioSenderApp   # or MultiChannelAudioReceiverApp
xcodegen                        # generates the xcodeproj (gitignored)
pod install                     # generates the xcworkspace
open MultiChannelAudioSenderApp.xcworkspace
```

> Platform note: multi-channel behavior on iOS has not been verified on device/simulator yet —
> the macOS-side conclusions (the 4.5.2 send-side custom-track input clamp to 2 channels, lifted
> from 4.5.3; receiver-side adaptive decoding including odd 3/5/7) come from macOS regression
> testing (`Docs/audio_multichannel_test_summary.md`); treat iOS behavior as
> subject to verification. Audio dumps on iOS land inside the app sandbox container (exportable
> via Xcode → Devices and Simulators); exact file naming is to be confirmed by measurement.

[中文文档](README.md)
