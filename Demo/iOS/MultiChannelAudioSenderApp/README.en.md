# iOS Multi-Channel Audio Sender Demo (MultiChannelAudioSenderApp)

A **sender-side** verification app built on the Agora Native SDK for iOS
(`AgoraRtcEngine_iOS` 4.7.0, CocoaPods; version pinned in the Podfile): joins a channel
as a **broadcaster** and publishes multi-channel WAV material (48 kHz / 16-bit / N channels)
in a loop through a custom audio track. This project is a full port of the macOS app
(`Demo/macOS/MultiChannelAudioSender/`) with feature parity; pair it with the
`MultiChannelAudioReceiver` app (macOS), a Linux receiver, or `Demo/web/` for verification.

[中文文档](README.md)

## UI Language

English / 中文, defaults to English; switch via the Language picker at the top
(persisted with `@AppStorage("uiLanguage")`).

## Features

- **Host publishing**: `.liveBroadcasting` + broadcaster role, `disableVideo()`, no
  microphone/camera publish; the WAV is published via `publishCustomAudioTrack`.
- **Bundled assets** (in `Resources/`, from `Demo/linux/public/`; per-channel
  distinguishable Channel ID content):
  - `8_Channel_ID_signed_16_bit_pcm.wav` (8ch, payload 125)
  - `6_Channel_ID_48k.wav` (6ch, payload 124)
  - `4_Channel_ID_48k.wav` (4ch, payload 123)
  - `odd_3ch.wav` / `odd_5ch.wav` / `odd_7ch.wav` (3/5/7 odd channels, payload 123/124/125
    plus `custom_channel_num`; cut from the 8ch asset via ffmpeg channelmap)
- **WAV parsing**: minimal RIFF parser (`WavReader.swift`) handling both PCM (format=1)
  and WAVE_FORMAT_EXTENSIBLE (0xFFFE, PCM decided by the SubFormat GUID); only
  48 kHz / 16-bit accepted, anything else is rejected at load time.
- **Multi-channel private parameters** set before join: `che.audio.custom_payload_type`
  (≤4ch→123, ≤6ch→124, otherwise→125), `che.audio.custom_channel_num`,
  `che.audio.custom_sample_rate`.
- **Pacing**: 10 ms-per-frame pacer loops the file via `pushExternalAudioFrameRawData`
  (`samples` = total sample count across all channels); the UI shows sent frame count /
  audio duration.
- **Audio frame dump**: `che.audio.frame_dump` toggle (applied on join).
- **Odd-channel warning**: selecting a 3/5/7-channel asset (indices 3/4/5, entries marked
  with ⚠) shows an orange banner — odd-channel streams are verifiable only with
  Native/Linux receivers; the Web SDK mis-decodes them (as N+1 channels with a duplicated
  phantom channel and a false multi-channel ✓ verdict), so results are untrustworthy.
  Boundary note: since 2026-10-12 `Demo/web` ships an `oddChannelFix.ts` runtime patch
  that fixes demo-layer detection/playback (on by default), but the **Web SDK itself still
  does not support odd channel counts** — the Native-side guidance "verify odd channels
  with Native/Linux receivers" is unchanged (see `Docs/audio_multichannel_test_summary.md` §4.2).
- **Background publishing**: `UIBackgroundModes = audio` in `Info.plist` keeps RTC audio
  publishing alive when the app goes to background (`AVAudioSession` is managed by the
  Agora SDK itself; no app-side audio-session code is required).
- **No local capture**: the app only pushes WAV files and never uses the microphone or
  camera, so `Info.plist` intentionally carries no `NSMicrophoneUsageDescription` /
  `NSCameraUsageDescription`.

## Prerequisites & Build

- iOS 14.0+ (the SwiftUI `@main App` lifecycle, `@AppStorage`, `onChange`, and
  `ScrollViewReader` all require iOS 14; the macOS counterpart's floor, macOS 12.0, is the
  same SwiftUI-App starting point), Xcode 16, CocoaPods,
  [XcodeGen](https://github.com/yonsm/XcodeGen) (`brew install xcodegen`)

```bash
cd Demo/iOS/MultiChannelAudioSenderApp
xcodegen generate   # generates MultiChannelAudioSenderApp.xcodeproj from project.yml
pod install         # integrates AgoraRtcEngine_iOS (version pinned in the Podfile), produces the xcworkspace
open MultiChannelAudioSenderApp.xcworkspace
```

CLI build (simulator, unsigned):

```bash
xcodebuild -workspace MultiChannelAudioSenderApp.xcworkspace \
    -scheme MultiChannelAudioSenderApp -configuration Debug \
    -destination 'generic/platform=iOS Simulator' \
    CODE_SIGNING_ALLOWED=NO build
```

The project is configured unsigned (`CODE_SIGNING_ALLOWED=NO`) for simulator builds and
runs. **On-device runs require your own signing**: open the xcworkspace in Xcode and pick a
Team under Signing & Capabilities.

> An iOS-specific detail of unsigned simulator builds: the iOS simulator runtime on Apple
> Silicon requires every loaded image to carry at least an ad-hoc signature, while
> `CODE_SIGNING_ALLOWED=NO` makes CocoaPods skip signing the embedded frameworks. An
> unsigned `AgoraRtcKit.framework` is then refused by dyld — and because the framework is
> weak-linked, the symptom is subtle: the `AgoraRtcEngineKit` class binds to 0 and
> `sharedEngine` silently returns nil (no SDK log at all). This project's Podfile therefore
> adds a build phase via a `post_integrate` hook (ordered after
> `[CP] Embed Pods Frameworks`) that ad-hoc signs the embedded frameworks for unsigned
> simulator builds, so the product of the build command above can be installed and run on
> the simulator directly.

## Usage

1. Pick a WAV asset (disabled while joined; Leave first to switch);
2. Fill in App ID (pre-filled) and channel name, UID 0 = auto-assign, tap Join;
3. Tap Publish to start looping; Stop to halt;
4. Verify on the receiving side with `MultiChannelAudioReceiver` (macOS), a Linux receiver,
   or `Demo/web/`: check the codec (multiopus), channel count, and per-channel content.

## Command-line automation (for regression testing)

Launch arguments (mapped into `UserDefaults`) replace UI interaction; the app log is also
mirrored to stdout unbuffered. On the simulator, `xcrun simctl launch --console` captures
stdout directly:

```bash
# install onto a booted simulator first
xcrun simctl install booted <DerivedData>/MultiChannelAudioSenderApp.app
# launch with automation arguments (--console pipes app stdout to your terminal)
xcrun simctl launch --console booted com.agora.demo.MultiChannelAudioSenderApp \
    -autoChannel <channel> -autoUid <uid> -autoWav 0 \
    -autoJoin YES -autoPublish YES -autoQuitAfter 300
```

- `autoWav`: asset index (0=8ch, 1=6ch, 2=4ch, 3=3ch, 4=5ch, 5=7ch); `autoWavPath`: custom
  WAV file path (takes precedence over autoWav; must be 48 kHz / 16-bit / 1~8 channels).
  **Sandbox**: on device only paths inside the app container are readable; on the
  simulator, host absolute paths generally work.
- `autoJoin`/`autoPublish`: join on launch and start publishing once joined;
  `autoQuitAfter`: seconds until self-termination — it stops publishing and leaves the
  channel first, then calls `exit(EXIT_SUCCESS)` after 0.5 s (so the pacer never outlives
  the engine into process teardown, which would otherwise spam -7/NOT_INITIALIZED logs).
- `autoUid` accepts digits only; an invalid (non-numeric) value silently falls back to 0
  (server-assigned uid).
- Pair with the Web client opened as `?channel=<channel>&uid=<webuid>&subTo=<autoUid>` for
  per-version regression (sample `window.__mcRemoteTrack.getVolumeLevels()` per-channel
  peaks; same methodology as the macOS README).

## Audio frame dump location (iOS)

With `che.audio.frame_dump` on, dump files land **inside the app sandbox container**
(unlike macOS, where they go to `~/Library/Logs/`). Export or browse the container via
Xcode → Window → Devices and Simulators → select the device/simulator (on the simulator the
container lives under
`~/Library/Developer/CoreSimulator/Devices/<UDID>/data/Containers/Data/Application/<UUID>/`).
**The exact file naming on iOS has not been verified yet** — treat it as subject to
measurement; the macOS-verified `audio_dump_*` naming is a reference, not a guarantee.

## Send-side multi-channel capability: version notes (iOS not yet verified)

**macOS-verified findings (regression of 2026-10-12; full matrix in
`Demo/macOS/MultiChannelAudioSender/README.md`)**: the public 4.5.2 clamps custom-track
push input to 2 channels (negotiation works, but only the first 2 channels carry content,
CH2+ silent); **fixed from 4.5.3 onward** (4.5.3 / 4.6.0 / 4.6.2 / 4.7.0 all pass 8-channel
end-to-end on macOS; 4.7.0 additionally passes 4ch/6ch assets).

> ⚠ **The iOS send path has not been verified yet**: both the 4.5.2 clamp and the 4.5.3
> fix are macOS-measured conclusions. The *possibility* of the same 4.5.2 send-side
> 2-channel clamp on iOS cannot be ruled out (Native platforms share the same core, so
> behavior is expected to be close), but **iOS behavior must be confirmed by measurement**.
> This app is the regression tool for exactly that: switch the `AgoraRtcEngine_iOS` version
> in the Podfile, publish the 8ch asset via the CLI automation above, and sample
> per-channel peaks on the Web client to produce the iOS matrix. Also note the iOS and
> macOS version trains are not identical (e.g. iOS has a 4.6.4, while macOS has no 4.6.7).

Also note: `pushExternalAudioFrameRawData`'s `samples` is the **total sample count across
all channels** (same usage as the official API-Examples CustomAudioSource), unlike Linux
`sendAudioPcmData`'s per-channel semantics.
