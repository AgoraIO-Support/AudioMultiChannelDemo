# macOS Multi-Channel Audio Sender Demo (MultiChannelAudioSender)

A **sender-side** verification app built on the Agora Native SDK for macOS
(`AgoraRtcEngine_macOS` 4.7.0, CocoaPods; version pinned in the Podfile): joins a channel
as a **broadcaster** and
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
pod install         # integrates AgoraRtcEngine_macOS (version pinned in the Podfile), produces the xcworkspace
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

## Command-line automation (for regression testing)

Launch arguments (mapped into `UserDefaults`) replace UI interaction; the app log is also
mirrored to stdout unbuffered:

```bash
./MultiChannelAudioSender.app/Contents/MacOS/MultiChannelAudioSender \
    -autoChannel <channel> -autoUid <uid> -autoWav 0 \
    -autoJoin YES -autoPublish YES -autoQuitAfter 300
```

- `autoWav`: asset index (0=8ch, 1=6ch, 2=4ch); `autoWavPath`: custom WAV file path (takes
  precedence over autoWav; must be 48 kHz / 16-bit / 1~8 channels); `autoJoin`/`autoPublish`: join on launch
  and start publishing once joined; `autoQuitAfter`: seconds until self-termination (> 0).
- `autoUid` accepts digits only; an invalid (non-numeric) value silently falls back to 0
  (server-assigned uid).
- Pair with the Web client opened as `?channel=<channel>&uid=<webuid>&subTo=<autoUid>` for
  per-version regression (sample `window.__mcRemoteTrack.getVolumeLevels()` per-channel
  peaks; see the matrix below).

## Send-side multi-channel capability: verified version matrix (updated 2026-10-12)

Method: this app publishes the 8-channel Channel ID asset (content rotates across the
eight channels on a 14 s loop) via the command-line automation above; the Web verification
client (`Demo/web/`) subscribes and samples `getVolumeLevels()` every 200 ms for 20 s,
taking per-channel peaks — content level ≈ 56~85% (real content), ~10% is the dither floor
of the 16-bit asset's silent sections (a normal silence reading), 0% is digital zero
(content lost to the send-side clamp).

| AgoraRtcEngine_macOS | Negotiation | Per-channel content on Web | Verdict |
| --- | --- | --- | --- |
| 4.5.2 | multiopus 8ch OK | only CH0/CH1 carry content (identical values), CH2-7 always digital zero | ✗ 2-channel clamp |
| 4.5.3 | multiopus 8ch OK | all 8 channels peak at 74~83%, floor ~10% | ✓ fixed (**fix point**) |
| 4.6.0 | multiopus 8ch OK | all 8 channels peak at 75~84%, floor ~10% | ✓ fixed |
| 4.6.2 | multiopus 8ch OK | all 8 channels peak at 75~85%, floor ~10% | ✓ fixed |
| 4.7.0 | multiopus 8ch OK | all 8 channels peak at 71~83%, floor ~10%; 4ch/6ch assets also pass | ✓ fixed |

Evidence screenshots: `Test/web_recv_8ch_from_macos_{452_control,453,460,462,470}_20261012.png`
and `Test/web_recv_{4,6}ch_from_macos_470_20261012.png`.
Note: there is no macOS SDK 4.6.7 (4.6.2 is followed by 4.7.0; iOS has a separate 4.6.4).
On 4.5.3+ the peaks appear on different channels in sequence, matching the asset's
rotating-channel structure; a 4.7.0 → 4.7.0 native interop run additionally proved via
receiver-side PCM dump that dump channel i corresponds to source channel i (envelope NCC
0.67~0.78, cross-channel ≈ 0 — no reordering). The full summary lives in
`Test/macos_sender_multi-channel_audio_test_result.md`.

**Historical finding on 4.5.2 (verified 2026-10-10)**: the public 4.5.2 reads at most 2
channels from a custom-track push input — negotiation works (the remote side decodes a
multiopus stream with the configured channel count, verified for 8ch/6ch/4ch), but only
the first 2 channels carry content and channels 3+ are silent (proven by both per-channel
volume levels on the web demo and per-user PCM dumps on the receiver); identical results
with `pushExternalAudioFrameRawData` and `pushExternalAudioFrameSampleBuffer` (whose ASBD
self-describes 8 channels); neither `setExternalAudioSource(channels: 8)` nor the private
parameter `che.audio.input_channels: 8` lifts the clamp (both return 0 but change
nothing). This limitation is lifted from 4.5.3 onward (see the matrix above).

Also note: `pushExternalAudioFrameRawData`'s `samples` is the **total sample count
across all channels** (same usage as the official API-Examples CustomAudioSource),
unlike Linux `sendAudioPcmData`'s per-channel semantics. Multi-channel **decoding** on
macOS (including 3/5/7 odd channels) is unaffected — see the receiver README.
