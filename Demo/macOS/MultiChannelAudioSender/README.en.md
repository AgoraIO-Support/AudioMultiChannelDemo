# macOS Multi-Channel Audio Sender Demo (MultiChannelAudioSender)

A **sender-side** verification app built on the Agora Native SDK for macOS
(`AgoraRtcEngine_macOS` 4.5.2, CocoaPods): joins a channel as a **broadcaster** and
publishes multi-channel WAV material (48 kHz / 16-bit / N channels) in a loop through a
custom audio track. Pairs with the `MultiChannelAudioReceiver` app
(`Demo/macOS/MultiChannelAudioReceiver/`).

[中文文档](README.md)

## UI Language

English / 中文, defaults to English; switch via the Language picker at the top
(persisted with `@AppStorage("uiLanguage")`).

## Features

- **Host publishing**: `.liveBroadcasting` + broadcaster role, `disableVideo()`, no
  microphone/camera publish; the WAV is published via `publishCustomAudioTrack`.
- **Bundled assets** (in `Resources/`, copied from `Demo/linux/public/`; per-channel
  distinguishable Channel ID content):
  - `8_Channel_ID_signed_16_bit_pcm.wav` (8ch, payload 125)
  - `6_Channel_ID_48k.wav` (6ch, payload 124)
  - `4_Channel_ID_48k.wav` (4ch, payload 123)
- **WAV parsing**: minimal RIFF parser (`WavReader.swift`) handling both PCM (format=1)
  and WAVE_FORMAT_EXTENSIBLE (0xFFFE, PCM decided by the SubFormat GUID); only
  48 kHz / 16-bit accepted, anything else is rejected at load time.
- **Multi-channel private parameters** set before join: `che.audio.custom_payload_type`
  (≤4ch→123, ≤6ch→124, otherwise→125), `che.audio.custom_channel_num`,
  `che.audio.custom_sample_rate`.
- **Pacing**: 10 ms-per-frame pacer loops the file via `pushExternalAudioFrameRawData`;
  the UI shows sent frame count / audio duration.
- **Audio frame dump**: `che.audio.frame_dump` toggle (applied on join).

## Prerequisites & Build

- macOS 12.0+, Xcode 14+, CocoaPods, [XcodeGen](https://github.com/yonsm/XcodeGen)
  (`brew install xcodegen`)

```bash
cd Demo/macOS/MultiChannelAudioSender
xcodegen generate   # generates MultiChannelAudioSender.xcodeproj from project.yml
pod install         # integrates AgoraRtcEngine_macOS 4.5.2, produces the xcworkspace
open MultiChannelAudioSender.xcworkspace
```

CLI build:

```bash
xcodebuild -workspace MultiChannelAudioSender.xcworkspace \
    -scheme MultiChannelAudioSender -configuration Debug build
```

The project uses ad-hoc signing (`CODE_SIGNING_ALLOWED=NO`), no sandbox, no entitlements.

## Usage

1. Pick a WAV asset (disabled while joined; Leave first to switch);
2. Fill in App ID (pre-filled) and channel name, UID 0 = auto-assign, click Join;
3. Click Publish to start looping; Stop to halt;
4. Verify on the receiving side with `MultiChannelAudioReceiver` or `Demo/web/`:
   check the codec (multiopus), channel count, and per-channel content.

## Known limitation (verified 2026-10-10)

**The public AgoraRtcEngine_macOS 4.5.2 (CocoaPods) reads at most 2 channels from a
custom-track push input:**

- Negotiation works — the remote side decodes a multiopus stream with the configured
  channel count (verified for 8ch/6ch/4ch);
- but only the first 2 channels carry content; channels 3+ are silent (proven by both
  per-channel volume levels on the web demo and per-user PCM dumps on the receiver);
- identical results with `pushExternalAudioFrameRawData` and
  `pushExternalAudioFrameSampleBuffer` (whose ASBD self-describes 8 channels);
- i.e. multi-channel **sending** on the public macOS SDK needs SDK-side enablement
  (as done for the custom Linux builds). This app is the ready-made regression tool
  for that capability once it lands.

Also note: `pushExternalAudioFrameRawData`'s `samples` is the **total sample count
across all channels** (same usage as the official API-Examples CustomAudioSource),
unlike Linux `sendAudioPcmData`'s per-channel semantics. Multi-channel **decoding** on
macOS (including 3/5/7 odd channels) is unaffected — see the receiver README.
