# quick-demo-multiopus — Usage Guide

This demo shows how to publish and subscribe to **multi-channel audio** (up to 8 channels, based on Multi-Opus encoding) with the **Agora RTC Web SDK (agora-rtc-sdk-ng)**, and how the subscriber side can pick which channels to play (mono / stereo / surround).

## 1. What the demo does

- **Publisher side**: fetches a multi-channel `.wav` file → decodes and loops it with the Web Audio API → captures it as a custom audio track with `channelCount = 8` → publishes it to an Agora channel.
- **Subscriber side**: subscribes to the remote audio track → calls `selectChannels()` to choose which channels to hear and the mix mode → uses `getVolumeLevels()` to render a live per-channel volume bar.

Key point: only after enabling `ENABLE_MULTIOPUS_CODEC` will Agora transmit audio as multi-channel (instead of the default mono).

## 2. Tech stack

| Item | Version / Notes |
|------|------|
| Framework | Vue 3 (`<script setup>` + TS) |
| Build tool | Vite 5 |
| UI library | Naive UI (`NButton` / `NInput` / `NNotificationProvider`) |
| RTC SDK | `agora-rtc-sdk-ng@4.20.1-200` |
| Styling | Less |

## 3. Running the demo

> This demo lives under `projects/quick-demo-multiopus` inside the `agora-rtc-web` monorepo and its dependencies are managed by a pnpm workspace. From the **repository root**:

```bash
pnpm i
pnpm -F=./projects/quick-demo-multiopus/ run dev
```

If `pnpm` is not on your PATH, you can launch the already-installed local Vite directly from this folder:

```bash
./node_modules/.bin/vite
```

The dev server runs at **http://localhost:5173/**.

Other scripts (in this folder's `package.json`):

| Command | Purpose |
|---------|---------|
| `pnpm run dev` | Start the Vite dev server |
| `pnpm run build` | `vue-tsc` type check + Vite build into `dist/` |
| `pnpm run preview` | Preview the production build |

## 4. UI layout

The page (`src/Multiopus.vue`) is divided, top to bottom, into:

1. **Parameter row 1**
   - `Current channel`: the Agora channel name (saved to localStorage and pre-filled next time).
   - `Multi-channel audio URL`: the `.wav` to publish; defaults to `/8_Channel_ID.wav` on the same origin (see `public/`).
   - `Audio channel count`: defaults to `8`, mapped to the `channelCount` of `createCustomAudioTrack`.
2. **Parameter row 2 (identity config)** — *new*
   - `Token`: the Agora token used by **this tab** to join. Because a token is bound to a uid, each tab must hold the token of its own uid.
   - `Publisher UID`: the uid used by the publisher side.
   - `Subscriber UID`: the uid used by the subscriber side.
   - These three fields are persisted to localStorage so both tabs share them.
3. **Role links** — *new*
   - `Open Publisher (uid: …)` and `Open Subscriber (uid: …)`: two links built from the fields above. Clicking opens the matching role in a new tab. See section 5.
4. **Channel-select row**
   - Enter the channel indices to play, comma-separated (e.g. `0,1,2,3,4,5`).
   - The three buttons call `remoteAudioTrack.selectChannels()` with mode `surround` / `stereo` / `mono`. **Subscribe first, then click these.**
5. **Operation row**: Join / Leave / Publish / Unpublish / Subscribe / Unsubscribe.
6. **Volume bars**: refreshed every 300ms after subscribing, showing each channel's live volume.

### Typical click order
**Publisher tab**: Join channel → Publish audio.
**Subscriber tab**: Join channel → Subscribe audio → choose channels & mode.

## 5. Two-tab local verification (key workflow)

To verify multi-channel audio you run two tabs in the same channel — one publisher, one subscriber — without crossing wires with other real users in the channel.

The current tab's **identity (`uid`)** and **subscription target (`subTo`)** are taken from the URL. The two role links generate those URLs for you:

| URL param | Meaning |
|-----------|---------|
| `channel` | Channel name (takes priority over localStorage and the default). |
| `uid` | The uid this tab joins as. |
| `subTo` | The remote uid to subscribe to (only on the subscriber link), so you subscribe to this demo's publisher rather than someone else. |

The generated links look like:

- **Publisher**: `http://localhost:5173/?channel=<channel>&uid=<publisherUID>`
- **Subscriber**: `http://localhost:5173/?channel=<channel>&uid=<subscriberUID>&subTo=<publisherUID>`

> **Token note:** the links intentionally do **not** carry a token, because an Agora token is bound to a specific uid and the two tabs need different tokens. After opening each tab, paste that tab's matching token into its `Token` field. (If you generated a wildcard token with `uid = 0`, the same token works for both — paste it into each tab.)

### Step by step
1. In the first tab, fill in `Current channel`, `Publisher UID`, `Subscriber UID`.
2. Click **Open Publisher** → in the new tab paste the publisher uid's token into `Token` → Join channel → Publish audio.
3. Back in the first tab click **Open Subscriber** → in the new tab paste the subscriber uid's token into `Token` → Join channel → Subscribe audio → choose channels.

## 6. Core code walkthrough (`src/Multiopus.vue`)

- **Enable multi-channel encoding** (must be set before creating the client):
  ```ts
  AgoraRTC.setParameter("ENABLE_MULTIOPUS_CODEC", true);
  ```
- **Identity & links**
  - `UID` / `SUB_TO` are read from the URL (`uid` / `subTo`) and decide the tab's role.
  - `token` / `publisherUid` / `subscriberUid` are reactive inputs, persisted to localStorage via `watch`.
  - `publisherLink` / `subscriberLink` are `computed` from the channel + uids.
  - `joinChannel()` joins with `token.value` and the tab's uid (falling back to `publisherUid` when the URL has no `uid`).
- **Publisher: turn a wav into a multi-channel track**
  - `createAudioBuffer()`: `fetch` the file → `AudioContext.decodeAudioData()`.
  - `getAudioBufferTrack()`: use `createMediaStreamDestination()` with explicit `channelCount`, loop playback (re-add on `onended`), and output one `MediaStreamTrack`.
  - `getAudioTrack()`: `AgoraRTC.createCustomAudioTrack({ mediaStreamTrack, channelCount })`.
- **Subscriber: select channels**
  ```ts
  remoteAudioTrack.selectChannels([0,1,2,3,4,5], "surround" | "stereo" | "mono");
  ```
- **Volume visualization**: `remoteAudioTrack.getVolumeLevels()` returns a per-channel volume array, rendered as progress bars.

## 7. Configuration

| To change… | Edit… |
|------------|-------|
| Agora App ID | `APP_ID` in `Multiopus.vue` |
| Token / publisher uid / subscriber uid | The `Token` / `Publisher UID` / `Subscriber UID` inputs in the UI (persisted to localStorage) |
| Default audio file / channel count | `audioURL` / `channelCount` defaults; sample 4/6/8-channel wavs are in `public/` |
| Default channel | The `channel` default value |

> ⚠️ `APP_ID` is a plaintext demo credential and tokens are entered manually for local verification only. In production, fetch tokens dynamically from your own server.

## 8. Directory structure

```
quick-demo-multiopus/
├─ index.html              # Entry HTML
├─ src/
│  ├─ main.ts              # createApp + mount
│  ├─ App.vue              # Wraps the page in NNotificationProvider
│  ├─ Multiopus.vue        # All logic and UI
│  └─ style.css
├─ public/                 # 4/6/8_Channel_ID.wav samples
├─ vite.config.ts
└─ package.json
```
