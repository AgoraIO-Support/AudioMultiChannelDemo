# Communication Records Backup: Customer Multi-Channel Audio Project (2025-04)

> Purpose: the `References/沟通记录/` directory is planned for deletion; this document archives
> the content of all its materials in text form. Compiled: 2026-10-09. The original binary files
> (screenshots / xlsx / mkv) are not embedded; their contents are transcribed below.
> **Anonymization note**: per this repo's pre-open-sourcing convention, the customer name is
> removed (the customer-side engineer is referred to by the first name Abhi); prefixes carrying
> customer identifiers in the original filenames are referred to here with the generic
> `customer_` prefix.
> 中文版：`communication_records_backup.md`

---

## 1. Background and conclusion at a glance

- **Customer**: a broadcast customer; Agora side: Alan (CSE), lqk (SDK R&D).
- **Scenario**: the customer's Linux server sends broadcast material carried as "4 stereo tracks
  holding 8 channels" over Agora RTC (multiopus/OPUSMC); a Web client subscribes for verification.
- **Symptom**: the Web subscriber heard garbled audio / scrambled content.
- **Root cause**: the sender-side audio layout — the source data's channel arrangement did not
  match the **sample-interleaved 8-channel layout** that the SDK's `sendAudioPcmData` expects.
- **Resolution**: on 2025-04-14 the customer confirmed "it was indeed the sender-side audio
  layout"; fixed by reordering to the mapping agreed on the call.

**Final confirmed mapping** (agreed on the call; see 3.2): 8 channels interleaved frame by frame;
frame n is `[T1Ln, T1Rn, T2Ln, T2Rn, T3Ln, T3Rn, T4Ln, T4Rn]`, i.e.
CH1=T1L, CH2=T1R, CH3=T2L, CH4=T2R, CH5=T3L, CH6=T3R, CH7=T4L, CH8=T4R.
(This is the standard layout documented in `Demo/docs/linux-sdk-multichannel.html` §1.5.)

---

## 2. Material inventory and content archive

Original directory: `References/沟通记录/` (~128 MB).

| File | Type / size | Content summary |
| --- | --- | --- |
| `沟通记录2025.txt` | text, 4.2 KB | WeCom chat log 2025-04-07 ~ 04-14 (full translation in Appendix A) |
| `audio_mutil_channel_mapping.txt` | text, 240 B | Mapping-confirmation conversation (full text in 3.2) |
| `abhi_audio_order_mapping.jpg` | image, 45 KB | Mapping table provided by Abhi, the customer's engineer (the version discussed on the call); see 3.2 |
| `alan_audio_order_mapping.png` | image, 30 KB | Alan's channel-frame index table after sync-up; see 3.2 |
| `customer_order.xlsx` | Excel, 13 KB | The customer's current send order (two candidate tables); see 3.3 |
| `Agora_Order_Expectation.xlsx` | Excel, 12 KB | Agora's expected order; see 3.4 (includes an ambiguity note) |
| `企业微信截图_ccca…png` | screenshot, 309 KB | Chat context of Abhi (the customer's engineer) sending the two xlsx files; see 3.5 |
| `企业微信截图_31d7…png` | screenshot, 365 KB | ffprobe output of VictoriaMulti.mkv; see 3.5 |
| `VictoriaMulti.mkv` | video, 133 MB | The customer's test source file; metadata in 3.6 |
| `~$ltipleAudioChannel.docx` | 162 B | Office lock file, no content value, not archived |

---

## 3. Detailed content of each artifact

### 3.1 沟通记录2025.txt (full chat log)

WeCom chat from 2025-04-07 to 04-14; a full English translation is in **Appendix A**. Timeline
of key points:

- **04-07**: Alan first asked the customer to provide their Linux integration code; explained
  that the customer's 8 channels are **not 7.1 surround** but four L/R pairs — (0,1)(2,3)(4,5)(6,7);
  shared a test channel (AppId `aab8b8f5a8cd4469a63042fcfafe7063`, channel `142651995438`,
  no token) so lqk could subscribe and listen, and tried dumping as well.
- **04-08 morning**: Alan sent `AGORA_sample.txt` / `output.txt` / a screenshot; lqk
  repeatedly asked for the actual audio to identify the noise type (completely garbled noise vs
  audible speech); lqk noticed the customer's code assumes "every track has strictly identical
  length and strictly 2 channels", yet elsewhere in the code a track appeared to have a single
  channel — contradictory; the customer provided an iOS-app subscription recording
  `far_in_48000hz_2ch.wav`.
- **04-08 11:56 (key insight)**: lqk proposed — **the source multi-channel data is stored
  interleaved (LRLRLR…), but the customer read it as planar blocks (LLLL…RRRR…)**.
- **04-08 afternoon**: Alan sent the customer's test source file `VictoriaMulti.mkv`; lqk noted
  the server side would not read the mkv directly — it must be converted to PCM first (the
  customer relays it via shared memory); he confirmed all 4 tracks are stereo, that slight
  duration differences across tracks do not matter much, and that the interleaving issue should
  be the focus.
- **04-09**: the customer's engineer provided the audio layout (`customer_order.xlsx` + an
  image); Alan replied with `Agora_Order_Expectation.xlsx`.
- **04-14**: Alan confirmed: **problem solved — it was indeed the sender-side audio layout**.

### 3.2 Channel-mapping confirmation (audio_mutil_channel_mapping.txt + two mapping images)

Full text of `audio_mutil_channel_mapping.txt`:

```text
@Alan This is the mapping that we discussed in Call. Please verify again
[abhi_audio_order_mapping.jpg]

Synced up with @Abhi, the audio layout is like below
[alan_audio_order_mapping.png]

Confirmed that it should be correct.
```

**abhi_audio_order_mapping.jpg** (provided by Abhi; the mapping discussed on the call — a two-row table):

| CH1_1 | CH2_1 | CH3_1 | CH4_1 | CH5_1 | CH6_1 | CH7_1 | CH8_1 | CH1_2 | CH2_2 | … |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T1L1 | T1R1 | T2L1 | T2R1 | T3L1 | T3R1 | T4L1 | T4R1 | T1L2 | T1R2 | … |

Reading: `CHn_m` = channel n of frame m; `TkL/Rm` = sample m of track k's left/right channel.
That is **frame-interleaved**: frame 1 = [T1L1, T1R1, T2L1, T2R1, T3L1, T3R1, T4L1, T4R1],
frame 2 likewise, etc.

**alan_audio_order_mapping.png**: a vertical index list CH1_1…CH8_1, CH1_2…CH8_2, CH1_3,
CH2_3… (a channel-frame index listing companion to the table above).

### 3.3 customer_order.xlsx (the customer's current order)

Parameter block (identical in both xlsx files):

| Item | Value |
| --- | --- |
| Tracks × channels | 4 tracks, 2 channels each (L, R) |
| Sample size (L+R) | 32 bit (L 16-bit + R 16-bit) |
| Data sent to SDK per write (all tracks) | 15360 bytes |
| Per-track data per write (L&R) | 15360 / 4 = 3840 bytes |
| Per-track samples per write (L&R) | 3840 / 32 = 120 |

Table 1 (titled as the customer's current order — track-block order: one block holds a track's
120 samples with L/R interleaved inside the track, then the next track):

```text
T1L1 T1R1 T1L2 T1R2 … T1L120 T1R120 | T2L1 T2R1 … T2L120 T2R120 | T3… | T4L1 … T4L120 T4R120
```

Table 2 (a second table in the same file — fully planar: all tracks' L series first, then all
R series):

```text
T1L1 T1L2 … T1L120 T2L1 … T2L120 T3L1 … T4L120 | T1R1 T1R2 … T1R120 T2R1 … T4R120
```

### 3.4 Agora_Order_Expectation.xlsx (Agora's expected order)

Same parameter block as 3.3, followed by the legend (T1L1 = Track 1 Left sample 1, 16 bit;
an L+R pair is 32 bit) and the "Agora Expected Order" table, whose ORDER row reads, cell by cell:

```text
T1L1 T1L2 T1L3 T1L4 … T1L120 | T2L1 … T2L120 | T3L1 … T3L120 | T4L1 … T4L120 | T1R1 T1R2 … T1R120 | … | T4R1 … T4R120
```

> **Archival note (ambiguity)**: read literally, this row presents a planar "all L, then all R"
> order, which does not match the frame-interleaved mapping confirmed after the call (3.2). The
> sheet was drawn quickly during the conversation and is illustrative; **the binding agreement is
> the mapping image in 3.2 plus the 2025-04-14 resolution confirmation**; the sample-interleaved
> layout documented in `Demo/docs/linux-sdk-multichannel.html` §1.5 is that final agreement.

### 3.5 Two WeCom screenshots

- **Screenshot_ccca…png**: Abhi (the customer's engineer) sent `customer_order.xlsx` ("this is the
  current Order we are sending to Agora (4 tracks where each track has 2 channels L & R)")
  followed by `Agora_Order_Expectation.xlsx` ("can you confirm with the BE engineer that he
  expects this order from us?"); the in-chat previews match the contents in 3.3/3.4.
- **Screenshot_31d7…png**: ffprobe output of `VictoriaMulti.mkv` — one video stream (mpeg4,
  1920x1080) plus **four audio streams (all AC-3, 44100 Hz, stereo, 192 kb/s)** with slightly
  different durations (~4:00.571 to 4:00.606). This corroborates the 04-08 observation ("all
  tracks are stereo, but their durations differ slightly").

### 3.6 VictoriaMulti.mkv (133 MB test source video)

- Role: the customer's test source file; the server does not read the mkv directly — it is
  decoded and relayed as PCM via shared memory.
- Metadata (re-verified locally with ffprobe on 2026-10-09, matching the screenshot in 3.5):

| Stream | Type | Parameters |
| --- | --- | --- |
| #0 | Video | MPEG-4 (Simple Profile), 1920x1080, yuv420p |
| #1–#4 | Audio ×4 | AC-3, 44100 Hz, stereo, 192 kb/s (4 stereo tracks carrying 8 channels) |

- Container duration ~243.7 s (4:03.7), overall bitrate ~4367 kb/s.
- Relation to the current docs: this material's "4×AC-3 stereo → 8 channels" layout is exactly
  the reordering scenario in `Demo/docs/linux-sdk-multichannel.html` §1.5; a 44.1 kHz source must
  be resampled to 48 kHz first, and each 15360-byte write must be split into 10 ms frames before
  `sendAudioPcmData`.

---

## 4. Mapping to current documentation

| Communication-record content | Current location |
| --- | --- |
| Reordering 4×stereo block order → 8-channel sample-interleaved | `Demo/docs/linux-sdk-multichannel.html` §1.5; `Spec/linux_sdk_audio_multichannel_spec.md` |
| Final confirmed mapping CH1=T1L…CH8=T4R | same as above |
| Resampling 44.1 kHz sources to 48 kHz; splitting large writes into 10 ms frames | same section, notes |

---

## Appendix A: Full English translation of 沟通记录2025.txt

(The customer filename in the original text has been replaced with `customer_order.xlsx` per the
anonymization convention; everything else is translated faithfully.)

```text
Alan 2025-4-7 15:16
I asked the customer's engineers to provide their Linux integration code first.

Alan 2025-4-7 15:16
Because the customer's integration code references the code we gave them.

Alan 2025-4-7 15:17
The customer's 8 channels are not 7.1 — it's (0,1) as one L/R pair, (2,3), (4,5), (6,7), like that.

lqk 2025-4-7 15:18
I need to listen to it — please provide the audio when you can.

Alan 2025-4-7 15:20
@lqk When convenient, just subscribe to this channel: AppId: aab8b8f5a8cd4469a63042fcfafe7063 Channel Name: 142651995438, no token needed.

Alan 2025-4-7 15:22
(re: "need to listen") ------ @lqk I'll also try dumping now.

Alan 2025-4-8 10:22
[AGORA_sample.txt]

Alan 2025-4-8 10:22
[output.txt]

Alan 2025-4-8 10:23
[image]

lqk 2025-4-8 10:48
As I've said several times above, I hope to get the actual audio to see what the noise really is.
Is it completely unintelligible, totally scrambled noise, or can you still hear someone talking?

Alan 2025-4-8 10:56
(re: the above) ------ @lqk A recording can be provided.

Alan 2025-4-8 10:57
Looks unrelated to the Web SDK API; let me figure out how to locate the problem point.

lqk 2025-4-8 10:58
[image] Is the length of each of the user's tracks strictly identical, and is each track strictly
2-channel? Their code seems built on these assumptions.

lqk 2025-4-8 10:59
But further down in their code it looks like a track may have one channel — so those two
assumptions may contradict each other.

Alan 2025-4-8 10:59
Looks like it — I'll ask the customer's engineers to clarify.

Alan 2025-4-8 11:00
Best if they give us a demo that reproduces it every time.

lqk 2025-4-8 11:00
Bring their original audio and let's listen.

Alan 2025-4-8 11:00
OK.

Alan 2025-4-8 11:49
This is the effect of the iOS app subscription.

Alan 2025-4-8 11:49
[far_in_48000hz_2ch.wav]

lqk 2025-4-8 11:56
It sounds like the source multi-channel data may be stored interleaved LRLRLRLR…, but they read
it as LLLLLLLL RRRRRRRR.

Alan 2025-4-8 12:02
OK, I'll get on a voice call with their engineers this afternoon to align.

Alan 2025-4-8 14:11
@lqk [image]

Alan 2025-4-8 14:11
I'll have the customer test again.

Alan 2025-4-8 14:11
[VictoriaMulti.mkv]

Alan 2025-4-8 14:12
(re: VictoriaMulti.mkv) ------ @lqk This is the test source audio file.

lqk 2025-4-8 14:16
(re: VictoriaMulti.mkv) ------ @Alan They don't read it directly, right? They convert to PCM first?

Alan 2025-4-8 14:18
There should be an intermediate step — per the customer's engineers, it's shared memory.

Alan 2025-4-8 14:18
I'll have them re-confirm the audio channel layout.

lqk 2025-4-8 14:18
All tracks are stereo, but their durations differ slightly — that probably doesn't matter much;
focus on the interleaving issue: stored interleaved LRLRLR… but read as LLLLLLLL RRRRRRRR. [image]

Alan 2025-4-8 14:19
(re: the above) ------ @lqk It's probably exactly what you said.

lqk 2025-4-8 14:19
(re: "shared memory") ------ @Alan What I mean is, what you sent isn't what he reads on the
server — he surely doesn't read the .mkv directly on the server.

Alan 2025-4-8 14:19
Let's wait for the customer engineers to re-verify.

Alan 2025-4-8 14:19
Yes, understood. Though this time it's purely their test code.

Alan 2025-4-9 13:33
[customer_order.xlsx]

Alan 2025-4-9 13:34
[image]

Alan 2025-4-9 13:35
@lqk This is the audio layout from the customer's engineers.

Alan 2025-4-9 13:37
[Agora_Order_Expectation.xlsx]

Alan 2025-4-14 13:37
@lqk Problem solved — thanks for the support! It was indeed the sender-side audio layout.

Alan 2025-4-14 13:38
[image]
```
