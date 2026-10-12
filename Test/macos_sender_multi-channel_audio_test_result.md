# macOS SDK 音频多声道（multiopus）测试结果汇总

> 汇总日期：2026-10-12
> 测试范围：AgoraRtcEngine_macOS 公开版 4.5.2 / 4.5.3 / 4.6.0 / 4.6.2 / 4.7.0（CocoaPods）的
> 多声道**发送**与**接收**能力；发送链路验证同时覆盖 macOS→Web 与 macOS→macOS 两种接收端。
> 结论一句话：**发送能力自 4.5.3 起可用（4.5.2 存在 2 声道钳制）；接收能力自 4.5.2 起已验证可用。**

## 1. 结论总表

| AgoraRtcEngine_macOS | 发送多声道（自定义音轨 push PCM） | 接收/订阅播放多声道 |
| --- | --- | --- |
| 4.5.2 | ✗ 协商正常但输入被钳制为 2 声道（仅 CH0/CH1 有内容，CH2-7 数字静音） | ✅ 8ch 解码播放、逐声道控制、dump 八声道齐全，含 3/5/7 奇数声道（2026-10-10 验证，§5.1） |
| 4.5.3 | ✅ 8ch 全通（**修复点**，§3.2） | 本轮未单独回归 Native 接收（接收链路已在 4.5.2 与 4.7.0 两点验证，见 §5） |
| 4.6.0 | ✅ 8ch 全通 | 同上 |
| 4.6.2 | ✅ 8ch 全通 | 同上 |
| 4.7.0 | ✅ 8ch 全通；4ch/6ch 素材同验通过（§3.3） | ✅ Web 端 + macOS 原生 dump 双重验证，声道顺序 1:1（§4） |

- **发送可用起点：4.5.3**。4.5.2 存在发送侧 2 声道钳制（SDK 侧缺口，客户端无绕行手段）；
  修复点精确定位为 4.5.2 → 4.5.3——macOS 在两者之间无其他公开版本
  （CocoaPods 序列：4.5.2 → 4.5.3 → 4.6.0 → 4.6.2 → 4.7.0），4.5.2 实测异常、4.5.3 实测正常。
- **接收可用起点：4.5.2**（2026-10-10 首个受测版本即通过；Native 接收侧多声道解码
  在公开版上始终正常，与发送侧钳制无关）。
- macOS 版 SDK **不存在 4.6.7**：CocoaPods（`AgoraRtcEngine_macOS` / `ShengwangRtcEngine_macOS`）
  与声网 macOS 发版说明中均无此版本（iOS 侧另有 4.6.4）。

## 2. 测试环境与方法

### 2.1 链路与角色

- 发送端：`Demo/macOS/MultiChannelAudioSender`（broadcaster 角色，`createCustomAudioTrack(.mixable)`
  + `pushExternalAudioFrameRawData`，10 ms/帧 pacer 循环推送；join 前设置私参
  `che.audio.custom_payload_type` / `custom_channel_num` / `custom_sample_rate`）。
- 接收验证端（二者可同频道并行）：
  - Web 验证 demo（`Demo/web`，agora-rtc-sdk-ng 4.20.1-200，`ENABLE_MULTIOPUS_CODEC` 开启），
    Chrome 订阅后读 `getVolumeLevels()` 逐声道音量 + getStats codec；
  - macOS 接收端 `Demo/macOS/MultiChannelAudioReceiver`（观众角色，开 `che.audio.frame_dump`
    落盘解码后 PCM，供离线逐声道核对）。
- 凭证：App ID `43f818097c124394821bd44598b7ca9d`（无证书，token 留空）；
  发送 uid 固定 12345，Web uid 54321，receiver uid 0（自动分配）。
- 自动化：sender 支持 `-autoChannel/-autoUid/-autoWav/-autoJoin/-autoPublish/-autoQuitAfter`，
  receiver 支持 `-autoChannel/-autoJoin/-autoDump/-autoQuitAfter`（autoDump 即 join 时开启 frame dump），
  两端日志均无缓冲镜像 stdout；Web 端经 Chrome DevTools 协议驱动点击与采样。

### 2.2 素材基准（Channel ID WAV，48 kHz / 16bit，逐声道轮播）

| 素材 | 声道 | 循环时长 | 各声道内容出现时刻（10 ms 包络峰值 -14~-21 dB） | 静音段形态 |
| --- | --- | --- | --- | --- |
| `8_Channel_ID_signed_16_bit_pcm.wav` | 8 | 14.0 s | CH0@0.24 / CH2@1.91 / CH1@3.44 / CH7@5.07 / CH5@6.86 / CH4@8.94 / CH6@10.73 / CH3@13.14 s | -84 dB dither |
| `6_Channel_ID_48k.wav` | 6 | 5.83 s | CH0@0.53 / CH1@1.22 / CH2@2.21 / CH4@3.02 / CH5@3.92 / CH3@5.47 s | 数字零 |
| `4_Channel_ID_48k.wav` | 4 | 5.83 s | CH0@0.53 / CH1@1.22 / CH2@2.21 / CH3@5.47 s | 数字零 |

以上为本机对素材文件直接分析（10 ms 包络）得到的基准，是后文判定逐声道内容的依据。

### 2.3 判定标准

- Web 端订阅后连续采样取各声道**峰值**：8ch 用例采 20 s、4/6ch 用例采 15 s，均覆盖超过 1 个素材循环。
- 声道判定「有内容」= 出现内容级峰值（≈56~85%，对应素材 -14~-44 dB 内容经 OPUSMC 有损编码）。
- 静默读数分两种、可明确区分：8ch 素材静音段为 dither 底噪 ~10%（≈ -90 dB，属正常）；
  4/6ch 素材静音段为数字零 0%。而被钳制丢失的声道同样呈数字零 0% 且**全程无内容脉冲**——
  对 8ch 素材，0% 与 ~10% 的差异即可判定；对 4/6ch 素材，判定依据为「每声道是否出现内容峰值」。
- 原生链路补充证据：receiver 的 per-user dump（48 kHz / N 声道 / 16bit WAV）做逐声道 RMS 与
  10 ms 包络归一化互相关（NCC）分析。

## 3. 发送侧版本回归（2026-10-12，macOS sender → Web）

每个版本一轮：Podfile 切版本 → `pod install` → 构建 → sender 命令行自动化发布 → Web 端采样判定。
各版本独立频道：4.5.2 对照 `mc452ctl`、4.5.3 `mc453`、4.6.0 `mc460`、4.6.2 `mc462`、4.7.0 `mc470`。

### 3.1 对照组：4.5.2（复现既有结论，验证测试链路有效）

- 协商：`codec: multiopus (8 声道)`，信令 channelCount=8 —— 正常；
- 20 s 各声道峰值：**[65, 65, 0, 0, 0, 0, 0, 0]** —— CH0/CH1 有内容且数值雷同，CH2-7 恒为数字零，
  与 2026-10-10 的问题记录（§6）一致；
- 截图：`Test/web_recv_8ch_from_macos_452_control_20261012.png`。

### 3.2 8ch 回归：4.5.3 / 4.6.0 / 4.6.2 / 4.7.0 全部通过

| 版本 | 协商 | 20 s 各声道峰值（%） | 判定 |
| --- | --- | --- | --- |
| 4.5.3 | multiopus 8ch ✓ | [79, 79, 74, 83, 77, 78, 78, 75] | ✅ 8 声道全通 |
| 4.6.0 | multiopus 8ch ✓ | [79, 77, 78, 84, 78, 79, 80, 75] | ✅ 8 声道全通 |
| 4.6.2 | multiopus 8ch ✓ | [79, 80, 75, 85, 78, 79, 80, 78] | ✅ 8 声道全通 |
| 4.7.0 | multiopus 8ch ✓ | [83, 79, 74, 83, 79, 71, 80, 81] | ✅ 8 声道全通 |

- 四个版本的秒级时间线均呈「内容峰值按序出现在不同声道」的轮播特征，与素材基准（§2.2）吻合；
  静默声道读数 ~10%（dither 底噪），与 4.5.2 对照组的精确数字零形成可明确区分的对照。
- 发送端日志：各版本 join 正常，发布全程无 `pushExternalAudioFrameRawData` 报错。
- **修复点定位**：4.5.2 ✗、4.5.3 ✓，且两者之间 macOS 无其他公开版本，故修复点精确为 **4.5.3**。
  4.5.3 的被测二进制经双重核实：Podfile.lock 与打包 framework 的 `CFBundleShortVersionString` 均为 4.5.3。
- 截图：`Test/web_recv_8ch_from_macos_{453,460,462,470}_20261012.png`。

### 3.3 4.7.0 的 4ch / 6ch 素材补测

4.5.2 时代对 4ch/6ch 仅验证过协商正常（内容同样只有前 2 声道），本次在 4.7.0 上补全内容用例
（频道 `mc4704` / `mc4706`，15 s 采样）：

| 素材 | 协商 | 各声道峰值（%） | 判定 |
| --- | --- | --- | --- |
| 4ch（payload 123） | multiopus 4ch ✓ | [78, 78, 79, 84] | ✅ 全 4 声道有内容 |
| 6ch（payload 124） | multiopus 6ch ✓ | [76, 77, 76, 84, 78, 80] | ✅ 全 6 声道有内容 |

- 秒级时间线呈逐声道轮播，与 4/6ch 素材基准（§2.2）吻合；这两个素材静音段为数字零，
  故静默声道读数为 0% 而非 8ch 素材的 ~10%（差异源于素材，与 SDK 行为无关）。
- 截图：`Test/web_recv_4ch_from_macos_470_20261012.png`、`Test/web_recv_6ch_from_macos_470_20261012.png`。

## 4. 双端 4.7.0 原生互通（2026-10-12，macOS sender → macOS receiver）

发送侧修复后，首次具备原生↔原生多声道互通验证条件。

### 4.1 设置

- sender 4.7.0 发布 8ch 素材（频道 `mcn470`，uid 12345）；receiver 4.7.0 观众加入
  （uid 3517625381）并开启 frame dump；同频道 Web 端并行订阅复测。
- 两个 app 的 Podfile 均为 AgoraRtcEngine_macOS 4.7.0，构建通过；
  receiver 日志显示正常 join、识别 host 12345 发布音频、8 行声道音量私参（100%）全部下发成功。

### 4.2 dump 实证：八声道齐全，声道顺序 1:1

dump 文件 `~/Library/Logs/audio_dump_playback_before_mixed_0_2026_10_12_112900_12345.wav`
（48 kHz / 8ch / 16bit，134 s；位置与命名规则与 4.5.2 一致）：

- **八声道全部携带内容**：各声道整段最大 RMS 为 CH0=-26.7 / CH1=-28.3 / CH2=-27.9 / CH3=-20.9 /
  CH4=-29.0 / CH5=-29.0 / CH6=-26.5 / CH7=-27.4 dB（底噪 ≈ -89.6 dB）；
- **声道顺序 1:1 保持、无重排**：dump 与源 WAV 做 10 ms 包络归一化互相关（NCC，循环相位对齐后），
  dumpCHi 与同序号 srcCHi 的 NCC = 0.67~0.78，与其他源声道 NCC ≈ 0（≤0.04）。
  即该链路不存在 Vorbis 式的声道重排，接收端声道序号与发送素材声道一一对应。

### 4.3 同场 Web 复测

同频道订阅 uid 12345：codec=multiopus 8ch，20 s 峰值 [80, 80, 80, 85, 79, 79, 80, 78]，
与 §3.2 的 4.7.0 单测一致。截图 `Test/web_recv_8ch_from_macos_470_native_session_20261012.png`。

## 5. 接收能力验证

### 5.1 4.5.2（2026-10-10）

`Demo/macOS/MultiChannelAudioReceiver`（AgoraRtcEngine_macOS 4.5.2，观众角色）订阅 AWS 侧
`sample_send_mkv_multiopus` 发布的 8 声道 OPUSMC + H264 流（频道 `mkvav1010a`）：

- 接收码率 ~262 kbps，落在 8 声道 OPUSMC 预期区间（250~280 kbps）；
- per-user dump 为 48 kHz / **8 声道**，八声道 RMS -13.6~-15.5 dB 各不相同，无串声道、无丢声道；
  mixer 输出恒 2 声道；
- 逐声道开关/音量（`che.audio.neteq.adjust_decoded_volume`）下发即生效
  （置 0 后 dump 对应声道数字静音，恢复后 ~1 s 回到原电平）；
- Native 接收侧自适应解码含 3/5/7 奇数声道（Web 端仅支持 4/6/8 偶数声道）。

### 5.2 4.7.0（2026-10-12）

即 §4 的原生互通验证：接收、解码、逐声道私参下发与 dump 全链路通过，dump 八声道齐全且
声道顺序 1:1。4.5.3 / 4.6.0 / 4.6.2 未单独回归 Native 接收（发送回归中 Web 端解码均正常）。

## 6. 4.5.2 发送侧问题回顾（已闭环）

- 现象：协商正常（对端解出对应声道数 multiopus 流），但仅前 2 声道携带内容，CH2 起静音。
- 已排除的客户端因素：demo pacer bug（已修）、`samples` 参数语义（总样本数，与 Linux 每声道
  语义不同）、raw PCM / CMSampleBuffer 两种推入路径、mixable/direct 音轨类型、
  `setExternalAudioSource(channels: 8)`、私参 `che.audio.input_channels: 8`（均返回 0 但不生效）。
- 结论：SDK 侧输入管线钳制，客户端无可行绕行；**4.5.3 起 SDK 已解除该钳制**（§3.2）。
- 备注：2026-10-10 记录中曾猜测「Linux 定制版的发送侧改动未同步 macOS 分支」。本轮回归数据
  只支持「4.5.3 起能力可用」这一事实；具体修复合入路径未获证实，留待 SDK 研发说明。

## 7. 遗留项状态

本轮回归开始时记录的三个遗留项已全部闭环：

1. ~~4.5.3 未测~~ → 已测，修复点定位为 4.5.2 → 4.5.3（§3.2）。
2. ~~声道序号映射待核对~~ → 已闭环：4.7.0 原生链路 dump 与源 WAV 包络互相关证实逐声道同序号
   对应（NCC 0.67~0.78，跨声道 ≈ 0），无重排（§4.2）。
3. ~~4/6ch 素材未在新版本测内容~~ → 已在 4.7.0 补测通过（§3.3）。

回归工具链已就绪：sender/receiver 命令行自动化 + Web 端采样脚本，单个版本一轮约 5 分钟
（含 pod install 下载）。

## 8. 证据文件索引

| 文件 | 内容 |
| --- | --- |
| `Test/macos_sender_2ch_clamp_issue_20261010.md` | 4.5.2 发送侧钳制问题记录与排查过程 |
| `Test/TestResults_20261010_macos.md` | 4.5.2 接收端 8ch 全量验证 + 发送端初测 |
| `Test/macos_receiver_8ch_verified_20261010.png` 等 | 4.5.2 接收端验证截图组 |
| `Test/web_recv_8ch_from_macos_452_control_20261012.png` | 4.5.2 对照：CH0/CH1 有内容、CH2-7 为 0 |
| `Test/web_recv_8ch_from_macos_453_20261012.png` | 4.5.3：multiopus 8ch，声道内容轮播（修复点定位） |
| `Test/web_recv_8ch_from_macos_460_20261012.png` | 4.6.0：multiopus 8ch，声道内容轮播 |
| `Test/web_recv_8ch_from_macos_462_20261012.png` | 4.6.2：同上 |
| `Test/web_recv_8ch_from_macos_470_20261012.png` | 4.7.0：同上 |
| `Test/web_recv_4ch_from_macos_470_20261012.png` | 4.7.0 + 4ch 素材：multiopus 4ch，全声道有内容 |
| `Test/web_recv_6ch_from_macos_470_20261012.png` | 4.7.0 + 6ch 素材：multiopus 6ch，全声道有内容 |
| `Test/web_recv_8ch_from_macos_470_native_session_20261012.png` | 4.7.0↔4.7.0 原生会话同场的 Web 端复测（multiopus 8ch） |
| `~/Library/Logs/audio_dump_playback_before_mixed_0_2026_10_12_112900_12345.wav` | 4.7.0 原生互通 dump（134 s / 8ch，本机日结材料，不入库） |
| `Demo/macOS/MultiChannelAudioSender/README.md` | 版本实测矩阵 + 命令行自动化用法 |
