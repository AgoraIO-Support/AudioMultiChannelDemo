# 问题记录：Linux→Web 场景下静音声道音量柱不归零（`getVolumeLevels()` 仪表底数）

> 日期：2026-10-08
> 状态：**根因已确认，非 SDK 缺陷**——音量表是 dB 宽量程刻度（`(dBFS_RMS+100)/100`，仅数字零显示 0%）；16bit 发送素材静音段含 -84dB dither（不可闻），被仪表忠实显示为 ~10%。24bit 素材对照实验端到端坐实因果链（见 3.8/3.9）。可听输出无异常。
> 关联 FAQ：`Demo/web/README.md` / `README.en.md`「常见问题」
> English version: `web_volume_meter_floor_linux2web_en.md`

## 1. 现象

在 Live Demo 2（`?demo=2`）页面作为观众订阅 Linux 发送端（`sample_send_wav_multiopus`，8 声道 OPUSMC）时：

- 素材为 Channel ID（各声道轮播报号），任一时刻只有 1 个声道发声；
- **实际听感正常**：只有发声声道有声，其余 7 个声道听不到声音；
- 但**音量柱显示异常**：7 个静音声道的音量柱不归零，维持在 ~6~10%，与发声声道（~12%）差距很小，几乎无法从音量柱区分哪个声道在发声。
- 对比场景 **web→web（双标签页自验证）无此问题**：静音声道音量柱精确归零。

## 2. 测试环境

| 项 | 值 |
| --- | --- |
| App ID | `43f818097c124394821bd44598b7ca9d`（项目 "Test 128 Hosts Env"） |
| 频道 | `multichannel8ch` |
| Argus callId | `6ac74e1c33d2695a000022b8` |
| 发送端 | AWS 测试服务器（54.152.163.4）Docker 容器 `multiopus-sender`，uid **8888**，Linux SDK 4.4.30.149，payload 125 / 8ch / 48 kHz，素材 `8_Channel_ID_signed_16_bit_pcm.wav`（循环） |
| Web 接收端 | Chrome 154，上海侧，`agora-rtc-sdk-ng` 4.20.1-200（`release_20260915_02_vos_web`），uid 433911（手工测试）/ 479912（自动化测量） |
| Web 对照发布端 | 浏览器标签页 uid 7777，发布 `8_Channel_ID.wav`（8ch） |

发送端启动命令（服务器上）：

```bash
docker run -d --rm --network host --name multiopus-sender agora-multichannel-linux \
  ./sample_send_wav_multiopus --channelId multichannel8ch \
  --token 43f818097c124394821bd44598b7ca9d --userId 8888 \
  --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

## 3. 排查过程与证据

### 3.1 源素材本身干净

对 `Demo/linux/public/8_Channel_ID_signed_16_bit_pcm.wav` 按声道、按 0.5s 窗测 RMS：

- 各声道静音窗 RMS ≈ **0.006%**（数字静音）；发声窗峰值 4~12%。

### 3.2 码流本身干净（Linux 接收端落盘验证）

AWS 服务器上运行 Linux 接收端订阅 uid 8888，落盘 25 秒解码 PCM 后回传分析：

```bash
docker run -d --rm --network host --name multiopus-recv agora-multichannel-linux \
  ./sample_receive_pcm_multiopus --channelId multichannel8ch \
  --token 43f818097c124394821bd44598b7ca9d --userId 9999 --remoteUserId 8888 \
  --numOfChannels 8 --sampleRate 48000 --audioFile /tmp/recv8ch.pcm
```

结果：静音声道 RMS ≈ **0.003%**（数字静音），发声声道峰值 4~12%，与源素材一致。
→ 编码 → 网络 → 解码全链路无泄漏、无串声道。

### 3.3 传输质量干净（Argus）

两段 Web 接收会话（uid 433911 / 479912，peer=8888）：

| 计数器 | 结果 |
| --- | --- |
| Wrtc Audio Recv Packets Loss（2045） | **全程 0%** |
| Wrtc Audio Recv Bitrate（2044） | 稳定 ~256 kbps（符合 8 声道 OPUSMC 码率档位） |
| Wrtc Audio Recv Current Delay Ms（2047） | 56~172 ms，正常抖动范围 |

→ 排除跨洋丢包触发 NetEq PLC 补偿噪声的假设。

### 3.4 Web 端实际播放的 PCM 是数字静音（Web Audio tap 实测）

在订阅页面用调试钩子 `window.__mcRemoteTrack`（`Demo/web/src/Multiopus.vue` 暴露）取远端音轨，
`AudioContext` + `createChannelSplitter(8)` 直接测量播放轨 PCM：

- 注意：`createMediaStreamSource` 必须设置 `channelCount=8` + `channelCountMode='explicit'` +
  `channelInterpretation='discrete'`，否则 Chrome 默认下混成立体声（此时只有 CH0/CH1 有内容，
  CH2~7 全零，是测量假象）。
- 结果：静音时段 PCM RMS ≈ **0.005%**（数字静音）。

→ 送达播放/可听输出的数据没有问题，与听感一致。

### 3.5 对照实验：web→web 音量表精确归零

同页面、同 SDK、同解码器，订阅**浏览器发布**的 8 声道流（uid 7777）：

| 场景 | 静音声道 `getVolumeLevels()` 最小值 | 发声声道峰值 | tap 实测静音底 |
| --- | --- | --- | --- |
| Linux→web（uid 8888） | **6~9%（有底数）** | 78~84% | 0.005% |
| web→web（uid 7777） | **0%（精确归零）** | 78~80% | 0% |

→ 音量表底数**只在 Linux 编码的流上出现**，web 编码的流完全正常。

### 3.6 编码帧抓取对比：排除帧长假设（CBR/VBR 差异后也被排除）

在 AWS 上用 `sample_receive_encoded_audio` 抓取编码帧（日志含 `length` 与 `sendTs`）：

| 流 | 帧长分布 | sendTs 间隔 | 判读 |
| --- | --- | --- | --- |
| Linux 发送（uid 8888，WAV PCM 10ms 推帧 → SDK 编码） | **恒定 640 字节**（590/590 帧） | 恒 960 样本 = **20ms** | CBR ~256 kbps，静音时段帧也不缩小 |
| Web 发布（uid 7777，`8_Channel_ID.wav`） | 15~260 字节（均值 109），其中 15 字节帧 ×240 | 恒 960 样本 = **20ms** | VBR，静音时段帧极小（类 DTX 形态） |

→ **排除"10ms vs 20ms 帧长"假设**：两条流的线上帧长同为 20ms（Linux 编码器会把 10ms PCM
推帧缓冲成 20ms Opus 帧；`sendAudioPcmData` API 契约本身也只接受 10ms 帧，见
`NGIAgoraMediaNode.h`）。当时观察到的码流形态差异是 **CBR（恒定 640B）vs VBR（静音时 15B 小帧）**。
（**注：CBR/VBR 差异随后也被排除**——3.7 中底数消退前后帧形态不变；真正根源是素材静音底，见 3.8/3.9。）

注：SDK 二进制中存在 `che.audio.bitrate_mode` / `che.audio.dtx_mode` / `che.audio.acm_ptime`
等私参（`strings libagora_rtc_sdk.so` 可见），可用于后续 VBR/DTX 对照实验。但截至 2026-10-08
这些参数的语义**未经实证**（取值集合/默认值/生效时机未知）：仓库内 X86_64 与 Aarch64 的
`libagora_rtc_sdk.so` 及 tgz 包均为 stripped（无符号表），反汇编核实需要带符号表的 SDK 包。
命名推断（未验证）：`acm_ptime` = ACM 包化帧长；`bitrate_mode` = CBR/VBR 选择；
`dtx_mode` = DTX 开关（二进制内另有 `EnableOpusDtx`/`DisableOpusDtx` 字符串佐证 DTX 存在）。

### 3.7 三个发送端 sample 对照与底数的会话态表现（2026-10-08 下午补测）

逐一运行 `Demo/linux/multiopus/` 下 3 个发送端 sample，同一 Web 订阅端
（Live Demo 1 + `window.__mcRemoteTrack`，16s 窗内逐声道 min/max）对比：

| 发送端 sample | uid | 流会话状态 | 线上帧形态 | 静音声道音量表 min | 底数 |
| --- | --- | --- | --- | --- | --- |
| WAV（session #1，启动后 5~15 min） | 8888 | 新鲜 | CBR 640B/20ms | 6~12% | **有** |
| WAV（session #1，启动后 ~59 min） | 8888 | 长时间运行 | CBR 640B/20ms（复抓帧确认不变） | **0%** | 无（已消退） |
| PCM（新鲜会话） | 8889 | 新鲜 | CBR 640B/20ms | **0%** | 无 |
| 预编码 Opus（opus8） | 8890 | 新鲜 | 640B/20ms 直通 | 全声道持续 32~91% | 素材各声道始终有声，N/A |
| WAV（session #2，重启容器后） | 8888 | 新鲜（~2 min 与 ~6 min 各测一次） | CBR 640B/20ms | 4.6~8.9% | **有（复现）** |

要点：

- **底数不是某个 cpp 特有的**：PCM 路径与 WAV 路径共用同一编码器与私参组合
  （payload 125 / channel_num 8 / sample_rate 48000），三者私参完全一致。
- **底数与"流会话新鲜度"相关**：WAV 发送端两次新鲜启动都出现底数，运行 ~1 小时后
  底数消失；消退点位于会话年龄 15 min ~ 60 min 之间（未细分）。
  （**后由 3.8/3.9 进一步查明**：仪表读的是真实解码残余；会话年龄影响的是编码器是否把
  素材的 dither 残余抑制为零——素材静音底才是底数的直接来源，编码器抑制状态决定其是否显现。）
- **CBR/VBR 假设也被削弱**：底数存在与消失的两个时段，Linux 侧线上帧都是恒定 640B CBR
  （抓帧确认），帧形态不变而底数消失——底数触发条件还需进一步隔离（候选变量：流会话年龄、
  uid 复用导致的 VOS 会话替换状态、频道内其他参与者状态等）。
- 预编码 Opus 直通路径（opus8）功能正常，8 声道解码、音量随内容起伏；其素材
  （`8ch_2.opus`）各声道始终有声，不能用于底数测量。

### 3.8 音量表实现源码分析：无 Linux/Web 分支，dB 宽量程刻度 + 素材静音底解释差异

对 `Demo/web/node_modules/agora-rtc-sdk-ng/AgoraRTC_N-production.js`（4.20.1-200）pretty print
后分析（prettier 输出见分析时临时文件，关键行号为 prettified 版本行号）：

- 调用链：`RemoteAudioTrack.getVolumeLevels()`（26974 行）→ `transformer.getVolumes()`
  （21397 行，`class jb` = Multiopus Insertable Streams transformer，21317 行）。
- 音量来源：`lastSplitAudioData`——transformer 中实际流过的**逐声道解码 PCM**
  （`AudioData` 各声道 plane，21335-21377 行），即仪表读的是真实解码数据，不是码流估算。
- 每声道电平公式（`Fb`，21304 行）：
  `level = max(10*log10(mean(x²)) + 100, 0) / 100`，即 **(dBFS_RMS + 100) / 100**；
  仅当样本完全为 0（sum=0）时返回 0。
- **没有任何区分 Linux/Web 来源的代码分支**，全流路单一实现。

换算对照（实测值代入公式）：

| 静音段残余 RMS | 对应 dB | 音量表读数 |
| --- | --- | --- |
| 精确数字零 | -inf | **0%** |
| 0.003%（Linux 接收端落盘实测值） | -90 dB | **~9.5%**（与观测的 6~12% 底数吻合） |
| 12%（报号语音 RMS） | -18 dB | ~82%（与观测峰值吻合） |

两个发送场景的素材静音底（本轮实测）：

| 素材 | 用途 | 静音段 min RMS | 音量表显示 |
| --- | --- | --- | --- |
| `8_Channel_ID_signed_16_bit_pcm.wav`（16bit） | Linux 发送（WAV/PCM 两 sample 同内容） | 0.0061%（**-84.3 dB，带 dither**） | ~6~12% 底数 |
| `8_Channel_ID.wav`（24bit） | Web 发布 | **精确数字零** | **0%** |

→ **linux→web 与 web→web 的音量表差异由素材静音底决定**：16bit 素材静音段含 -84dB dither，
编码传输后残余 ~-90dB（不可闻），被 dB 宽量程仪表放大显示为 ~10%；24bit 素材静音段为数字零，
仪表显示 0。仪表代码本身无分支、无 bug 意义上的错误——它忠实地以 dB 刻度报告了真实残余。

残留细节（已不影响结论，仅存档）：老化的 WAV 流会话（~1h）与一次新鲜 PCM 会话中，解码静音
声道变为精确零（仪表归零），而新鲜 WAV 会话两次都带残余——编码器对 dither 级信号的抑制
似乎随会话状态/年龄生效，具体触发条件需 SDK 内部确认，属 -90dB 级细节。

### 3.9 端到端确认：换用静音段为数字零的 24bit 素材，底数消失（2026-10-08）

**关于"Audacity 里看振幅是 0"**：16bit 素材的静音段不是零，而是 ±1~7 LSB 的 dither
（实测静音窗 CH0 样本分布：0 占 19%、±1 占 35%、±2~3 占 39%、±4~7 占 7%，80.8% 样本非零）。
±7/32768 ≈ 0.02% 满幅，Audacity 默认线性波形视图无法显示（需切换到 Waveform (dB) 视图或
Effect→Amplify +60dB 才能看到）。

**确认实验**：AWS 上用同一 WAV 发送端 sample、同一频道、新鲜会话（uid 8891），仅把素材换成
`8_Channel_ID_24bit.wav`（静音段为精确数字零；demo 的 24bit→16bit 转换为右移 8 位，
`0 >> 8 = 0`，零保留）：

| 发送端会话 | 素材静音段 | 音量表静音声道 min（16s 窗） |
| --- | --- | --- |
| 新鲜 WAV 会话（uid 8888，两次） | 16bit，-84dB dither | 4.6~12%（有底数） |
| 新鲜 WAV 会话（uid 8891） | 24bit，精确零 | **0.00%（无底数）** |

→ 唯一变量是素材静音底，底数随之消失/复现，**素材 dither 因果链端到端坐实**。

## 4. 结论

- **音量表无 Linux/Web 分支**：`getVolumeLevels()` 单一实现，读 Insertable Streams 里真实的
  逐声道解码 PCM，公式为 `(dBFS_RMS + 100) / 100` 的 dB 宽量程刻度（仅数字零显示 0%，
  -90dB 残余即显示 ~10%）。
- **linux→web 与 web→web 的显示差异由素材静音底决定**：Linux 发送素材（16bit WAV/PCM）静音段
  含 -84dB dither，解码后残余 ~-90dB（不可闻）→ 显示 ~10%；Web 发布素材（24bit）静音段为
  精确数字零 → 显示 0%。听感"无声"与测量完全一致。
- 已排除：编码/网络/解码链路泄漏（Linux 落盘 0.003% RMS）、传输丢包（Argus 0%）、帧长
  （双方线上均 20ms）、发送端 sample 差异（WAV/PCM 同编码器同私参）、播放链路
  （Web Audio tap 实测静音段数字零）。
- 残留细节（-90dB 级，不影响结论）：dither 残余在部分会话（老化 WAV 流、一次新鲜 PCM 流）
  中被编码器抑制为精确零、新鲜 WAV 会话两次未抑制——抑制触发条件需 SDK 内部确认。
- 对验证工作的影响：逐声道内容核对以「`selectChannels` 声道选择 + 试听」为准；读音量柱时
  记住它是 dB 刻度——~10% 等于 -90dB，等同于无声。

## 5. 复现/测量方法备注

- Web 页面调试钩子：`window.__mcRemoteTrack`（订阅成功后可用），支持
  `getVolumeLevels()`、`getMediaStreamTrack()`（轨道 `channelCount=8`）。
- 页面测量脚本要点：见 3.4 的 AudioContext + ChannelSplitter 接法；采样窗口 100 ms × 100 次。
- Linux 落盘分析：Python 按 8 声道交错解交错后分窗计算 RMS（`samples[i::8]`）。

## 6. 附件

| 文件 | 说明 |
| --- | --- |
| `live_demo_2_8_audio_streaming_audio_volume_ui_linux2web_20261008a.png` | 问题截图（修复前 Live Demo 2 裸进度条 + 静音声道不归零） |
| `live_demo_2_volume_ui_fixed_20261008.png` | 音量条 UI 修复后（CH 标签 + 百分比），订阅 Linux 8 声道流的实测渲染 |
| `live_demo_2_volume_ui_fixed_20261009.png` | 同上，2026-10-09 更新版（现行版本） |

## 7. 后续行动

- [x] 音量表机制已查清（无需反馈 SDK bug）：`getVolumeLevels()` 为 `(dBFS+100)/100` 的
      dB 宽量程刻度、无 Linux/Web 分支；底数根源是 16bit 发送素材静音段的 -84dB dither
      （24bit 素材对照实验已坐实，见 3.9）。可选改进：向 Web SDK 团队建议在文档中注明
      音量值为 dB 刻度语义
- [ ] （可选，仅残留细节相关）核实私参语义：`che.audio.bitrate_mode` / `che.audio.dtx_mode` /
      `che.audio.acm_ptime`（取值、默认值、生效时机）。当前仅 strings 级证据，仓库内 .so 均为
      stripped，需要带符号表的 SDK 包或咨询 SDK 研发；2026-10-08 暂停。注：主问题已闭环，
      此项只与"编码器在何种条件下把 dither 残余抑制为零"的残留细节（3.7/3.8 末段）相关
- [x] FAQ 写入 `Demo/web/README.md` / `README.en.md`
- [x] Live Demo 2 音量条 UI 对齐 Live Demo 1（CH 标签 + 百分比），已部署
