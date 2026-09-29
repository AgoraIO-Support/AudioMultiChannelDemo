# AudioMultiChannelDemo

Delivery repository for the TVU Networks multi-channel audio (multiopus) project, including:

- A C++ demo for 3–8 channel audio send/receive on the Agora Linux SDK
- A Web-side multi-channel verification demo (OPUSMC, payload types 123/124/125, 48 kHz only)
- The accompanying documentation site and deployment materials

## Directory Guide

| Directory | Contents |
| --- | --- |
| `Spec/` | Requirement specifications (PRD): audio multi-channel for Linux SDK and Web SDK |
| `Demo/linux/` | Linux C++ demo: 3 send paths (raw PCM / 16-bit & 24-bit WAV / pre-encoded Opus) + 3 receive paths (decoded PCM / encoded frames / mixed audio), x86_64 + aarch64 |
| `Demo/web/` | Web verification demo (Vue3 + Vite, agora-rtc-sdk-ng 4.20.1-200): multi-channel publish/subscribe, per-channel playback selection, per-channel volume |
| `Demo/docs/` | Multi-channel integration documentation site (built with the Web app and published to `dist/docs/`) |
| `Docs/` | Deployment and operations docs (AWS test server deployment, frontend cache management) |
| `LinuxSDKFolder/` | Agora Linux SDK v4.4.30.149 (X86_64 / Aarch64, including symbol packages) |
| `References/` | Reference materials (historical samples, design docs, communication records; excluded from the Docker build context) |

## Quick Start

- Linux demo build and run: see `Demo/linux/README.md` ([English](Demo/linux/README.en.md))
- Web demo startup and dual-tab verification: see `Demo/web/README.md` ([English](Demo/web/README.en.md))
- End-to-end verification (Linux sends → Web subscribes and checks each channel): see the two READMEs above and the `Demo/docs/` documentation site

---
