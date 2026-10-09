# 沟通记录备份：客户音频多声道项目（2025-04）

> 用途：`References/沟通记录/` 目录计划删除，本文档将其中全部材料的内容文字化存档。
> 整理日期：2026-10-09。原始二进制文件（截图 / xlsx / mkv）不内嵌于本文档，内容均已转为文字与表格。
> **脱敏说明**：按本仓库开源前的脱敏约定，客户名称已隐去（客户侧工程师以名字 Abhi 代称）；
> 原文件名中含客户公司标识的前缀在本文档中统一以 `customer_` 前缀代称。
> English version: `communication_records_backup_en.md`

---

## 1. 背景与结论速览

- **客户**：某广电客户；Agora 侧：Alan（CSE）、lqk（SDK 研发）。
- **场景**：客户 Linux 服务器把"4 条立体声轨承载 8 声道"的广播素材经 Agora RTC 发送（multiopus/OPUSMC），
  Web 端订阅验证。
- **问题**：Web 端订阅听到杂音/内容错乱。
- **根因**：发送端 audio layout——源数据的声道排布与 SDK `sendAudioPcmData` 期望的
  **逐样本交错（sample-interleaved）8 声道布局**不一致。
- **解决**：2025-04-14 客户确认"确实是发送端 audio layout 问题"，按通话确认的映射重排后解决。

**最终确认的映射**（通话确认，见 3.2）：8 声道逐帧交织，第 n 帧为
`[T1Ln, T1Rn, T2Ln, T2Rn, T3Ln, T3Rn, T4Ln, T4Rn]`，即
CH1=T1L、CH2=T1R、CH3=T2L、CH4=T2R、CH5=T3L、CH6=T3R、CH7=T4L、CH8=T4R。
（即本仓库 `Demo/docs/linux-sdk-multichannel.html` §1.5 记录的标准布局。）

---

## 2. 材料清单与内容化存档

原目录：`References/沟通记录/`（约 128 MB）。

| 文件 | 类型/大小 | 内容摘要 |
| --- | --- | --- |
| `沟通记录2025.txt` | 文本，4.2 KB | 2025-04-07 ~ 04-14 企业微信沟通记录（全文见附录 A） |
| `audio_mutil_channel_mapping.txt` | 文本，240 B | 映射确认对话记录（全文见 3.2） |
| `abhi_audio_order_mapping.jpg` | 图片，45 KB | 客户研发 Abhi 提供的映射表（通话中讨论的版本），内容见 3.2 |
| `alan_audio_order_mapping.png` | 图片，30 KB | Alan 同步确认后的声道-帧序号表，内容见 3.2 |
| `customer_order.xlsx` | Excel，13 KB | 客户当前发送顺序（两张候选表），内容见 3.3 |
| `Agora_Order_Expectation.xlsx` | Excel，12 KB | Agora 期望顺序，内容见 3.4（含表述歧义说明） |
| `企业微信截图_ccca…png` | 截图，309 KB | 客户研发 Abhi 发送两份 xlsx 的对话上下文，内容见 3.5 |
| `企业微信截图_31d7…png` | 截图，365 KB | VictoriaMulti.mkv 的 ffprobe 输出，内容见 3.5 |
| `VictoriaMulti.mkv` | 视频，133 MB | 客户测试源文件；元数据见 3.6 |
| `~$ltipleAudioChannel.docx` | 162 B | Office 临时锁文件，无内容价值，未备份 |

---

## 3. 各材料详细内容

### 3.1 沟通记录2025.txt（聊天全文）

2025-04-07 ~ 04-14 的企业微信沟通记录，完整原文见**附录 A**。要点时间线：

- **04-07**：Alan 先请客户提供 Linux 集成代码；说明客户 8 声道**不是 7.1 环绕**，而是
  (0,1)(2,3)(4,5)(6,7) 四组左右声道对；提供测试频道（AppId `aab8b8f5a8cd4469a63042fcfafe7063`、
  频道 `142651995438`、无 token）供lqk订阅试听，并尝试 dump。
- **04-08 上午**：Alan 发出 `AGORA_sample.txt` / `output.txt` / 截图；lqk多次要求拿到具体音频，
  以判断杂音类型（完全杂乱噪声 vs 能听到人声）；lqk发现客户代码假设"每条 track 长度严格一致、
  每 track 严格 2 声道"，但代码中又出现单声道 track 的迹象，二者矛盾；客户提供 iOS app 订阅录音
  `far_in_48000hz_2ch.wav`。
- **04-08 11:56（关键判断）**：lqk提出——**源数据多声道是交织存储（LRLRLR…），但读取时按
  平面块序（LLLL…RRRR…）读了**。
- **04-08 下午**：Alan 发客户测试源文件 `VictoriaMulti.mkv`；lqk指出服务器端不会直接读 mkv、
  应转 PCM 再读（客户侧经 shared memory 中转）；确认 4 条轨都是双声道、各轨时长有细微差异但影响
  不大，重点排查交织存储问题。
- **04-09**：客户研发给出音频 layout（`customer_order.xlsx` + 图）；Alan 回发
  `Agora_Order_Expectation.xlsx`。
- **04-14**：Alan 确认：**问题解决，确实是发送端 audio layout 问题**。

### 3.2 声道映射确认（audio_mutil_channel_mapping.txt + 两张映射图）

`audio_mutil_channel_mapping.txt` 全文：

```text
@Alan This is the mapping that we discussed in Call. Please verify again
[abhi_audio_order_mapping.jpg]

Synced up with @Abhi, the audio layout is like below
[alan_audio_order_mapping.png]

Confirmed that it should be correct.
```

**abhi_audio_order_mapping.jpg**（客户研发 Abhi 提供、通话中讨论的映射，两行的表）：

| CH1_1 | CH2_1 | CH3_1 | CH4_1 | CH5_1 | CH6_1 | CH7_1 | CH8_1 | CH1_2 | CH2_2 | … |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T1L1 | T1R1 | T2L1 | T2R1 | T3L1 | T3R1 | T4L1 | T4R1 | T1L2 | T1R2 | … |

读法：`CHn_m` = 第 m 帧的第 n 声道；`TkL/Rm` = 第 k 条轨的左/右声道第 m 个样本。
即**逐帧交织**：第 1 帧 = [T1L1, T1R1, T2L1, T2R1, T3L1, T3R1, T4L1, T4R1]，第 2 帧同理。

**alan_audio_order_mapping.png**：纵向序号表 CH1_1…CH8_1、CH1_2…CH8_2、CH1_3、CH2_3…
（声道-帧序号清单，与上图配套）。

### 3.3 customer_order.xlsx（客户当前顺序）

参数块（两份 xlsx 相同）：

| 项 | 值 |
| --- | --- |
| 轨数 × 声道 | 4 条轨，每轨 2 声道（L, R） |
| 单样本大小（L+R） | 32 bit（L 16bit + R 16bit） |
| 单次写入 SDK 数据量（全部轨） | 15360 字节 |
| 每轨单次写入数据量（L&R） | 15360 / 4 = 3840 字节 |
| 每轨单次写入样本数（L&R） | 3840 / 32 = 120 |

表 1（标题为"客户当前顺序"，track 块序：每轨 120 个样本的 L/R 交织为一块，再排下一轨）：

```text
T1L1 T1R1 T1L2 T1R2 … T1L120 T1R120 | T2L1 T2R1 … T2L120 T2R120 | T3… | T4L1 … T4L120 T4R120
```

表 2（同文件第二张表，全平面序：先全部轨的 L 系列、再全部 R 系列）：

```text
T1L1 T1L2 … T1L120 T2L1 … T2L120 T3L1 … T4L120 | T1R1 T1R2 … T1R120 T2R1 … T4R120
```

### 3.4 Agora_Order_Expectation.xlsx（Agora 期望顺序）

参数块同 3.3。其后为图例（T1L1 = Track 1 Left sample 1，16 bit；L+R 一对 32 bit）与
"Agora Expected Order" 表，其 ORDER 行按单元格原文为：

```text
T1L1 T1L2 T1L3 T1L4 … T1L120 | T2L1 … T2L120 | T3L1 … T3L120 | T4L1 … T4L120 | T1R1 T1R2 … T1R120 | … | T4R1 … T4R120
```

> **存档说明（歧义提示）**：该行字面呈现为"先全部 L 再全部 R"的平面序，与通话后最终确认的
> 逐帧交织映射（3.2）不一致。该表为沟通中快速绘制的示意，**最终有效约定以 3.2 的映射图与
> 2025-04-14 的解决确认为准**；本仓库现行文档（`Demo/docs/linux-sdk-multichannel.html` §1.5）
> 记录的逐样本交错布局即最终约定。

### 3.5 企业微信截图 ×2

- **截图_ccca…png**：客户研发 Abhi 先后发出 `customer_order.xlsx`（"this is the current Order we are
  sending to Agora (4 tracks where each track has 2 channels L & R)"）与 `Agora_Order_Expectation.xlsx`
  （"can you confirm with the BE engineer that he expects this order from us?"），附图内预览与
  3.3/3.4 内容一致。
- **截图_31d7…png**：`VictoriaMulti.mkv` 的 ffprobe 输出——1 路视频（mpeg4, 1920x1080）+
  **4 路音频流（均为 AC-3, 44100 Hz, stereo, 192 kb/s）**，各音频流时长略有差异
  （约 4:00.571 ~ 4:00.606）。该截图佐证了 04-08 的观察（"都是双声道的，但是 duration
  是有区别的"）。

### 3.6 VictoriaMulti.mkv（133 MB 测试源视频）

- 角色：客户的测试源文件；服务器侧不直接读 mkv，而是解码后经 shared memory 中转出 PCM。
- 元数据（2026-10-09 本地 ffprobe 复核，与 3.5 截图一致）：

| 流 | 类型 | 参数 |
| --- | --- | --- |
| #0 | 视频 | MPEG-4（Simple Profile），1920x1080，yuv420p |
| #1~#4 | 音频 ×4 | AC-3，44100 Hz，stereo，192 kb/s（4 条立体声轨承载 8 声道内容） |

- 容器总时长 ~243.7 s（4:03.7），总码率 ~4367 kb/s。
- 与现行文档的关系：该素材的"4×AC-3 stereo → 8 声道"布局正是
  `Demo/docs/linux-sdk-multichannel.html` §1.5 的重排场景；44.1 kHz 源须先重采样到 48 kHz，
  且 15360 字节/次的写入须拆成 10 ms 帧后再送 `sendAudioPcmData`。

---

## 4. 与现行文档的对应关系

| 沟通记录内容 | 现行位置 |
| --- | --- |
| 4×stereo 块序 → 8 声道逐样本交错的重排 | `Demo/docs/linux-sdk-multichannel.html` §1.5；`Spec/linux_sdk_audio_multichannel_spec.md` |
| 最终确认的映射 CH1=T1L…CH8=T4R | 同上 |
| 44.1 kHz 源重采样到 48 kHz、大写入拆 10 ms 帧 | 同上 §1.5 注意事项 |

---

## 附录 A：沟通记录2025.txt 全文

（原文中的客户文件名已按脱敏约定替换为 `customer_order.xlsx`，其余逐字保留。）

```text
Alan 2025-4-7 15:16
我问客户研发，先让客户提供Linux 集成代码吧

Alan 2025-4-7 15:16
因为客户的集成代码，是参考我们给的代码。

Alan 2025-4-7 15:17
客户的8声道，不是7.1声道，是0,1一个左右声道，2，3一个左右声道，4，5一个左右声道，6，7一个左右声道这样子。

lqk 2025-4-7 15:18
需要听一下的，有空提供一下音频吧

Alan 2025-4-7 15:20
@lqk  如果你方便的时候，只要订阅这个频道就可以：   AppId : aab8b8f5a8cd4469a63042fcfafe7063 Channel Name: 142651995438 no token needed.

Alan 2025-4-7 15:22
"lqk： 需要听一下的，有空提供一下音频吧" ------ @lqk  dump，我现在也尝试一下

Alan 2025-4-8 10:22
[AGORA_sample.txt]

Alan 2025-4-8 10:22
[output.txt]

Alan 2025-4-8 10:23
[图片]

lqk 2025-4-8 10:48
另外我上面已经说了很多次了，希望能拿到具体的音频，看下杂音到底是什么杂音？ 所以听不清到底一点都听不清，完全杂乱的噪声，还是能听到有人说话。

Alan 2025-4-8 10:56
"lqk： 另外我上面已经说了很多次了，希望能拿到具体的音频，看下杂音到底是什么杂音？ 所以听不清到底一点都听不清，完全杂乱的噪声，还是能听到有人说话。" ------ @lqk  这个可以提供录音的。

Alan 2025-4-8 10:57
看起来是跟web sdk api关联不大，我看怎么来排查判断问题发生点。

lqk 2025-4-8 10:58
[图片] 用户每条track的长度是不是确定严格一致的，每条track的声道数是不是严格2声道，我看他代码都是在这个假设的基础上的。

lqk 2025-4-8 10:59
但是从他下面的代码有看出他的track可能有一个channel，所以这两个可能有点矛盾

Alan 2025-4-8 10:59
看起来有点像，我找客户研发问清楚

Alan 2025-4-8 11:00
最好我让他给一个能每次复现的demo吧

lqk 2025-4-8 11:00
把他的原始音频拿过来听听

Alan 2025-4-8 11:00
好

Alan 2025-4-8 11:49
  这个是ios app订阅的效果。

Alan 2025-4-8 11:49
[far_in_48000hz_2ch.wav]

lqk 2025-4-8 11:56
听起来有可能是源数据多声道是交织存储的LRLRLRLRLRLRLRLR ，但是他读取的时候按照LLLLLLLL RRRRRRRR的

Alan 2025-4-8 12:02
好，下午我跟客户研发语音开会对一下吧

Alan 2025-4-8 14:11
@lqk [图片]

Alan 2025-4-8 14:11
我让客户再测试一下

Alan 2025-4-8 14:11
[VictoriaMulti.mkv]

Alan 2025-4-8 14:12
"Alan： [VictoriaMulti.mkv]" ------ @lqk  这个是测试源文件audio file.

lqk 2025-4-8 14:16
"Alan： [VictoriaMulti.mkv]" ------ @Alan  他这个不是直接读取的吧，转成pcm再读取的吧

Alan 2025-4-8 14:18
应该有中转，听客户研发说，是shared memory.

Alan 2025-4-8 14:18
我让他们重新确认audio channel layout

lqk 2025-4-8 14:18
都是双声道的，但是duration是有区别的，不过这个可能影响不大，重点排查交织存储的问题吧 交织存储的LRLRLRLRLRLRLRLR ，但是他读取的时候按照LLLLLLLL RRRRRRRR的 [图片]

Alan 2025-4-8 14:19
"lqk： 都是双声道的，但是duration是有区别的，不过这个可能影响不大，重点排查交织存储的问题吧 交织存储的LRLRLRLRLRLRLRLR ，但是他读取的时候按照LLLLLLLL RRRRRRRR的 [图片]" ------ @lqk  估计是你说的原因。

lqk 2025-4-8 14:19
"Alan： 应该有中转，听客户研发说，是shared memory." ------ @Alan  我意思是你发的不是他在server读取的内容，他在server上应该不会直接读取.mkv

Alan 2025-4-8 14:19
等客户研发这边重新验证一下。

Alan 2025-4-8 14:19
嗯，理解。不过这次是他们纯测试的代码。

Alan 2025-4-9 13:33
[customer_order.xlsx]

Alan 2025-4-9 13:34
[图片]

Alan 2025-4-9 13:35
@lqk  这个是客户研发，给的音频layout，

Alan 2025-4-9 13:37
[Agora_Order_Expectation.xlsx]

Alan 2025-4-14 13:37
@lqk  问题解决，感谢技术支持！ 确实是发送端audio layout问题。

Alan 2025-4-14 13:38
[图片]
```
