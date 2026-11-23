# Agora Linux SDK Multi-Channel Audio (multiopus) Demo

A standalone C++ demo for multi-channel audio send/receive with the Agora Linux SDK
(v4.4.30.149, x86_64 / aarch64), written against the requirements in
`Docs/audio_multichannel_spec.md`. It covers 4 send paths and 4 receive paths.
Multi-channel encoding uses Opus multistream (OPUSMC, payload type 123/124/125) and
**supports 48 kHz only**.

[中文文档](README.md)

## Features

| Sample | Direction | Spec coverage | Description |
| --- | --- | --- | --- |
| `sample_send_pcm_multiopus` | Send | Req. 1 (PCM send), Req. 4 (private params & negotiation) | Raw PCM multi-channel send, 10 ms frames, 3~8 channels configurable |
| `sample_send_wav_multiopus` | Send | Req. 2 (WAV send), Req. 4 | Multi-channel WAV send: RIFF parsing, 16-bit sent as-is, 24-bit converted to 16-bit, unsupported bit depth, channel count, and non-48 kHz rejected before connecting |
| `sample_send_opus8` | Send | Req. 3 (pre-encoded Opus send) | 8-channel raw Opus stream (640 bytes per 20 ms frame), `AUDIO_CODEC_OPUSMC` + `MIX_DISABLED` |
| `sample_send_mkv_multiopus` | Send | Req. 1/4/10 (MKV 4xstereo asset) | Decodes MKV with ffmpeg: 4 AC-3 stereo tracks resampled/reordered into 8-channel 48 kHz audio, plus mpeg4 video converted to I420 and sent on PTS cadence (the only sample that depends on ffmpeg) |
| `sample_receive_pcm_multiopus` | Receive | Req. 5 (decoded PCM callback) | Per-user multi-channel PCM via `onPlaybackAudioFrameBeforeMixing`, written to disk (new file every 100 MB) |
| `sample_receive_encoded_audio` | Receive | Req. 6 (encoded frame callback) | `audioRecvEncodedFrame=true` + `onEncodedAudioFrameReceived`, no decoding, no playback |
| `sample_receive_mixed_audio` | Receive | Req. 7 (mixed-audio receive) | Mixed output via `onPlaybackAudioFrame` (the SDK always mixes to 2 channels, odd/even split; the demo `--numOfChannels` accepts 1 or 2, default 2), written to disk (new file every 100 MB) |
| `sample_receive_av_multiopus` | Receive | Req. 5 + Req. 8 (AV receive) | Multi-channel PCM dump plus I420 frames via `IVideoFrameObserver2` dumped to a YUV file, with periodic progress logs |

> Req. 10 (4×stereo → 8-channel layout mapping) is an integration-side convention;
> see "4×Stereo Layout Mapping" below.

## Directory Layout

```
Demo/linux/
├── CMakeLists.txt            # Top level; SDK path set via AGORA_SDK_DIR
├── build.sh                  # Helper script: ./build.sh [x86_64|aarch64] [clean]
├── common/                   # Copied from LinuxSDKFolder/X86_64 example/common (matches SDK v4.4.30.149)
├── multiopus/                # 4 sender samples
├── receive/                  # 4 receiver samples
├── tools/                    # Per-channel PCM content analysis tool
├── public/                   # Assets: 8ch_2.opus, 8_Channel_ID_signed_16_bit_pcm.wav (+ same-name .raw.pcm),
│                             # 4/6_Channel_ID_48k.wav (48 kHz resampled), 8_Channel_ID_24bit.wav (24-bit verification asset),
│                             # odd_3ch/odd_5ch/odd_7ch.wav (3/5/7 odd-channel assets, cut from the 8ch one via ffmpeg channelmap),
│                             # VictoriaMulti.mkv (1 mpeg4 1080p stream + 4 AC-3 44.1 kHz stereo tracks = 8 channels; committed via Git LFS, ~133MB)
└── out/                      # Build output (binaries + copy of public assets)
```

> Note: `common/` is kept byte-identical to the SDK's own example code for easy comparison
> and future syncing. The following **upstream issues found by static analysis are recorded
> only, intentionally not fixed**:
>
> 1. `sample_local_user_observer.h/.cpp` uses `std::string` without including `<string>`
>    itself (relies on transitive includes; fails on libc++ variants with a different
>    include graph, but works with the Linux GCC / current SDK combination);
> 2. `sample_local_user_observer.cpp` `calculate_layout` contains a self-assignment
>    `y_offset_ = y_offset_;` (no-op leftover from the video-mixing path, never reached by
>    the audio-only demo);
> 3. `sample_connection_observer.cpp` `onTransferStatistics` prints `uint64_t` with `%lu`
>    (works on Linux x86_64/aarch64 where `unsigned long` is 64-bit; undefined behavior on
>    other platforms);
> 4. `file_parser/helper_h264_parser.cpp` and `helper_h265_parser.cpp` constructors do not
>    initialize the `data_size_` member (`file_parser/` is not compiled into this project's
>    samples; it is only copied along with `common/`).

## Build

### SDK preparation: automatic extraction during builds

`LinuxSDKFolder/X86_64/` and `LinuxSDKFolder/Aarch64/` now contain only the original SDK
archives (`Agora-RTC-<arch>-linux-gnu-*.tgz`). Extracted `agora_rtc_sdk/` directories are
not tracked in Git. Both `build.sh` and the Dockerfile locate and extract the appropriate
archive for the target architecture, then verify `agora_rtc_sdk/agora_sdk/libagora_rtc_sdk.so`
and the headers. No manual preprocessing is required. The extracted directories are excluded
by both `.gitignore` and `.dockerignore` to keep them out of commits and Docker contexts.

Manual CMake still requires extracting the archive first:

```bash
# x86_64 example; same for aarch64, using the package under LinuxSDKFolder/Aarch64/
cd LinuxSDKFolder/X86_64
tar -xzf Agora-RTC-x86_64-linux-gnu-v4.4.30.149-20260127_144108-1004142.tgz
```

### Helper script (recommended)

```bash
cd Demo/linux
./build.sh x86_64     # or ./build.sh aarch64; with no argument, arch is picked from uname -m
./build.sh clean      # remove build/ and out/
```

The script selects the SDK directory, extracts the archive when needed, and verifies that
`agora_sdk/libagora_rtc_sdk.so` exists.

To check the MKV temporary file permissions, symlink protection, and cleanup independently:

```bash
c++ -std=c++11 -Wall -Wextra -Werror tests/private_pcm_files_test.cpp -o build/private_pcm_files_test
./build/private_pcm_files_test
```

After building and preparing `out/public/`, run the connection-failure cleanup regression
for all four senders:

```bash
export LD_LIBRARY_PATH="$(pwd)/../../LinuxSDKFolder/Aarch64/agora_rtc_sdk/agora_sdk${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
bash tests/connection_failure_regression.sh
```

Use `X86_64` instead of `Aarch64` for that architecture. The test uses an invalid App ID,
checks the connection wait timeout and exit status, and prints the directory containing its logs;
no real App ID or token is required.

### Lifecycle and subscription regression

After rebuilding, run `APP_TOKEN='<credential>' bash tests/lifecycle_regression.sh` on Linux
with RTC network access. The credential is the value accepted by the samples' `--token` option;
use an App ID for a project without certificate authentication. The script cycles publication
and subscription on the same connections, restarts each participant with the same UID, and
tests rapid signal-driven exit. It checks API results, callbacks, PCM growth, process exit and
application or fatal SDK logging errors. Separate SDK logs, application logs, and PCM are retained
in the printed temporary directory. Configure `CYCLES`, `TIMEOUT_SEC`,
`SETTLE_SEC`, `OBSERVE_SEC`, `MIN_GROWTH_BYTES` (default 38400), `CHANNEL_ID`, `SENDER_UID`, `RECEIVER_UID`, and `PCM_INPUT` as
needed. Credentials must be valid for the chosen channel and both UIDs.

The opt-in `--lifecycleControl 1` uses SIGUSR1 to toggle publication or subscription; wait for
each operation's log before sending another signal. The receiver flushes each frame in this
mode. This checks file growth, not channel content, and does not replace long-duration, token
renewal, or network recovery testing. Run `python3 tests/lifecycle_harness_test.py` for the
offline positive and negative tests of the script itself.

### Per-channel PCM content analysis

The build also produces `out/pcm_channel_analyzer`. It reads a source WAV (16/24-bit integer PCM)
or raw 16-bit PCM and the remote raw PCM written by `sample_receive_pcm_multiopus`, then prints RMS,
peak, the best one-to-one source-to-received mapping, and correlation for every channel. A channel
count, correlation, or RMS check failure returns non-zero, so channel reordering, loss, duplication,
and clear crosstalk are detected. A non-empty file or non-silent channels alone are not treated as
success.

For example, after sending the 8-channel WAV and receiving UID 8899:

```bash
./out/pcm_channel_analyzer \
  --source public/8_Channel_ID_signed_16_bit_pcm.wav \
  --received received_8ch.pcm.uid-8899 \
  --channels 8 --sample-rate 48000 --max-seconds 5
```

Use `--channels` 3/4/5/6/7/8 for the corresponding cases. A raw PCM source also needs
`--source-channels N`. If the receiver starts in the middle of the source file, use
`--source-offset-ms <milliseconds>` to align the comparison window. The default comparison window
is the first five seconds, so the tool can be used as a gate after each multi-channel regression.

The SDK-independent synthetic tests can be run with:

```bash
bash tests/run_pcm_channel_analysis_tests.sh
```

### Manual CMake

```bash
cd Demo/linux
mkdir build && cd build
cmake .. -DAGORA_SDK_DIR=../../LinuxSDKFolder/X86_64/agora_rtc_sdk
make -j8
```

`AGORA_SDK_DIR` accepts relative or absolute paths. Linking matches the SDK's own examples
(`agora_rtc_sdk` + `pthread/dl/rt/m`; `libagora-fdkaac.so` and `libaosl.so` are loaded at
runtime from the SDK directory via rpath).

## Docker

Run these from the **repository root** (`AudioMultiChannelDemo/`) — the build context must
include both `Demo/linux` and `LinuxSDKFolder`:

```bash
# Build the image (SDK arch is picked from the build machine: X86_64 / Aarch64)
docker build -f Demo/linux/Dockerfile -t agora-multichannel-linux .

# Cross-build an ARM64 image (requires buildx + QEMU, or build natively on ARM)
docker buildx build --platform linux/arm64 \
    -f Demo/linux/Dockerfile -t agora-multichannel-linux:arm64 .
```

Inside the image: binaries and assets are in `/app` (including `public/`), and the SDK shared
libraries are in `/opt/agora/lib` (wired up via `LD_LIBRARY_PATH`). The image runs as a
non-root user (UID 10001); `/app` is writable for receiver output and SDK logs. Any mounted
output directory must also be writable by that user. Start with the default container network
and ensure RTC UDP traffic is allowed:

```bash
docker run --rm -it agora-multichannel-linux \
    ./sample_send_wav_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

All samples take the same arguments as in "Run Examples" below — just replace the part after
`docker run ... agora-multichannel-linux` with the command you want. Use `--network host`
only when required by the network environment.

## Run Examples

Run the commands below from the `out/` directory. As in the reference samples, `--token`
carries either a token or an appId (pass the appId when no certificate is enabled).

### 1. Send an 8-channel WAV file (16-bit)

```bash
./sample_send_wav_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

> Other WAV assets: use `public/8_Channel_ID_24bit.wav` to verify 24-bit conversion
> (scenario 2.2, same command); use `public/4_Channel_ID_48k.wav` / `public/6_Channel_ID_48k.wav`
> (resampled from 44.1 kHz to 48 kHz) for 4/6-channel verification with
> `--numOfChannels 4 --custom_channel_num 4 --custom_payload_type 123` and
> `--numOfChannels 6 --custom_channel_num 6 --custom_payload_type 124`. For odd channel counts,
> use `public/odd_3ch.wav` / `odd_5ch.wav` / `odd_7ch.wav` (cut from the 8ch asset via ffmpeg
> channelmap) — e.g. 5 channels: `--audioFile public/odd_5ch.wav --numOfChannels 5
> --custom_channel_num 5 --custom_payload_type 124 --sampleRate 48000` (interop verified, see
> `Docs/audio_multichannel_test_summary.md`).

### 2. Send raw multi-channel PCM (3~8 channels)

```bash
# Default asset: public/8_Channel_ID_signed_16_bit_pcm.raw.pcm (48kHz/8ch/16-bit, extracted from the same-name WAV)
./sample_send_pcm_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.raw.pcm" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

### 3. Send a pre-encoded 8-channel Opus raw stream

```bash
./sample_send_opus8 --channelId <name> --token <token> --audioFile "public/8ch_2.opus"
```

### 4. Receive multi-channel PCM (per-user callback + dump)

```bash
./sample_receive_pcm_multiopus --channelId <name> --token <token> \
    --numOfChannels 8 --sampleRate 48000 --audioFile received_8ch.pcm
```

For PCM/AV continuity regression, explicitly add `--traceFrames 1` to emit `PCM_FRAME_TRACE` with
RTP timestamps and `renderTimeMs`, allowing timestamp regressions and abnormal intervals to be checked.
Duplicate or non-uniform RTP timestamps are not treated as packet loss by themselves. This option is
off by default; assess log volume before enabling it for long runs.

### 5. Receive encoded frames (raw OPUSMC dump)

```bash
./sample_receive_encoded_audio --channelId <name> --token <token> --audioFile received.opus
```

### 6. Receive mixed output (SDK always mixes to 2 channels; demo can opt for mono)

```bash
./sample_receive_mixed_audio --channelId <name> --token <token> --audioFile received_mixed.pcm
```

### 7. Send an MKV file (4xstereo audio tracks + video)

Asset: `public/VictoriaMulti.mkv` (1 mpeg4 1920x1080 stream + 4 AC-3 44.1 kHz stereo tracks,
carrying 8 channels in total). At startup, ffmpeg decodes the 4 audio tracks once, resamples
them to 48 kHz, and reorders them into interleaved 8-channel PCM in a per-run private
`/tmp/agora-multiopus-XXXXXX/` directory (mode 0700, PCM files at most 0600). Audio is
then sent in 10 ms frames and video on a per-frame PTS cadence, each looping independently.
Channel count 8 / payload type 125 / 48000 Hz are hardcoded:

The conversion runs on every startup. Temporary files are removed on normal exit or
initialization failure. A signal that terminates the process by default (such as SIGTERM or
SIGKILL), or an interruption during conversion, can leave the private directory behind for
the running user or an administrator to inspect and remove.

```bash
# This sample depends on ffmpeg (libavformat/libavcodec/libavutil/libswresample/libswscale);
# install the matching -dev packages for a local build. The Docker image already has them.
./sample_send_mkv_multiopus --channelId <name> --token <token> --userId 8899 \
    --videoFile public/VictoriaMulti.mkv
```

### 8. Receive multi-channel audio + video (AV dump)

```bash
./sample_receive_av_multiopus --channelId <name> --token <token> --userId 8898 \
    --remoteUserId 8899 --audioFile received_av_8ch.pcm --videoFile received_video.yuv
```

- Audio and video are dumped per remote UID as `<audioFile>.uid-<UID>` and
  `<videoFile>.uid-<UID>`: 48 kHz / N-channel / 16-bit interleaved PCM and raw I420 frames
  (~3 MB per frame at 1080p; a new file every 100 MB). For example, play UID 8899 with
  `ffplay -f rawvideo -pix_fmt yuv420p -s 1920x1080 received_video.yuv.uid-8899`;
- Without `--remoteUserId`, the sample subscribes to audio and video of all remote users.

### Force no fallback (codec_unfallback)

All sender samples accept the `--codecUnfallback` flag (note: it takes a value — pass `1`;
the bare flag is rejected by the argument parser):

```bash
./sample_send_pcm_multiopus ... --codecUnfallback 1
```

## Payload Type to Channel Mapping

| payload type | channels | sample rate | channel_mapping | coupled_streams | num_streams |
| --- | --- | --- | --- | --- | --- |
| 123 | 3 | 48000 | 0,1,2 | 1 | 2 |
| 123 | 4 | 48000 | 0,1,2,3 | 2 | 2 |
| 124 | 5 | 48000 | 0,1,2,3,4 | 2 | 3 |
| 124 | 6 | 48000 | 0,1,2,3,4,5 | 3 | 3 |
| 125 | 7 | 48000 | 0,1,2,3,4,5,6 | 3 | 4 |
| 125 | 8 | 48000 | 0,1,2,3,4,5,6,7 | 4 | 4 |

- 123 defaults to 4 channels, 124 to 6, 125 to 8. **For 3/5/7 channels you must set
  `che.audio.custom_channel_num` explicitly** (`--custom_channel_num` in the demo; if unset,
  it follows `--numOfChannels`).
- If `--custom_payload_type` is not set, the demo derives it from the channel count
  (≤4 → 123, ≤6 → 124, otherwise → 125).
- To confirm the settings took effect, look for this log line:
  `MultistreamEncoder channel num:N rate:48000 coupled:M channel_mapping:... bitrate:...`.

## Private Parameter Timing (Important)

| Private parameter | Set on | When |
| --- | --- | --- |
| `che.audio.custom_payload_type` / `che.audio.custom_channel_num` / `che.audio.custom_sample_rate` | `connection->getAgoraParameter()` | After `createRtcConnection`, before `connect()`; setting them after connect has no effect |
| `che.audio.codec_unfallback` (JSON array, e.g. `[123,124,125]`) | `service->getAgoraParameter()` | **Before `createRtcConnection()`**; setting it at connection level has no effect |

The sender must set the payload type parameters. The receiver does not need them under the
123/124/125 negotiation scheme.

## Verifying the Receiver

### PCM dump files (sample_receive_pcm_multiopus)

Output is raw 48 kHz / N-channel / 16-bit little-endian interleaved PCM. Each remote user gets
an independent file named `<audioFile>.uid-<UID>`; rotated files use `.2`, `.3`, and so on.
Path characters in a UID are replaced with `_`:

```bash
ffplay -f s16le -ar 48000 -ch_layout 7.1 received_8ch.pcm.uid-8899   # 8-channel example
# Or use Audacity: File > Import > Raw Data, choose Signed 16-bit PCM / Little-endian / N channels / 48000 Hz
```

When sending a Channel ID file (`8_Channel_ID_signed_16_bit_pcm.wav`, where each channel has
distinct content), checking the dump channel by channel confirms there is no channel crosstalk
or channel loss.

### End-to-end verification with the Web demo

Use the Web verification demo (`Demo/web/` in this repo, `agora-rtc-sdk-ng@4.24.10` mainline build, Chrome 96+):

1. Create the client with `AgoraRTC.createClient({ mode: "rtc", codec: "vp8", multiopus: true })`.
2. Subscribe to the multi-channel stream published by the Linux side.
3. `getVolumeLevels()` returns an array whose length equals the decoded channel count, and the
   underlying codec shows as `multiopus`.
4. `selectChannels(channels, "surround"|"stereo"|"mono")` plays any combination of channels.

Note: the Web side decodes even channel counts only — a 3/5/7-channel stream raises no error there but
is mis-decoded as the next even count (the last channel is duplicated into a phantom one and the demo
even shows a false multi-channel ✓); results are untrustworthy, so verify odd channels with a
Native/Linux receiver (see `Docs/audio_multichannel_test_summary.md` §5).
Safari/Firefox behavior is undefined.

### Encoded frame dump (sample_receive_encoded_audio)

Output is a per-user raw OPUSMC frame stream (640 bytes per frame), suitable for forwarding or
custom decoding. Files are named `<audioFile>.uid-<UID>`. The callback `info` carries only
`codec` and `sendTs` (in SDK v4.4.30.149,
`AudioEncodedFrameInfo` in `AgoraMediaBase.h` has no channel-count or sample-rate fields).

Timestamp validation separates codec behavior from transport continuity. The default monitor mode
requires strictly increasing `sendTs` values. A gap above 1920 ticks is logged as
`APP_LOG_WARNING` so packet loss, reordering, and late delivery can be observed without directly
failing the codec case or changing the process exit code. Non-monotonic timestamps always log
`APP_LOG_ERROR` and produce exit code 1.

Enable the strict transport gate only on a controlled, no-loss network:

```bash
./sample_receive_encoded_audio --channelId <name> --token <token> \
    --audioFile received.opus --strictTimestampContinuity 1
```

Strict mode allows one subscription-warmup interval. Later gaps log `APP_LOG_ERROR` and produce
exit code 1. `tests/full_regression.sh` uses monitor mode by default; apply the strict gate to all
encoded-frame scenarios with:

```bash
APP_ID='<credential>' bash tests/full_regression.sh --strict-timestamp-continuity
```

### Mixed audio dump (sample_receive_mixed_audio)

This callback contains the SDK's global mix and has no remote-UID dimension, so it remains one
aggregate output file.

Output is mixed 48 kHz / 16-bit little-endian interleaved PCM, 2 channels by default
(multi-channel streams are always mixed down to 2 channels — odd channels to one side, even
channels to the other; `--numOfChannels 1` outputs mono):

```bash
ffplay -f s16le -ar 48000 -ch_layout stereo received_mixed.pcm
```

## 4×Stereo Layout Mapping (Req. 10)

On the source side, 8 channels are carried as 4 stereo tracks in **track-block order**
(`[T1L,T1R]×N [T2L,T2R]×N [T3L,T3R]×N [T4L,T4R]×N`). This buffer **cannot** be passed directly
to `sendAudioPcmData`. It must first be reordered on the application side into Agora's
8-channel **sample-interleaved** layout:

```
Channel order: CH1=T1L, CH2=T1R, CH3=T2L, CH4=T2R, CH5=T3L, CH6=T3R, CH7=T4L, CH8=T4R
The buffer repeats [T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R] for every sample
```

- Channel pairs (0,1)(2,3)(4,5)(6,7) map to tracks 1~4, matching the 4 coupled streams of
  8-channel OPUSMC.
- One source-side write is 15360 bytes = 8 channels × 16-bit × 960 samples = 20 ms @ 48 kHz; split it
  into 10 ms frames before calling the SDK.
- A 44.1 kHz source (PCM decoded from AC-3) must be resampled to 48 kHz first. Resampling and
  reordering can happen in either order, but both must finish before `sendAudioPcmData`.
- This mapping was confirmed by both sides in the 2025-04 joint debugging session. (The root
  cause of the earlier noise issue on the receiver was reading sample-interleaved data as
  planar blocks.)

## Notes

- **48 kHz only**: OPUSMC encoding/decoding supports 48000 Hz only. 44.1 kHz files (such as
  `4_Channel_ID.wav`, `6_Channel_ID.wav`) must be resampled first. The WAV sender rejects them
  before connecting; it does not resample them.
- **Channel count consistency**: the PCM frame channel count, `--numOfChannels`,
  `custom_channel_num`, the WAV header channel count, and the receiver's
  `setPlaybackAudioFrameBeforeMixingParameters` channel count must all match (a hard
  requirement above 4 channels), or the full multi-channel content cannot be recovered.
- **codec_unfallback placement**: it must be set on `service->getAgoraParameter()` before
  `createRtcConnection()`; setting it at connection level does not work (verified by CSE).
- **When a non-multi-channel endpoint joins**: the Linux sender falls back to plain Opus
  (OPUSFB) and multi-channel stops working. Use `--codecUnfallback` to force multi-channel
  for the whole channel, and make sure every participant supports it.
- **Pre-encoded Opus input format**: `sample_send_opus8` accepts only a raw 8-channel 48 kHz
  Opus frame stream (640 bytes per frame, not ogg). The frame format must match
  `EncodedAudioFrameInfo` exactly.
- **Asset location**: after building, `build.sh` copies `public/` into `out/`; samples read
  files with relative paths by default.
- **The MKV sender needs ffmpeg**: `sample_send_mkv_multiopus` is the only target that depends
  on ffmpeg (libavformat/libavcodec/libavutil/libswresample/libswscale). Local builds need
  `pkg-config` and the matching `-dev` packages; when they are missing, CMake prints a warning
  and skips this target while the other 7 samples still build. The Dockerfile installs the dev
  packages in the builder stage and the runtime libraries in the final stage. `public/VictoriaMulti.mkv`
  (~133 MB) is committed via Git LFS (the root `.gitignore` `*.mkv` rule has an exception
  `!Demo/linux/public/VictoriaMulti.mkv`) — if you see a pointer file under `public/` after
  cloning, run `git lfs pull` first; the Docker build packs it into the image.
- **Video sending is limited to the MKV sample**: the other sender samples handle audio only
  (the h264 part of the reference sample was removed). `sample_send_mkv_multiopus` runs audio
  and video on separate threads with independent loops and does not do strict AV sync.
- The SDK mixing/playback path always outputs 2 channels (odd channels mixed to one side, even
  channels to the other; the mixed-audio sample's `--numOfChannels 1` outputs mono). For
  per-channel data, use `sample_receive_pcm_multiopus` or
  `sample_receive_encoded_audio`.
