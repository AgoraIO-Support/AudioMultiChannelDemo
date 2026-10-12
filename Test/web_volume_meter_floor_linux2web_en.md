# Case Record: Silent-Channel Volume Bars Not Reading Zero in Linux→Web (the `getVolumeLevels()` Meter Floor)

> Date: 2026-10-08
> Status: **Root cause confirmed — not an SDK defect.** The volume meter uses a wide dB scale
> (`(dBFS_RMS + 100) / 100`; only exact digital zeros read 0%). The 16-bit source material used by
> the Linux sender carries inaudible -84 dB dither in its silent sections, which the meter faithfully
> reports as ~10%. The causal chain was proven end-to-end with a 24-bit-material control experiment
> (see 3.8/3.9). Audible output was clean throughout.
> Related FAQ: "FAQ" in `Demo/web/README.md` / `README.en.md`
> 中文版本：`web_volume_meter_floor_linux2web.md`

## 1. Symptom

On the Live Demo 2 page (`?demo=2`), subscribing as an audience member to the Linux sender
(`sample_send_wav_multiopus`, 8-channel OPUSMC):

- The material is Channel ID (channels announce one at a time), so at any moment only 1 of the 8
  channels carries content;
- **What you hear is correct**: only the announcing channel is audible, the other 7 are silent;
- But the **volume bars look wrong**: the 7 silent channels stay at ~6~10% instead of 0, very close
  to the announcing channel (~12%), making the bars nearly useless for telling which channel speaks.
- The control scenario — **web→web (dual-tab self-verification) — shows no such floor**: silent
  channels read exactly 0.

## 2. Test Environment

| Item | Value |
| --- | --- |
| App ID | `43f818097c124394821bd44598b7ca9d` (project "Test 128 Hosts Env") |
| Channel | `multichannel8ch` |
| Argus callId | `6ac74e1c33d2695a000022b8` |
| Sender | AWS test server (54.152.163.4), Docker container `multiopus-sender`, uid **8888**, Linux SDK 4.4.30.149, payload 125 / 8ch / 48 kHz, looping `8_Channel_ID_signed_16_bit_pcm.wav` |
| Web receiver | Chrome 154, Shanghai side, `agora-rtc-sdk-ng` 4.20.1-200 (`release_20260915_02_vos_web`), uid 433911 (manual test) / 479912 (automated measurements) |
| Web control publisher | browser tab uid 7777, publishing `8_Channel_ID.wav` (8ch) |

Sender launch command (on the server):

```bash
docker run -d --rm --network host --name multiopus-sender agora-multichannel-linux \
  ./sample_send_wav_multiopus --channelId multichannel8ch \
  --token 43f818097c124394821bd44598b7ca9d --userId 8888 \
  --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

## 3. Investigation and Evidence

### 3.1 The source material itself

Per-channel RMS measured in 0.5 s windows over
`Demo/linux/public/8_Channel_ID_signed_16_bit_pcm.wav`:

- Silent windows: RMS ≈ **0.006%** (near digital silence); announcing windows: peaks 4~12%.

### 3.2 The bitstream itself is clean (Linux receiver PCM dump)

A Linux receiver on the AWS server subscribed to uid 8888 and dumped 25 s of decoded PCM:

```bash
docker run -d --rm --network host --name multiopus-recv agora-multichannel-linux \
  ./sample_receive_pcm_multiopus --channelId multichannel8ch \
  --token 43f818097c124394821bd44598b7ca9d --userId 9999 --remoteUserId 8888 \
  --numOfChannels 8 --sampleRate 48000 --audioFile /tmp/recv8ch.pcm
```

Result: silent channels RMS ≈ **0.003%** (near digital silence), announcing peaks 4~12%, matching
the source. → No leakage or cross-talk in the encode → network → decode chain.

### 3.3 Transport quality is clean (Argus)

For both Web receiver sessions (uids 433911 / 479912, peer = 8888):

| Counter | Result |
| --- | --- |
| Wrtc Audio Recv Packets Loss (2045) | **0% throughout** |
| Wrtc Audio Recv Bitrate (2044) | steady ~256 kbps (matches the 8-channel OPUSMC tier) |
| Wrtc Audio Recv Current Delay Ms (2047) | 56~172 ms, normal jitter-buffer range |

→ Rules out cross-Pacific packet loss triggering NetEq PLC comfort noise.

### 3.4 The PCM actually played by the Web side is digital silence (Web Audio tap)

Using the debug hook `window.__mcRemoteTrack` (exposed in `Demo/web/src/Multiopus.vue`), the remote
track was tapped with `AudioContext` + `createChannelSplitter(8)`:

- Caveat: `createMediaStreamSource` must be configured with `channelCount=8` +
  `channelCountMode='explicit'` + `channelInterpretation='discrete'`; otherwise Chrome downmixes to
  stereo by default (only CH0/CH1 carry content, CH2~7 read zero — a measurement artifact).
- Result: silent-period PCM RMS ≈ **0.005%** (digital silence).

→ The data reaching playout/audible output is fine, consistent with what you hear.

### 3.5 Control experiment: web→web meters read exactly zero

Same page, same SDK, same decoder — subscribing to a **browser-published** 8-channel stream
(uid 7777):

| Scenario | Silent-channel `getVolumeLevels()` min | Announcing-channel peak | Tap-measured silence floor |
| --- | --- | --- | --- |
| Linux→web (uid 8888) | **6~9% (floor present)** | 78~84% | 0.005% |
| web→web (uid 7777) | **0% (exact zero)** | 78~80% | 0% |

→ The meter floor appears **only for the Linux-encoded stream**; the Web-encoded stream is clean.

### 3.6 Encoded-frame capture: frame-duration hypothesis ruled out (the CBR/VBR difference was later ruled out too)

Encoded frames captured on AWS with `sample_receive_encoded_audio` (logs carry `length` and
`sendTs`):

| Stream | Frame-length distribution | sendTs delta | Reading |
| --- | --- | --- | --- |
| Linux sender (uid 8888, WAV PCM pushed at 10 ms → SDK encoder) | **constant 640 bytes** (590/590 frames) | constant 960 samples = **20 ms** | CBR ~256 kbps; frames do not shrink during silence |
| Web publisher (uid 7777, `8_Channel_ID.wav`) | 15~260 bytes (mean 109), incl. 240 frames of 15 bytes | constant 960 samples = **20 ms** | VBR; tiny frames during silence (DTX-like shape) |

→ **The "10 ms vs 20 ms frame duration" hypothesis is rejected**: both streams use 20 ms frames on
the wire (the Linux encoder buffers 10 ms PCM pushes into 20 ms Opus frames; the `sendAudioPcmData`
API contract itself only accepts 10 ms frames — see `NGIAgoraMediaNode.h`). The frame-shape
difference observed at the time was **CBR (constant 640 B) vs VBR (15 B silence frames)**.
(**Note: the CBR/VBR difference was later ruled out as well** — in 3.7 the frame shape did not
change across the floor's disappearance; the real root cause is the material's silence floor,
see 3.8/3.9.)

Note: the SDK binary contains private parameters such as `che.audio.bitrate_mode` /
`che.audio.dtx_mode` / `che.audio.acm_ptime` (visible via `strings libagora_rtc_sdk.so`) which could
drive follow-up VBR/DTX experiments. As of 2026-10-08 their semantics are **unverified** (value sets,
defaults, application timing unknown): both the X86_64 and Aarch64 `libagora_rtc_sdk.so` files and
the tgz packages in this repo are stripped (no symbol tables); binary verification would require a
symbolic-table SDK package. Naming-based guesses (unverified): `acm_ptime` = ACM packetization frame
duration; `bitrate_mode` = CBR/VBR selection; `dtx_mode` = DTX toggle (the strings `EnableOpusDtx` /
`DisableOpusDtx` also exist in the binary).

### 3.7 Three sender samples compared, and the floor's session-state behavior (added 2026-10-08 PM)

All three sender samples under `Demo/linux/multiopus/` were run one by one, measured from the same
Web subscriber (Live Demo 1 + `window.__mcRemoteTrack`, per-channel min/max over 16 s windows):

| Sender sample | uid | Stream session state | Wire frame shape | Silent-channel meter min | Floor |
| --- | --- | --- | --- | --- | --- |
| WAV (session #1, 5~15 min after start) | 8888 | fresh | CBR 640B/20ms | 6~12% | **present** |
| WAV (session #1, ~59 min after start) | 8888 | long-lived | CBR 640B/20ms (re-captured, unchanged) | **0%** | gone (faded) |
| PCM (fresh session) | 8889 | fresh | CBR 640B/20ms | **0%** | absent |
| Pre-encoded Opus (opus8) | 8890 | fresh | 640B/20ms pass-through | all channels constantly 32~91% | N/A — material has content on all channels |
| WAV (session #2, after container restart) | 8888 | fresh (measured at ~2 min and ~6 min) | CBR 640B/20ms | 4.6~8.9% | **present (reproduced)** |

Key points:

- **The floor is not specific to any one cpp**: the PCM and WAV paths share the same encoder and the
  exact same private-parameter set (payload 125 / channel_num 8 / sample_rate 48000).
- **The floor correlates with stream-session freshness**: two fresh WAV-sender starts showed the
  floor; it had vanished by ~1 hour of stream age (fade point somewhere between 15 and 60 min).
  (**Further clarified by 3.8/3.9**: the meter reads the real decoded residue; session age affects
  whether the encoder suppresses the material's dither residue to zeros — the material's silence
  floor is the direct source of the meter floor, and the encoder's suppression state decides whether
  it shows.)
- **The CBR/VBR hypothesis was weakened**: before and after the floor faded, the Linux side's wire
  frames were constant 640 B CBR (capture-verified) — the shape did not change while the floor
  disappeared.
- The pre-encoded Opus pass-through path (opus8) works correctly — 8 channels decode and levels
  follow the content; its material (`8ch_2.opus`) has content on all channels at all times, so it
  cannot be used for floor measurement.

### 3.8 Meter implementation source analysis: no Linux/Web branch — a wide dB scale plus the material's silence floor explains the difference

The file `Demo/web/node_modules/agora-rtc-sdk-ng/AgoraRTC_N-production.js` (4.20.1-200) was
pretty-printed and analyzed (line numbers refer to the prettified output):

- Call chain: `RemoteAudioTrack.getVolumeLevels()` (line 26974) → `transformer.getVolumes()`
  (line 21397; `class jb` = the Multiopus Insertable Streams transformer, line 21317).
- Level source: `lastSplitAudioData` — the **actual per-channel decoded PCM** flowing through the
  transformer (each channel plane of `AudioData`, lines 21335-21377). The meter reads real decoded
  data, not a bitstream estimate.
- Per-channel level formula (`Fb`, line 21304):
  `level = max(10*log10(mean(x²)) + 100, 0) / 100`, i.e. **(dBFS_RMS + 100) / 100**; it returns 0
  only when the samples are exactly zero (sum = 0).
- **There is no code branch distinguishing Linux-encoded from Web-encoded streams** — a single
  implementation for all streams.

Conversion table (measured values plugged into the formula):

| Silent-section residue RMS | In dB | Meter reading |
| --- | --- | --- |
| Exact digital zeros | -inf | **0%** |
| 0.003% (measured on the Linux receiver dump) | -90 dB | **~9.5%** (matches the observed 6~12% floor) |
| 12% (announcement speech RMS) | -18 dB | ~82% (matches observed peaks) |

Material silence floors for the two send scenarios (measured this round):

| Material | Used by | Silent-section min RMS | Meter display |
| --- | --- | --- | --- |
| `8_Channel_ID_signed_16_bit_pcm.wav` (16-bit) | Linux senders (WAV & PCM samples, same content) | 0.0061% (**-84.3 dB, dithered**) | ~6~12% floor |
| `8_Channel_ID.wav` (24-bit) | Web publisher | **exact digital zeros** | **0%** |

→ **The linux→web vs web→web meter difference is determined by the material's silence floor**:
the 16-bit material carries -84 dB dither in its silent sections, which survives encode/transport as
a ~-90 dB (inaudible) residue and is amplified visually by the wide dB-scale meter to ~10%; the
24-bit material has digital zeros, so the meter reads 0. The meter code has no branch and no bug in
the meaningful sense — it faithfully reports a real residue on a dB scale.

Residual detail (archived; does not affect the conclusion): in an aged WAV stream session (~1 h) and
in one fresh PCM session, the decoded silent channels became exact zeros (meter 0), while two fresh
WAV sessions carried the residue — the encoder's suppression of dither-level signals appears to
engage depending on session state/age. The exact trigger needs SDK-internal confirmation; this is a
-90 dB-level detail.

### 3.9 End-to-end confirmation: switching to the 24-bit material (digital-zero silence) removes the floor (2026-10-08)

**On "Audacity shows amplitude 0"**: the silent sections of the 16-bit material are not zero — they
contain ±1~7 LSB of dither (measured CH0 sample distribution in a silent window: 0 for 19%, ±1 for
35%, ±2~3 for 39%, ±4~7 for 7%; 80.8% of samples are non-zero). ±7/32768 ≈ 0.02% of full scale,
which Audacity's default linear waveform view cannot display (use the Waveform (dB) view, or
Effect → Amplify by +60 dB, to see it).

**Confirmation experiment**: on AWS, same WAV sender sample, same channel, a fresh session
(uid 8891), with only the material swapped to `8_Channel_ID_24bit.wav` (whose silent sections are
exact digital zeros; the demo's 24-bit→16-bit conversion is a right-shift by 8 bits, and
`0 >> 8 = 0` keeps zeros):

| Sender session | Material silence floor | Silent-channel meter min (16 s window) |
| --- | --- | --- |
| Fresh WAV sessions (uid 8888, twice) | 16-bit, -84 dB dither | 4.6~12% (floor present) |
| Fresh WAV session (uid 8891) | 24-bit, exact zeros | **0.00% (no floor)** |

→ The only variable was the material's silence floor, and the meter floor appeared/disappeared with
it — **the material-dither causal chain is proven end-to-end**.

## 4. Conclusion

- **The meter has no Linux/Web branch**: `getVolumeLevels()` is a single implementation that reads
  the real per-channel decoded PCM in the Insertable Streams transformer, with a wide dB-scale
  formula `(dBFS_RMS + 100) / 100` (only digital zeros read 0%; a -90 dB residue reads ~10%).
- **The linux→web vs web→web display difference is determined by the material's silence floor**:
  the Linux sender's material (16-bit WAV/PCM) carries -84 dB dither in silent sections, decoded as
  a ~-90 dB (inaudible) residue → reads ~10%; the Web publisher's material (24-bit) has exact digital
  zeros → reads 0%. What you hear ("silent") matches the measurements exactly.
- Ruled out: encode/transport/decode leakage (Linux dump 0.003% RMS), transport loss (Argus 0%),
  frame duration (both streams use 20 ms frames on the wire), sender-sample differences (WAV/PCM
  share the encoder and private parameters), and the playout path (Web Audio tap measured digital
  zero during silent periods).
- Residual detail (-90 dB level; does not affect the conclusion): the dither residue was suppressed
  to exact zeros by the encoder in some sessions (aged WAV stream, one fresh PCM stream) but not in
  two fresh WAV sessions — the suppression trigger needs SDK-internal confirmation.
- Impact on verification work: use `selectChannels` channel selection + listening as the
  authoritative per-channel check; when reading the volume bars, remember the scale is dB — ~10%
  means -90 dB, which is effectively silence.

## 5. Reproduction / Measurement Notes

- Web page debug hook: `window.__mcRemoteTrack` (available after a successful subscription),
  supports `getVolumeLevels()` and `getMediaStreamTrack()` (the track reports `channelCount=8`).
- In-page measurement: see 3.4 for the AudioContext + ChannelSplitter wiring; sampling window
  100 ms × 100 iterations.
- Linux dump analysis: Python, deinterleave 8 channels (`samples[i::8]`) and compute windowed RMS.

## 6. Attachments

| File | Description |
| --- | --- |
| `live_demo_2_8_audio_streaming_audio_volume_ui_linux2web_20261008a.png` | Symptom screenshot (pre-fix Live Demo 2 bare progress bars + silent channels not reading zero) |
| `live_demo_2_volume_ui_fixed_20261008.png` | After the volume-bar UI fix (CH labels + percentages), live render while subscribed to the Linux 8-channel stream |
| `live_demo_2_volume_ui_fixed_20261009.png` | Same as above, 2026-10-09 update (current version) |

## 7. Follow-ups

- [x] Meter mechanism fully understood (no SDK bug to report): `getVolumeLevels()` uses a
      `(dBFS+100)/100` wide dB scale with no Linux/Web branch; the floor originates from the -84 dB
      dither in the 16-bit sender material's silent sections (proven by the 24-bit control
      experiment, see 3.9). Optional improvement: suggest the Web SDK team document the dB-scale
      semantics of the volume values
- [ ] (Optional, only related to the residual detail) Verify the semantics of the private parameters
      `che.audio.bitrate_mode` / `che.audio.dtx_mode` / `che.audio.acm_ptime` (value sets, defaults,
      application timing). Currently only strings-level evidence; the .so files in this repo are all
      stripped — a symbolic-table SDK package or a direct answer from the SDK team is needed.
      Paused 2026-10-08. Note: the main question is closed; this item only relates to the residual
      detail of "when the encoder suppresses the dither residue to zeros" (end of 3.7/3.8)
- [x] FAQ entries added to `Demo/web/README.md` / `README.en.md`
- [x] Live Demo 2 volume-bar UI aligned with Live Demo 1 (CH labels + percentages), deployed
