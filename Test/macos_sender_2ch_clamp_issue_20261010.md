# macOS SDK 多声道发送仅前 2 声道有声——问题记录与跟踪

> 日期：2026-10-10
> 状态：**已闭环（2026-10-12）**——发送侧 2 声道钳制为 4.5.2 独有，修复点 4.5.2→4.5.3；
> 4.5.3/4.6.0/4.6.2/4.7.0 实测 8 声道全通。汇总见
> `Test/macos_sender_multi-channel_audio_test_result.md`；本文以下为 2026-10-10 的历史排查记录。
> 怀疑方向（未证实）：Linux 定制版的多声道发送相关代码未同步合并进 macOS SDK 对应分支
> 相关文档：`Spec/native_sdk_audio_multichannel_spec.md` 需求点7 场景7.2、待确认问题1；
> `Demo/macOS/MultiChannelAudioSender/README.md`「发送侧多声道能力：版本实测矩阵」章节

## 1. 问题现象

使用 macOS 发送端 app（`Demo/macOS/MultiChannelAudioSender`，AgoraRtcEngine_macOS **4.5.2**，
CocoaPods 公开版）以自定义音轨发布多声道 WAV（4/6/8 声道）时：

- **协商正常**：私参 `che.audio.custom_payload_type` / `custom_channel_num` 生效，
  订阅端（Web / macOS receiver）均解出**对应声道数**的 multiopus 流；
- **内容缺失**：只有**前 2 声道**携带真实内容，**第 3 声道起为数字静音**。
  接收端能"听到声音"（前 2 声道），但多声道内容不完整。

## 2. 测试环境

| 项 | 值 |
| --- | --- |
| 发送端 | macOS app（SwiftUI），`createCustomAudioTrack(.mixable)` + `pushExternalAudioFrameRawData`，10 ms/帧循环推送 |
| 发送素材 | 内置 Channel ID WAV：`8_Channel_ID_signed_16_bit_pcm.wav` / `6_Channel_ID_48k.wav` / `4_Channel_ID_48k.wav`（48 kHz / 16bit，各声道内容可区分） |
| SDK | AgoraRtcEngine_macOS 4.5.2（CocoaPods 公开版） |
| 私参 | join 前设置 `che.audio.custom_payload_type`（123/124/125）、`custom_channel_num`（4/6/8）、`custom_sample_rate`（48000） |
| 验证端 | ① Web Live Demo 1（localhost:5199，`getVolumeLevels` 逐声道 + getStats codec）；② macOS receiver app（逐声道 dump + 码率） |
| 频道 | `macsend1010a`（App ID `43f818097c124394821bd44598b7ca9d`，无证书） |

## 3. 实测证据

### 3.1 协商（全部正常）

| 素材 | Web 端判定 |
| --- | --- |
| 8ch（payload 125） | `codec: multiopus (multi-channel codec, 8 ch)`，channelCount=8 |
| 6ch（payload 124） | `codec: multiopus, 6 ch` |
| 4ch（payload 123） | `codec: multiopus, 4 ch` |

### 3.2 内容（仅前 2 声道）

- Web 端 6 秒连续采样（8ch 流）：CH0/CH1 随内容脉冲（18%~72%），**CH2~CH7 恒 0%**；
- macOS receiver 的 per-user dump（`audio_dump_playback_before_mixed_0_*_<uid>.wav`，48 kHz/8ch）：
  ch1/2 RMS ≈ -47 dB 且与源 WAV ch1 内容相关（互相关 0.45，Opus 有损 + 循环偏移），
  ch3~8 RMS ≈ -96 dB（数字静音）；
- 听感：Web 与 receiver 均可听到前 2 声道内容，其余无声。

## 4. 排查过程（排除项清单）

1. **demo 自身 bug（已修）**：发送 pacer 循环漏了 `offset += bytesPerFrame`，导致一直重发
   首 10ms（静音段）。修复后前 2 声道内容出现，但 ch3+ 仍静音——问题转入 SDK 层。
2. **`samples` 参数语义**：macOS `pushExternalAudioFrameRawData` 的 `samples` 是
   **全部声道总样本数**（官方 API-Examples `CustomAudioSource` 同样用法，
   `samples = data.length / sizeof(int16_t)`），与 Linux `sendAudioPcmData` 的
   **每声道**样本数语义不同。已按总样本数推送（480×8=3840）。
3. **推入方式**：`pushExternalAudioFrameRawData`（raw PCM）与
   `pushExternalAudioFrameSampleBuffer`（CMSampleBuffer，ASBD 自描述 8 声道交错 s16le）
   结论一致：仅前 2 声道有内容。
4. **音轨类型**：`createCustomAudioTrack(.mixable)` 与 `.direct` 结论一致。
5. **格式声明**：`setExternalAudioSource(true, 48000, channels: 8, ...)` 返回 0，不解除钳制。
6. **私参 `che.audio.input_channels: 8`**（SDK 二进制 strings 中确认存在，
   对应 `adm_input_channels`）：返回 0，但 CH2~CH7 仍恒 0%。

## 5. 结论

- 接收侧多声道解码（含 3/5/7 奇数声道）在公开版 macOS 4.5.2 上**正常**；
- 发送侧的 OPUSMC **协商**在公开版上**正常**（multistream 编码器存在且可配置）；
- 发送侧的自定义音轨**输入管线被钳制为 2 声道**：8ch 交错输入只有前 2 声道进入编码器。
- 因此判断为 **SDK 侧能力缺口**，客户端无可行绕行。

**待研发确认的猜测**：Linux 定制版（v4.2.32 / v4.4.30.x）为发送侧多声道做的改动
（如 `MultistreamEncoder` 的多声道输入接线）可能未同步合并到 macOS 对应分支
（Native 同源，接收侧显然已合并，发送侧疑似遗漏输入部分）。

## 6. 后续跟踪建议

1. SDK 研发确认：macOS 分支是否包含 Linux 定制版的发送侧多声道改动；若未合并，评估合入。
2. 若合入或提供定制 macOS SDK，用 `Demo/macOS/MultiChannelAudioSender` 直接回归：
   换 Pods 里的 SDK 版本 → `pod install` → 发布 8ch WAV → Web/receiver 端看
   CH2~CH7 是否有内容即为通过判据。
3. 回归注意点（本 demo 的既定约定）：
   - `samples` 传总样本数（每声道 × 声道数），10 ms/帧；
   - 私参在 join 之前设置；
   - 通过判据参考 Linux 侧：订阅端 `getVolumeLevels()` 8 路均有随内容变化的音量，
     或 receiver dump 8 声道均与源素材对应。

## 7. 证据文件

- `Test/web_sub_macsender_8ch_20261010.png`：Web 订阅 8ch，codec=multiopus 8ch 判定截图
- `Test/macos_sender_ui_20261010.png`：sender app UI
- 接收端 dump（本机 `~/Library/Logs/audio_dump_playback_before_mixed_0_*_<uid>.wav`，
  日结材料，不入库）
- 详细时间线：`Test/TestResults_20261010_macos.md`「macOS 发送端实测」与「追加排查」两节
