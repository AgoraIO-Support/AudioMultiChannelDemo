# 奇数声道（3/5/7）多端互通测试报告

> 日期：2026-10-12
> 结论：**3/5/7 奇数声道在 Linux↔Linux、Linux→macOS、macOS↔macOS、macOS→Linux 四条链路全部互通，
> 逐声道内容正确且与源素材同序号对应（12/12 通过）**。Web 端不能作为奇数声道的验证端——实测不报错、
> 但会按 N+1 偶数声道配置误解码，内容失真且界面误判为多声道（详见 §5 反向验证）。

## 1. 测试范围

| 维度 | 取值 |
| --- | --- |
| 声道数 | 3（payload 123 + `custom_channel_num=3`）、5（124 + 5）、7（125 + 7） |
| 链路 | Linux→Linux（同机双容器）、Linux→macOS、macOS→macOS、macOS→Linux |
| 发送端 | Linux：`sample_send_wav_multiopus`（Docker `agora-multichannel-linux:latest`，AWS 测试机）；macOS：`MultiChannelAudioSender` 4.7.0（`-autoWavPath` 自定义素材 + 自动化 join/publish） |
| 接收端 | Linux：`sample_receive_pcm_multiopus`（`--numOfChannels` 与发送端一致，落盘回调 PCM）；macOS：`MultiChannelAudioReceiver` 4.7.0（`-autoDump` → `~/Library/Logs/` per-user dump WAV） |

## 2. 素材与基准

源素材为 8 声道 Channel ID WAV（48 kHz / 16bit，八声道轮播内容）。用 ffmpeg `channelmap`
截取前 N 声道生成奇数声道素材（48 kHz / 16bit WAV）：

```bash
ffmpeg -y -i 8_Channel_ID_signed_16_bit_pcm.wav -af "channelmap=map=0|1|2" -c:a pcm_s16le odd_3ch.wav
ffmpeg -y -i 8_Channel_ID_signed_16_bit_pcm.wav -af "channelmap=map=0|1|2|3|4" -c:a pcm_s16le odd_5ch.wav
ffmpeg -y -i 8_Channel_ID_signed_16_bit_pcm.wav -af "channelmap=map=0|1|2|3|4|5|6" -c:a pcm_s16le odd_7ch.wav
```

素材已入库 `Demo/linux/public/odd_{3,5,7}ch.wav`。10ms 包络峰值基准（各声道均有内容）：

- 3ch：CH0=-17.1 / CH1=-19.4 / CH2=-19.9 dB
- 5ch：另含 CH3=-14.2 / CH4=-21.7 dB
- 7ch：另含 CH5=-21.0 / CH6=-19.8 dB

## 3. 判定方法

接收端落盘数据（Linux 为裸 s16le 交错 PCM；macOS dump 为 48 kHz / Nch / 16bit WAV）与源素材做
10 ms 包络归一化互相关（NCC，循环相位对齐）：每声道取最大 NCC 的源声道，须满足
① 与同序号源声道匹配；② NCC ≥ 0.4；③ 内容级峰值 > -45 dB。三项同时满足记该声道通过。

## 4. 实测结果（12/12 通过）

| 链路 | 3ch | 5ch | 7ch |
| --- | --- | --- | --- |
| Linux → Linux | ✅ 各声道 NCC 0.46~0.61，跨声道 ≤0.03 | ✅ 0.68~1.00，≤0.03 | ✅ 0.69~0.98，≤0.03 |
| Linux → macOS（receiver dump） | ✅ 0.65~0.74，≤0.03 | ✅ 0.65~0.98，≤0.03 | ✅ 0.62~0.98，≤0.03 |
| macOS → macOS（receiver dump） | ✅ 0.74~0.76，≤0.03 | ✅ 0.72~0.98，≤0.03 | ✅ 0.73~0.99，≤0.03 |
| macOS → Linux | ✅ 0.80~0.95，≤0.03 | ✅ 0.75~0.99，≤0.03 | ✅ 0.75~1.00，≤0.03 |

- 每条链路、每个声道：内容级峰值 -13.9 ~ -21 dB（与素材基准一致），且与同序号源声道
  一一对应（无重排、无串声道、无丢声道）。
- 发送端日志：Linux WAV sender 正确解析奇数声道头（如 "Sample Rate: 48000, Channels: 3"）
  并循环发送；macOS sender 私参按素材自动映射（3→123、5→124、7→125 + custom_channel_num）。

## 5. 结论

1. **奇数声道（3/5/7）端到端互通在 Native（macOS）与 Linux 之间全部验证通过**，
   包括混合方向（mac 发 linux 收、linux 发 mac 收）。
2. 奇数声道的必要条件不变：发送端须设置 `che.audio.custom_channel_num`（本仓库 demo 自动处理）；
   接收端免私参自适应解码；Linux 接收 sample 的 `--numOfChannels` 须与发送端一致。
3. Web 端不能用于奇数声道验证：实测表现不是报错，而是按 N+1 偶数声道误解码且内容失真（§5）。

## 5. 反向验证：Web 端接收奇数声道流的实际表现（2026-10-12）

方法：macOS sender 4.7.0 分别以 3/5/7 声道（内置 `odd_{3,5,7}ch.wav`，payload 123/124/125 +
`custom_channel_num`）发布，Web demo（Chrome，agora-rtc-sdk-ng 4.20.1-200 + `ENABLE_MULTIOPUS_CODEC`）
订阅并采样 `getVolumeLevels()` 15 s。

| 发送 | 订阅 | codec 判定 | 信令 channelCount | `getVolumeLevels()` 15 s 峰值 | demo 界面判定 |
| --- | --- | --- | --- | --- | --- |
| 3ch（源 CH0-2 有内容） | 成功 | multiopus | **4** | [83, 78, 79, 79]（4 路全有内容） | 「4 声道 → 多声道 ✓」（误判） |
| 5ch（源 CH0-4 有内容） | 成功 | multiopus | **6** | [78, 78, 79, 85, 76, 76]（6 路全有内容） | 「6 声道 → 多声道 ✓」（误判） |
| 7ch（源 CH0-6 有内容） | 成功 | multiopus | **8** | [79, 74, 77, 84, 78, 78, 76, 76]（8 路全有内容） | 「8 声道 → 多声道 ✓」（误判） |

**报错情况**：控制台无任何订阅/解码错误——`subscribe success`、远端音轨创建、audio element 正常进入
playing；仅有的日志为 `user-published` 提示与备用 WebSocket 竞速关闭等无害信息。截图：
`Test/web_recv_odd7ch_from_macos_20261012.png`。

**机制分析（与证据吻合）**：Web 解码器只实现了偶数声道 Opus 多流配置，按 payload type 的默认偶数
声道数去解（123→4、124→6、125→8）。奇数声道 OPUSMC 的最后一个 mono 子流被当作 coupled stereo 解，
其内容被复制到两个输出声道——3ch 时间线中 CH2/CH3 在同一秒同步出现等量内容（75%/75%），与
「mono 当 stereo 解」一致；源素材中不存在的第 N+1 个声道因此也出现内容级音量。

**结论修订**：旧文档表述「Web 端无法解码奇数声道」不够准确。实测为：**不报错、按 N+1 偶数声道
解码、内容失真（末声道被复制出幻影声道）、音量条全部有量、demo 误判为多声道 ✓**——结果完全不可信，
奇数声道流切勿用 Web 端验证。这一表现比"直接报错"更危险，务必注意。

## 6. 复现

- macOS 发送/接收自动化：`MultiChannelAudioSender -autoChannel <ch> -autoUid 12345 -autoWavPath <素材> -autoJoin YES -autoPublish YES`；
  `MultiChannelAudioReceiver -autoChannel <ch> -autoJoin YES -autoDump YES -autoQuitAfter 50`。
- Linux（AWS 测试机，`~/multiopus/odd_wav/` 存有素材）：
  `docker run --rm --network host -v ~/multiopus/odd_wav:/extra agora-multichannel-linux:latest /app/sample_send_wav_multiopus --token <appid> --channelId <ch> --userId 12345 --audioFile /extra/odd_3ch.wav --sampleRate 48000 --numOfChannels 3 --custom_channel_num 3 --custom_payload_type 123 --custom_sample_rate 48000`；
  接收端对应 `sample_receive_pcm_multiopus --numOfChannels 3 --audioFile ...`。
- 逐声道 NCC 分析方法同 `Test/macos_sender_multi-channel_audio_test_result.md` §4.2。
