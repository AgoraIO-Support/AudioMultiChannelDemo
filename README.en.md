# Agora Multi-Channel Audio (Multiopus) Demo

[中文](README.md)

Delivery repository for the multi-channel audio (multiopus) project, including:

- A C++ demo for 3–8 channel audio send/receive on the Agora Linux SDK
- A Web-side multi-channel verification demo (OPUSMC, payload types 123/124/125, 48 kHz only)
- A macOS multi-channel receiver verification app (Native SDK, audience-role receive-only, per-channel playback selection and volume, bilingual English/Chinese UI with English as default)
- The accompanying documentation site and deployment materials

## Channel Count Support

- Linux SDK (same as Native): supports 3/4/5/6/7/8-channel send/receive; 3/5/7 require the extra private parameter `che.audio.custom_channel_num` (handled automatically by the demos in this repository).
- Web verification demo: decodes even channel counts (4/6/8) only. A 3/5/7-channel stream sent from Linux cannot be decoded on the Web side (the decoder does not implement the odd-channel Opus multistream configurations), so channel-selection features such as `selectChannels` are unavailable for those streams as well.
- Actual interop verification (Web↔Native, Web↔Linux) covers 4/6/8 channels only; 3/5/7 are untested.
- If 3/5/7-channel content must reach Web clients, pad the stream with one silent channel at the sender to reach an even count (3→4, 5→6, 7→8) and have the Web side ignore the padded channel.
- Per-channel listening verification: use `Demo/web/` on Web (even channel counts only); on Native/macOS use the `Demo/macOS/` receiver app (per-channel mute/volume via the private parameter `che.audio.neteq.adjust_decoded_volume`; Native decodes adaptively, including 3/5/7).

## Directory Guide

| Directory | Contents |
| --- | --- |
| `Spec/` | Requirement specifications (PRD): audio multi-channel for Linux SDK, Web SDK, and Native SDK (macOS) |
| `Demo/linux/` | Linux C++ demo: 3 send paths (raw PCM / 16-bit & 24-bit WAV / pre-encoded Opus) + 3 receive paths (decoded PCM / encoded frames / mixed audio), x86_64 + aarch64 |
| `Demo/web/` | Web verification demo (Vue3 + Vite, agora-rtc-sdk-ng 4.20.1-200): multi-channel publish/subscribe, per-channel playback selection, per-channel volume |
| `Demo/macOS/` | macOS receiver verification app (SwiftUI + AgoraRtcEngine_macOS 4.5.2, CocoaPods): audience-role receive-only for multi-channel/stereo streams, per-channel playback selection and volume, audio frame dump |
| `Demo/docs/` | Multi-channel integration documentation site (built with the Web app and published to `dist/docs/`) |
| `Docs/` | Deployment and operations docs (AWS test server deployment, frontend cache management) |
| `LinuxSDKFolder/` | Agora Linux SDK v4.4.30.149 (X86_64 / Aarch64, including symbol packages) |
| `References/` | Reference materials (historical samples, design docs, communication records; excluded from the Docker build context) |

## Quick Start

- Linux demo build and run: see `Demo/linux/README.md` ([English](Demo/linux/README.en.md))
- Web demo startup and dual-tab verification: see `Demo/web/README.md` ([English](Demo/web/README.en.md))
- macOS receiver app build and per-channel listening: see `Demo/macOS/README.md` ([English](Demo/macOS/README.en.md))
- End-to-end verification (Linux sends → Web subscribes and checks each channel): see the two READMEs above and the `Demo/docs/` documentation site

---
