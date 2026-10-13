# macOS Multi-Channel Audio Receiver Demo (MultiChannelAudioReceiver)

A **pure receiver** verification app built on the Agora Native SDK for macOS
(`AgoraRtcEngine_macOS` 4.7.0, integrated via CocoaPods). It joins a channel as
**audience**, receives a remote host's multi-channel (OPUSMC, payload types
123/124/125, 48 kHz, 3~8 channels) or normal stereo/mono audio stream, and lets the
tester **select which channels to hear and set per-channel volume** through Native SDK
private parameters. See `Spec/native_sdk_audio_multichannel_spec.md` for the requirements
baseline.

> Companion sender-side app: see `../MultiChannelAudioSender/` (broadcaster role publishing
> bundled multi-channel WAV assets).

[中文文档](README.md)

## UI Language

The UI is bilingual (**English / 中文**) and defaults to English. Switch it with the
Language picker at the top of the main view — control labels re-render immediately, while
status-bar text generated at event time refreshes on the next status update —
and the choice is persisted via `@AppStorage("uiLanguage")` across launches. The strings
live in `MultiChannelAudioReceiver/L10n.swift` (lookup table + String(format:)
interpolation); log and status lines are emitted in the language active at the time
(already-appended log lines keep their original language).

## Features

- **Receive-only**: joins in `.liveBroadcasting` profile with the audience role;
  microphone/camera are never published; auto-subscribes remote audio and video.
- **Remote video display (on demand)**: the UI is a two-column "control | monitor" layout —
  the left column selects and controls the remote user, the right "Monitor — UID xxx" column
  shows the selected user: when the host publishes video the picture renders at full column
  width (`setupRemoteVideo`, aspect driven by the stream's real dimensions); audio-only hosts
  show just the bitrate chart plus an "Audio only" note.
- **Receive bitrate charts**: the selected remote user gets separate audio and video receive
  bitrate sparklines with Y-axis labels (max/0) — audio in green, video in blue, each covering
  the last 60 `remoteAudioStats`/`remoteVideoStats` samples (~2-minute window); the video
  chart only appears while the host publishes video.
- **Remote user list**: driven by remote audio **or video** publish state — uids are added in the
  `remoteAudioStateChangedOfUid` / `remoteVideoStateChangedOfUid` callbacks when media is
  starting/decoding and removed on `didOfflineOfUid`, so **only hosts actually publishing
  media are listed; audience-role clients never appear**. All three host shapes are covered:
  audio+video, audio-only, and video-only (selecting a video-only host shows a "Video only"
  note in the control column and sends no volume parameters).
- **Channel-count selector (1~8, default 8)**: a **pure display filter** — it only decides
  how many channel rows are rendered and changes nothing you hear. It exists because the
  Native SDK receiver has no callback reporting the remote stream's actual channel count,
  so you align the UI with the sender's material manually: choose 2 for a normal stereo
  stream (only channel 1/2 rows), keep 8 for an 8-channel stream, 3/5/7 accordingly. Two
  caveats: with no remote user selected, switching it has no visible effect (channel rows
  are not rendered then); hidden channels keep their volume state (all enabled at 100 by
  default), so choosing 2 does NOT mute channels 3~8 of an 8-channel stream — use a row's
  toggle to mute.
- **Per-channel control**: each row has an enable toggle and a 0~100% volume slider,
  applied immediately through the `che.audio.neteq.adjust_decoded_volume` private
  parameter. All channels default to enabled at 100.
- **Audio frame dump**: `che.audio.frame_dump` toggle. Verified on macOS 4.5.2 / 4.7.0: dumps land in
  **`~/Library/Logs/`** — per-user decoded output
  `audio_dump_playback_before_mixed_0_<yyyy_MM_dd_HHmmss>_<uid>.wav`
  (48 kHz / N ch / 16-bit, stops growing at ~120 MB; Leave+Join starts a new timestamped file);
  mixed output `audio_dump_playback_mixer_0.wav` (always 2 ch); playout `audio_dump_play_0.wav`;
  far-end mix `far_in_48000hz_2ch.wav` (2 ch).
- **Log area**: scrolling log at the bottom covering join/leave, remote user events,
  connection state changes and SDK errors.

## Prerequisites

- macOS 12.0+ (deployment target 12.0), Xcode 14+ (verified with Xcode 16.2)
- CocoaPods (`pod` on PATH)

## Build

```bash
cd Demo/macOS/MultiChannelAudioReceiver
pod install        # integrates AgoraRtcEngine_macOS (version pinned in the Podfile), creates MultiChannelAudioReceiver.xcworkspace
open MultiChannelAudioReceiver.xcworkspace   # open the workspace in Xcode (NOT the xcodeproj)
```

Command-line build:

```bash
xcodebuild -workspace MultiChannelAudioReceiver.xcworkspace \
    -scheme MultiChannelAudioReceiver -configuration Debug \
    CODE_SIGNING_ALLOWED=NO build
```

The project uses ad-hoc signing (`CODE_SIGN_IDENTITY = "-"`), no entitlements and no
sandbox, so local builds need no developer certificate.

## Usage

1. **Start a multi-channel sender** (e.g. the 8-channel WAV sender in Demo/linux):

   ```bash
   cd Demo/linux/out
   ./sample_send_wav_multiopus --channelId <channel> --token <token> \
       --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
       --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
   ```

2. **Join the same channel from this app**: the App ID is pre-filled with the default
   `43f818097c124394821bd44598b7ca9d` (your edits are persisted via `@AppStorage` across
   launches); fill in Token (leave empty for projects without an App Certificate),
   Channel ID and UID (0 = auto-assign), then
   click "Join". The status line shows the connected state plus the assigned local UID.
3. **Select the remote user**: pick the sender's uid in the "Remote Multi-Channel Control"
   section
   (the first remote is auto-selected). The current state of all 8 channel rows is
   pushed to that uid immediately upon selection.
4. **Listen per channel**: mute/enable a channel with its toggle, or drag its slider to
   set the volume percentage. When the sender plays the Channel ID sample (each channel
   carries distinguishable content), check channels one by one for cross-talk.
5. For a normal stereo stream, set "Channels" to 2 so only channel 1/2 rows appear (a pure
   display filter that does not change what you hear; if channels 3~8 are still audible,
   that is because hidden rows stay enabled at their default 100% — mute them explicitly
   with their toggles if needed).

## Private parameters

### Per-channel decoded volume `che.audio.neteq.adjust_decoded_volume`

```json
{"che.audio.neteq.adjust_decoded_volume":{"remoteUid": 109016691, "index": 1, "volume": 100}}
```

- `remoteUid`: target remote user uid;
- `index`: **1-based** channel index (1~8);
- `volume`: 0~100, a percentage. `100` = full volume (audible), `0` = muted.
- "Selecting channels to hear" = set chosen channels to 100 and the rest to 0.

### Audio frame dump `che.audio.frame_dump`

```json
{"che.audio.frame_dump":true}
```

When enabled, the SDK writes decoded audio frames to disk for offline channel-content
verification. On macOS 4.5.2 and 4.7.0 the dump directory is the same: `~/Library/Logs/`
(file naming in "Features" above).

## Command-line automation (for regression testing)

Launch arguments (mapped into `UserDefaults`) replace UI interaction; the app log is also
mirrored to stdout unbuffered:

```bash
./MultiChannelAudioReceiver.app/Contents/MacOS/MultiChannelAudioReceiver \
    -autoChannel <channel> -autoJoin YES -autoDump YES -autoQuitAfter 120
```

- `autoJoin`: join as audience on launch (uid 0 = auto-assign); `autoDump`: enable
  `che.audio.frame_dump` at join; `autoQuitAfter`: seconds until the app terminates
  itself (> 0).
- Used on 2026-10-12 for the 4.7.0 ↔ 4.7.0 native interop regression (per-channel NCC
  check against the dump); results in `Test/macos_sender_multi-channel_audio_test_result.md`.

## Notes

- **No multi-channel parameter is needed on the receiver**: the Native SDK decodes OPUSMC
  multi-channel streams adaptively under the 123/124/125 negotiation mechanism (including
  odd channel counts 3/5/7), and receives normal stereo/mono streams without any special
  handling. Payload type / `custom_channel_num` parameters are only required on the
  **sender** side (see `Demo/linux/README.en.md`).
- **Odd channel counts (3/5/7)**: decoded adaptively on the Native side (interop verified
  2026-10-12, see `Test/odd_channels_interop_test_20261012.md`); the Web side
  (`Demo/web`) raises no error for such streams but mis-decodes them as N+1 channels (the last
  channel is duplicated into a phantom one and the demo shows a false multi-channel ✓) —
  results are untrustworthy, so never verify odd-channel streams on Web (§5 of the same document).
- **The channel-count selector only controls the UI rows**: switching it pushes no SDK
  parameter; setting an `index` beyond the stream's actual channel count has no effect in
  the SDK; whenever the selected uid changes, the state of all 8 rows (including hidden
  ones) is re-applied so behavior stays deterministic.
- **The volume parameter acts after decoding, before playout**: it only affects what this
  client hears, not what other participants in the channel hear.
- The macOS SDK has no `didOccurWarning` callback (neither 4.5.2 nor the currently integrated
  4.7.0 headers expose it — only `didOccurError`); the log area reports
  `didOccurError` plus connection state changes.
- The app is unsandboxed and never captures the microphone (the audience role publishes
  no audio track), so it does not request microphone permission.
