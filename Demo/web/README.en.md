# Web Multi-Channel Audio (multiopus) Demo

A multi-channel audio send/receive verification app based on the Agora Web SDK
(`agora-rtc-sdk-ng@4.20.1-200`, the multi-channel custom build). Tech stack: Vue3 + Vite + Naive UI.
See `Spec/web_sdk_audio_multichannel_spec.md` for the requirements background.

[中文文档](README.md)

## Features

- **Multi-channel publish (Publisher)**: pick a built-in multi-channel WAV file (4/6/8-channel
  Channel ID test tones). The file goes through `fetch` → `AudioContext.decodeAudioData` → a
  `MediaStreamDestination` with explicit `channelCount` →
  `AgoraRTC.createCustomAudioTrack({ mediaStreamTrack, channelCount })`, and is published as an
  OPUSMC multi-channel stream. The file plays in a loop.
- **Multi-channel subscribe (Subscriber)**: enter the channel, your uid (+token), and the target
  uid (subTo) to subscribe to the remote audio and play it.
- **Per-channel volume bars**: after subscribing, `getVolumeLevels()` is called every 300 ms to
  render live volume for each channel.
- **Codec detection**: hooks `RTCPeerConnection.getStats()` to read the inbound codec
  (`opus` / `multiopus`), polling every 800 ms, up to 6 times.
- **Channel selection playback**: `selectChannels(channels, mode)` plays any combination of
  channels from the remote track in `surround` / `stereo` / `mono` mode — used to listen to each
  channel of the Channel ID files one by one.
- **Two-tab role control**: URL parameters `channel` / `uid` / `subTo` separate the publisher tab
  from the subscriber tab.
- **Two UI layouts**: the default (`?demo=1`) is this project's layout; `?demo=2` reproduces the
  original quick-demo-multiopus layout (free-text audio URL, OK (surround/stereo/mono) buttons,
  plain volume bars). Quick links in the header switch between the two demos.
- **Chinese/English toggle**: a button in the top-right corner switches the UI language
  (defaults to the browser language; the choice is remembered in localStorage).

Key prerequisite: `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)` is set at the top of
`src/Multiopus.vue`, before `createClient` (see the code comment; setting it after the client is
created has no effect).

## Requirements

- **Browser: Chrome 96+, 110+ recommended.** Channel selection relies on Insertable Streams
  (a workaround for Chrome WebAudio bug 1116042).
- **Safari / Firefox are not supported**: joining a multi-channel channel has undefined behavior
  (possibly silence or stereo only).
- **Even channel counts only**: the Web side decodes 4/6/8 channels (3/5/7 do not apply; 16 or
  more channels are not supported).
- Node.js 18+ (for local development).

## Install and Run

```bash
cd Demo/web
npm install
npm run dev   # default http://localhost:5173/
```

Other scripts: `npm run build` (vue-tsc type check + build to `dist/`), `npm run preview`.

> `agora-rtc-sdk-ng@4.20.1-200` is published on the public npm registry and installs directly.
> If the download fails, ask Agora for the offline package of this custom build, then place
> `AgoraRTC_N-production.js` and `rtc-sdk_en.d.ts` in a local directory and reference it from
> `package.json` as `"agora-rtc-sdk-ng": "file:./vendor/agora-rtc-sdk-ng"`.

## Docker

```bash
# Run from the Demo/ directory (the build context must include both web/ and docs/;
# docs is copied into dist/docs during the build)
cd Demo
docker build -f web/Dockerfile -t agora-multichannel-web .
docker run --rm -p 8080:80 agora-multichannel-web
# Open http://localhost:8080/
```

Multi-stage build: `npm ci` + `npm run build` inside `node:20-alpine`, then the output is
served by nginx (`nginx.conf` sets up SPA fallback and gzip for static assets). App ID, token,
and uid are all entered in the page — no build-time configuration is needed. Note: browsers
may restrict media capabilities on non-localhost HTTP origins; use HTTPS in production.

## Two-Tab Self Test (Publish + Subscribe)

Agora tokens are bound to uids: each tab needs a token for its own uid (or generate a wildcard
uid=0 token in the Agora Console and share it between both tabs). For projects without an App
Certificate, the token can be left empty.

1. Open the page and fill in "Channel", "Local UID", and "Target UID (subTo)". The two links at
   the bottom of the page build role URLs from the values you entered:
   - **Open Publisher**: `?channel=<channel>&uid=<localUID>`
   - **Open Subscriber**: `?channel=<channel>&uid=<targetUID>&subTo=<localUID>`
2. Click "Open Publisher" → in the new tab, paste the token for the publisher uid → "Join
   Channel" → pick a file (e.g. `8_Channel_ID.wav`; the channel count automatically follows to
   8) → "Publish Audio".
3. Back on the first page, click "Open Subscriber" → in the new tab, paste the token for the
   subscriber uid → "Join Channel" → "Subscribe & Detect".
4. The subscriber result panel shows per-channel volume bars and the codec verdict. In the
   "Channel Selection" input, enter channel indexes (e.g. `0,1`) and click surround / stereo /
   mono to listen.

URL parameters (each one overrides the localStorage value):

| Parameter | Meaning |
| --- | --- |
| `channel` | Channel name |
| `uid` | The uid this tab joins with (decides the role) |
| `subTo` | Target uid to subscribe to (subscriber tab) |
| `token` | Token for this tab's uid (can also be entered in the page) |

## End-to-End Verification with the Linux Demo

1. On the Linux side (see `Demo/linux/` and `Spec/linux_sdk_audio_multichannel_spec.md`),
   publish 8-channel OPUSMC audio into the channel and note its uid.
2. Open this demo as a subscriber: `?channel=<same channel>&uid=<web uid>&subTo=<linux uid>`,
   enter the token, join the channel, and run "Subscribe & Detect".
3. Expected result: **8 volume bars**, codec detected as **multiopus**, and the remote track's
   channelCount (from signaling) shows 8.
4. Use "Channel Selection" to listen channel by channel (`0`, `1`, … `7`) and confirm the
   Channel ID content matches the Linux-side layout mapping (T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R)
   with no crosstalk and no noise.

Note: every endpoint in the channel should support multi-channel. If a non-supporting endpoint
joins while Linux is sending, the sender falls back to plain Opus (the Web side sees the codec
change to opus and the channel count drop). When the Web side is sending, there is no fallback
and the non-supporting endpoint hears nothing.

## How to Judge the Result

| Check | Multi-channel active | Not active |
| --- | --- | --- |
| Length of the `getVolumeLevels()` array | = actual channel count (4/6/8) | 1 (mono) |
| getStats inbound codec | `multiopus` | `opus` |
| Remote track `channelCount` (signaling pt value) | 4/6/8 | — |

The demo result panel reports multi-channel when "the volume array length is 4/6/8, or the codec is multiopus". Plain mono/stereo is never reported as multi-channel.

## Channel Selection Modes

`remoteAudioTrack.selectChannels(channels, mode)`, where `channels` is an array of channel
indexes (starting from 0):

- `surround` (default): outputs the selected channels following the standard multi-channel
  protocol.
- `stereo`: within the selected channels, 0,2,4,6… go to the left channel and 1,3,5,7… go to
  the right, producing stereo output. For example, with `0,1,2,3` selected, 0 and 2 are on the
  left and 1 and 3 are on the right.
- `mono`: all selected channels are mixed down to mono (delivered in a stereo container).

To listen to one channel: enter a single index (e.g. `3`) and choose mono, or enter a pair
(e.g. `2,3`) and choose stereo.

## FAQ

- **Subscribed to a multi-channel stream but only hear mono/stereo?** Check that
  `AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true)` is set before `createClient`. Without
  it, the stream is handled as plain audio and only mono/stereo can be decoded.
- **Token error / join failure?** Tokens are bound to uids — each tab must use the token for its
  own uid (or a uid=0 wildcard token). The `APP_ID` in the demo is a plain-text demo credential;
  replace it for real use and issue tokens from your own server.
- **Are the files 44.1 kHz?** The 4/6-channel files are 44.1 kHz/16-bit, and the 8-channel file
  is 48 kHz/24-bit. All are decoded by the Web Audio graph and encoded at the negotiated
  48 kHz (multi-channel codec supports 48 kHz only), so this does not affect verification.
- **Unsupported browser?** Safari/Firefox do not support multi-channel (undefined behavior). On
  Chrome 94/95 the related interfaces are off by default; Chrome 96+ officially supports them,
  110+ recommended.
- **A non-multi-channel endpoint joins the channel?** When Linux is sending, the sender falls
  back and multi-channel stops working (the Linux side can set `che.audio.codec_unfallback` to
  force no fallback). When the Web side is sending, there is no fallback and that endpoint hears
  nothing.
- **Maximum channel count?** 8 channels; even counts only; 16 or more are not supported.

## Directory Layout

```
Demo/web/
├─ index.html
├─ package.json            # agora-rtc-sdk-ng pinned to 4.20.1-200
├─ vite.config.ts
├─ tsconfig.json / tsconfig.app.json / tsconfig.node.json
├─ public/                 # 4/6/8_Channel_ID.wav assets
└─ src/
   ├─ main.ts              # createApp + mount
   ├─ App.vue              # NNotificationProvider wrapper, switches layouts via ?demo= query
   ├─ Multiopus.vue        # Live Demo 1 (default): all logic and UI (publish/subscribe/channel selection/codec detection)
   ├─ MultiopusV2.vue      # Live Demo 2 (?demo=2): reproduces the original quick-demo-multiopus layout
   ├─ style.css
   └─ vite-env.d.ts
```
