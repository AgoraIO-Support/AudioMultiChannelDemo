# macOS Multi-Channel Verification Apps (Demo/macOS)

This directory contains two self-contained macOS verification apps (SwiftUI +
AgoraRtcEngine_macOS 4.5.2 via CocoaPods, macOS 12.0+, bilingual English/Chinese UI with
English as default). Requirements baseline: `Spec/native_sdk_audio_multichannel_spec.md`.

| App | Role | Purpose |
| --- | --- | --- |
| [`MultiChannelAudioReceiver/`](MultiChannelAudioReceiver/README.md) | Audience (receive-only) | Subscribe to remote multi-channel/stereo streams: per-channel listen/volume control, remote video display, audio/video receive bitrate charts, audio frame dump |
| [`MultiChannelAudioSender/`](MultiChannelAudioSender/README.md) | Broadcaster (publish-only) | Loop-publish bundled 4/6/8-channel Channel ID WAV files through a custom audio track (multi-channel private-parameter negotiation) |

Each app is an independent project (own Podfile / xcodeproj / README): run `pod install`
in its directory and open its own `.xcworkspace`.

> Verified note: the public macOS 4.5.2 sender-side custom-track input is clamped to
> 2 channels (negotiation works, but ch3+ carry no content) — see the
> [known limitation in the sender README](MultiChannelAudioSender/README.en.md#known-limitation-verified-2026-10-10).
> Receive-side multi-channel decoding (including 3/5/7 odd channels) is unaffected.
