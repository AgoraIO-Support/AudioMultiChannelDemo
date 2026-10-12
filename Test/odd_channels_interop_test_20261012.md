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

## 5. 反向验证：Web 端接收奇数声道流的实际表现（2026-10-12）

方法：macOS sender 4.7.0 分别以 3/5/7 声道（内置 `odd_{3,5,7}ch.wav`，payload 123/124/125 +
`custom_channel_num`）发布，Web demo（Chrome，agora-rtc-sdk-ng 4.20.1-200 + `ENABLE_MULTIOPUS_CODEC`）
订阅。下表数据来自首轮 15 s 采样（200 ms 间隔）；「末两声道恒等」证据来自后续补采轮
（120 ms 间隔、20 s 窗，5ch/7ch 各 165 样本）——两轮采样窗口不同，峰值数字不跨轮比较。

| 发送 | 订阅 | codec 判定 | 信令 channelCount | `getVolumeLevels()` 15 s 峰值 | demo 界面判定 |
| --- | --- | --- | --- | --- | --- |
| 3ch（源 CH0-2 有内容） | 成功 | multiopus | **4** | [83, 78, 79, 79]（4 路全有内容） | 「4 声道 → 多声道 ✓」（误判） |
| 5ch（源 CH0-4 有内容） | 成功 | multiopus | **6** | [78, 78, 79, 85, 76, 76]（6 路全有内容） | 「6 声道 → 多声道 ✓」（误判） |
| 7ch（源 CH0-6 有内容） | 成功 | multiopus | **8** | [79, 74, 77, 84, 78, 78, 76, 76]（8 路全有内容） | 「8 声道 → 多声道 ✓」（误判） |

**报错情况**：控制台无任何订阅/解码错误——`subscribe success`、远端音轨创建、audio element 正常进入
playing；仅有的日志为 `user-published` 提示与备用 WebSocket 竞速关闭等无害信息。截图：
`Test/web_recv_odd7ch_from_macos_20261012.png`。

**机制分析（2026-10-12 阅读 `agora-rtc-sdk-ng@4.20.1-200` 源码定位到代码级根因）**：

**这是 Agora Web SDK 的实现缺陷，不是浏览器 bug。**

1. SDK 的 multiopus 声道数是写死的偶数表：`initCustomPayloads`/`toChannelCount` 按
   `4 + 2 * t` 给 payload type 分配声道数——只有 4/6/8，没有 3/5/7。
2. SDP 生成照该表写 fmtp（`createMultiopusPayload`：`coupled_streams=floor(N/2)`、
   `num_streams=ceil(N/2)`、`channel_mapping=0..N-1`）——pt123 恒写 4ch 配置
   （coupled=2/streams=2），即便线上实际是 3ch（coupled=1/streams=2）。
3. 订阅后 SDK 从 codec stats 把这个（自己写进 SDP 的）声道数读回，经
   `processMultiopusStats` → `onMultiopusCodecReceived` → `updateChannelCount(n)`
   用 Insertable Streams 把解码帧拆成 n 路——音量条数与信令 channelCount 同源同错。
4. 实际解码由 Chrome/WebRTC 内置的 `AudioDecoderMultiChannelOpus` 完成（multiopus 是
   libwebrtc 多年维护的非标准扩展，能力完整、默认不宣告，见
   `References/chromium_webrtc_multi_channel_audio_references.md`）——浏览器只是按 SDP 的
   错误配置忠实解码：stereo 子流正常，mono 子流被 stereo 配置解成左右复制（双单声道），
   于是末两声道恒等。

即：Chrome 具备完整的多声道解码能力；**SDK 把声道配置写死成偶数表，使 SDP 声明与实际流不符**，
浏览器按错误配置解码才产生幻影声道。修复方向也明确：SDK 侧按流的真实 fmtp（或信令里的真实
声道数）生成/使用配置，而非查偶数表。

证据链（实测观测与上述代码路径吻合）：

1. **线上流确实是奇数声道**：同一 3ch 流的 macOS receiver dump 文件头为 48 kHz / **3ch** / 16bit
   （5ch/7ch 同理），Native 端按真实声道数正确解码——排除「发送端编码成 4ch」的可能；
2. **Web 端末两声道恒等**（幻影声道即末声道内容的复制）：
   - 3ch：每秒快照中 CH2 == CH3（底噪时同为 ~10%，内容时同为 75%）；
   - 5ch：120 ms 采样 165/165 样本 CH4 == CH5（±2pp），含内容峰值 [78,78]、[76,76]；
   - 7ch：120 ms 采样 165/165 样本 CH6 == CH7（±2pp），含内容峰值 [80,80]、[74,74]，
     且内容时刻以 14.0 s 素材循环周期整倍复现（0.6~1.5 s 与 14.6~15.4 s 两段完全对应）；
   与「mono 子流复制为立体声对」一致；
3. **听感吻合**：主观试听能听到 N 种不同声音、界面却显示 N+1 路——源素材只有 N 路内容，
   Web 的 CH0…CH(N-1) 对应源 CH0…CH(N-1)，第 N+1 路只是末声道的复制品（3/5/7 三个用例均已
   实测确认这一听感与显示的对应关系）。

由此也解释了为什么 demo 界面会误判「多声道 ✓」：判定依据 `getVolumeLevels()` 数组长度（=解码输出
声道数 4/6/8）与信令 channelCount（=payload 默认偶数声道数），两者都被误解码带偏。

**结论修订**：旧文档表述「Web 端无法解码奇数声道」不够准确。实测为：**不报错、按 N+1 偶数声道
解码、内容失真（末声道被复制出幻影声道）、音量条全部有量、demo 误判为多声道 ✓**——结果完全不可信，
奇数声道流切勿用 Web 端验证。这一表现比"直接报错"更危险，务必注意。

## 6. 补丁方案：不改 SDK 修复 Web 端解析/显示/播放（2026-10-12 实测通过）

### 6.1 可行性边界（SDP 实测）

订阅 3ch 流时抓取的 SDP（offer 与 answer 一致）只包含三个偶数 multiopus 配置
（PT 112/106/105 ↔ 8/6/4ch）与普通 opus——**奇数声道的真实配置（coupled=1/streams=2 等）
不在 SDP 中**：VOS 不透传发送端 fmtp，码流也不内嵌配置。因此「让浏览器按正确奇数配置解码」
必须改 SDK 或 VOS，外部补丁拿不到真实声道数。

### 6.2 补丁设计（`Demo/web/src/oddChannelFix.ts`，不改 SDK 文件）

解码后的 PCM 内容本身正确（stereo 子流正常；mono 子流被复制为末两声道幻影对），补丁在运行时修正：

1. **检测**：读 `track.transformer.lastSplitAudioData`（SDK 用 Insertable Streams 拆出的逐声道 PCM），
   采样窗口内末两声道逐样本恒等（±1e-6）且见过非零内容 → 判定为奇数声道流（真实 N = 解码 N+1 − 1）；
2. **显示修正**：包装 `getVolumeLevels()` 去掉幻影末项（音量条显示真实 N 路）；
   `track.channelCount` 改为真实 N；
3. **播放修正**：`selectChannels([0..N-1], "surround")` 让播放输出丢弃幻影声道
   （避免末声道内容重复响度）；逐声道试听序号 0..N-1 本就映射正确，无需改映射。

已接入 `Demo/web/src/Multiopus.vue` 订阅路径（命中时页面弹提示）；**三个 Live Demo 操作区均有
「奇数声道补丁 / Odd-channel fix」开关**（默认开，localStorage 持久化、三页共享）——订阅中关掉会
立即撤销补丁（恢复 SDK 原始误行为，音量条变回 N+1 路、末两声道恒等），打开则重新检测并修正，
便于集成方对比验证。开关关/开实测：5ch 流关 → 6 条（CH4==CH5=76 幻影可见）；开 → 5 条（修正）。

截图：`Test/web_odd_fix_toggle_20261012.png`（三个 Live Demo 的「奇数声道补丁」开关）。

### 6.3 补丁效果实测（3/5/7 全通过）

| 发送 | 补丁后 channelCount | 音量条数 | 15 s 各声道峰值 | 界面判定 |
| --- | --- | --- | --- | --- |
| 3ch | **3**（原误显 4） | 3 | [74, 78, 80] | 「3 声道 → 多声道 ✓」（正确） |
| 5ch | **5**（原误显 6） | 5 | [84, 80, 78, 83, 79] | 「5 声道 → 多声道 ✓」（正确） |
| 7ch | **7**（原误显 8） | 7 | [82, 76, 80, 84, 73, 78, 78] | 「7 声道 → 多声道 ✓」（正确） |

截图：`Test/web_recv_odd7ch_patched_20261012.png`（7 路音量条）。

### 6.4 补丁限制

- 检测签名「末两声道恒等」理论上可能与「末两声道恰好同内容的真偶数流」撞车（Channel ID 类
  逐声道可辨素材不会）；
- codec 行仍显示 SDP 协商的偶数声道数（来自 getStats，属参考信息，与内容无关）；
- 补丁按次生效（每次订阅独立检测）；三个 Live Demo 均已接入并有开关；
- 根本修复仍应在 SDK/VOS 侧（按流真实配置生成 fmtp），补丁仅为验证链路的过渡手段。

## 7. 结论

1. **奇数声道（3/5/7）端到端互通在 Native（macOS）与 Linux 之间全部验证通过**，
   包括混合方向（mac 发 linux 收、linux 发 mac 收）。
2. 奇数声道的必要条件不变：发送端须设置 `che.audio.custom_channel_num`（本仓库 demo 自动处理）；
   接收端免私参自适应解码；Linux 接收 sample 的 `--numOfChannels` 须与发送端一致。
3. Web 端不能用于奇数声道验证：实测表现不是报错，而是按 N+1 偶数声道误解码且内容失真（§5）；
   验证 demo 已内置运行时补丁可修正显示与播放（§6），但根本修复应在 SDK/VOS 侧。

## 8. 复现

- macOS 发送/接收自动化：`MultiChannelAudioSender -autoChannel <ch> -autoUid 12345 -autoWavPath <素材> -autoJoin YES -autoPublish YES`；
  `MultiChannelAudioReceiver -autoChannel <ch> -autoJoin YES -autoDump YES -autoQuitAfter 50`。
- Linux（AWS 测试机，`~/multiopus/odd_wav/` 存有素材）：
  `docker run --rm --network host -v ~/multiopus/odd_wav:/extra agora-multichannel-linux:latest /app/sample_send_wav_multiopus --token <appid> --channelId <ch> --userId 12345 --audioFile /extra/odd_3ch.wav --sampleRate 48000 --numOfChannels 3 --custom_channel_num 3 --custom_payload_type 123 --custom_sample_rate 48000`；
  接收端对应 `sample_receive_pcm_multiopus --numOfChannels 3 --audioFile ...`。
- 逐声道 NCC 分析方法同 `Test/macos_sender_multi-channel_audio_test_result.md` §4.2。
