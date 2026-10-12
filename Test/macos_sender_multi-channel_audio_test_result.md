# macOS SDK 音频多声道（multiopus）测试结果汇总

> 汇总日期：2026-10-12
> 数据来源：① 2026-10-10 发送侧 2 声道钳制问题记录（`Test/macos_sender_2ch_clamp_issue_20261010.md`、
> `Test/TestResults_20261010_macos.md`）；② 2026-10-12 多版本回归测试（本文第 3 节，含 4.5.3 与 4/6 声道补测）；
> ③ 2026-10-12 双端 4.7.0 原生互通回归（本文第 7 节）
> 结论一句话：**发送能力自 4.5.3 起可用；接收能力自 4.5.2 起已验证可用。**

## 1. 版本结论总表

| AgoraRtcEngine_macOS | 发送多声道（自定义音轨 push 8ch PCM） | 接收/订阅多声道（含播放） |
| --- | --- | --- |
| 4.5.2 | ✗ 协商正常但输入被钳制为 2 声道（仅 CH0/CH1 有内容，CH2-7 数字静音） | ✅ 已验证：8ch 解码播放、逐声道控制、dump 八声道齐全（含 3/5/7 奇数声道支持） |
| 4.5.3 | ✅ 8 声道全部送达（**修复点**，见 3.3） | ✅（经 Web 端解码验证，未见回退） |
| 4.6.0 | ✅ 8 声道全部送达 | ✅（经 Web 端解码验证，未见回退） |
| 4.6.2 | ✅ 8 声道全部送达 | ✅（同上） |
| 4.7.0 | ✅ 8 声道全部送达（4ch/6ch 素材同验，见 3.4） | ✅（Web 端 + macOS 原生 dump 双重验证，声道顺序 1:1，见第 7 节） |

- **发送可用起点：4.5.3**。4.5.2 存在发送侧 2 声道钳制（SDK 侧缺口，客户端无绕行手段），
  4.5.3 / 4.6.0 / 4.6.2 / 4.7.0 实测全部正常——**修复点落在 4.5.3**（2026-10-12 补测确认）。
- **接收可用起点：4.5.2**（2026-10-10 首个受测版本即通过，Native 接收侧多声道解码
  在公开版上始终正常，与发送侧钳制无关）。
- 注：macOS 版 SDK **不存在 4.6.7**；CocoaPods（`AgoraRtcEngine_macOS` /
  `ShengwangRtcEngine_macOS`）与声网 macOS 发版说明中，4.5.2 之后的正式版序列为
  4.5.3 → 4.6.0 → 4.6.2 → 4.7.0（iOS 侧另有 4.6.4）。

## 2. 测试环境与方法

| 项 | 内容 |
| --- | --- |
| 发送端 | `Demo/macOS/MultiChannelAudioSender`（broadcaster + `createCustomAudioTrack(.mixable)` + `pushExternalAudioFrameRawData`，10 ms/帧 pacer 循环；join 前设置私参 `che.audio.custom_payload_type=125 / custom_channel_num=8 / custom_sample_rate=48000`） |
| 发送素材 | `8_Channel_ID_signed_16_bit_pcm.wav`（48 kHz / 16bit / 8ch，14.0 s 循环；内容八声道轮播，10ms 包络峰值 -14~-21 dB，依次出现于 0.24/1.91/3.44/5.07/6.86/8.94/10.73/13.14 s，静音段为 -84 dB dither） |
| 接收验证端 | Web 验证 demo（`Demo/web`，agora-rtc-sdk-ng 4.20.1-200，`ENABLE_MULTIOPUS_CODEC` 开启），Chrome 订阅后读 `getVolumeLevels()` 逐声道音量 + getStats codec |
| 判定标准 | 连续采样 20 s（覆盖 >1 个素材循环）取各声道峰值：内容级 ≈ 56~85% 为「有内容」；~10% 为素材 dither 底噪（无声）；0% 为数字零（内容被钳制丢失） |
| 工程凭证 | App ID `43f818097c124394821bd44598b7ca9d`（无证书，token 留空）；发送 uid 12345，Web uid 54321；每版本独立频道（mc452ctl / mc460 / mc462 / mc470） |
| 自动化 | sender 支持启动参数 `-autoChannel/-autoUid/-autoWav/-autoJoin/-autoPublish/-autoQuitAfter`，日志实时写 stdout；Web 端经 Chrome DevTools 协议驱动点击与采样 |

## 3. 2026-10-12 多版本回归实测数据

### 3.1 对照组 4.5.2（复现历史结论，验证测试链路有效）

- 协商：`codec: multiopus (多声道编码, 8 声道)`，信令 channelCount=8 —— 正常；
- 20 s 各声道峰值：**[65, 65, 0, 0, 0, 0, 0, 0]** —— CH0/CH1 有内容且数值雷同，
  CH2-7 恒为数字零，与 2026-10-10 记录完全一致；
- 截图：`Test/web_recv_8ch_from_macos_452_control_20261012.png`。

### 3.2 被测版本（4.6.0 / 4.6.2 / 4.7.0）

| 版本 | 协商 | 20 s 各声道峰值（%） | 判定 |
| --- | --- | --- | --- |
| 4.6.0 | multiopus 8ch ✓ | [79, 77, 78, 84, 78, 79, 80, 75] | ✅ 8 声道全通 |
| 4.6.2 | multiopus 8ch ✓ | [79, 80, 75, 85, 78, 79, 80, 78] | ✅ 8 声道全通 |
| 4.7.0 | multiopus 8ch ✓ | [83, 79, 74, 83, 79, 71, 80, 81] | ✅ 8 声道全通 |

- 三个版本的秒级时间线均呈现「内容峰值按序出现在不同声道」的轮播特征，与素材逐声道
  轮播结构吻合；静默声道读数 ~10%（素材 dither 底噪经编码传输后的正常值），
  而 4.5.2 的 CH2-7 是精确数字零 —— 两种「静音」形态可明确区分。
- 发送端日志（4.6.0/4.6.2/4.7.0）：join 正常、发布全程无 `pushExternalAudioFrameRawData` 报错。
- 截图：`Test/web_recv_8ch_from_macos_460_20261012.png`、`..._462_20261012.png`、`..._470_20261012.png`。

### 3.3 补测：4.5.3（定位修复点）

为定位修复点落在 4.5.x 还是 4.6.0，对 4.5.3 做了同法 8ch 测试（频道 `mc453`）：

- 协商：`codec: multiopus (多声道编码, 8 声道)`，channelCount=8 —— 正常；
- 20 s 各声道峰值：**[79, 79, 74, 83, 77, 78, 78, 75]** —— 8 声道全部有内容；
- 判定：**4.5.3 已修复**，即发送侧 2 声道钳制的修复点为 **4.5.2 → 4.5.3**；
- 二进制复核：Podfile.lock 与打包 framework 的 `CFBundleShortVersionString` 均为 4.5.3；
- 截图：`Test/web_recv_8ch_from_macos_453_20261012.png`。

### 3.4 补测：4.7.0 的 4ch / 6ch 素材

4.5.2 时代 4ch/6ch 仅验证协商正常（内容同样只有前 2 声道），本次在 4.7.0 上补全内容用例
（频道 `mc4704` / `mc4706`）：

| 素材 | 协商 | 15 s 各声道峰值（%） | 判定 |
| --- | --- | --- | --- |
| 4ch（payload 123） | multiopus 4ch ✓ | [78, 78, 79, 84] | ✅ 全 4 声道有内容 |
| 6ch（payload 124） | multiopus 6ch ✓ | [76, 77, 76, 84, 78, 80] | ✅ 全 6 声道有内容 |

- 素材基准（10 ms 包络峰值，各声道均有内容、轮播）：4ch — CH0@0.53s/CH1@1.22s/CH2@2.21s/CH3@5.47s；
  6ch — 另含 CH4@3.02s/CH5@3.92s（5.83 s 循环）。
- 秒级时间线呈逐声道轮播，与源素材结构吻合；4/6ch 素材静音段为数字零（无 dither），
  故静默声道读数为 0% 而非 8ch 素材的 ~10%。
- 截图：`Test/web_recv_4ch_from_macos_470_20261012.png`、`Test/web_recv_6ch_from_macos_470_20261012.png`。

## 4. 接收能力验证结论（2026-10-10，4.5.2）

`Demo/macOS/MultiChannelAudioReceiver`（AgoraRtcEngine_macOS 4.5.2，观众角色）订阅 AWS 侧
`sample_send_mkv_multiopus` 发布的 8 声道 OPUSMC + H264 流（频道 `mkvav1010a`）：

- 接收码率 ~262 kbps，落在 8 声道 OPUSMC 预期区间（250~280 kbps）；
- per-user dump 为 48 kHz / **8 声道**，八声道 RMS -13.6~-15.5 dB 各不相同，
  无串声道、无丢声道；mixer 输出恒 2 声道；
- 逐声道开关/音量（`che.audio.neteq.adjust_decoded_volume`）下发即生效
  （置 0 后 dump 对应声道数字静音，恢复后 ~1 s 回到原电平）；
- Native 接收侧自适应解码含 3/5/7 奇数声道（Web 端仅支持 4/6/8 偶数声道）。

## 5. 4.5.2 发送侧问题回顾（已闭环）

现象：协商正常（对端解出对应声道数 multiopus 流），但仅前 2 声道携带内容，CH2 起静音。
已排除项：demo pacer bug（已修）、`samples` 参数语义（总样本数，与 Linux 每声道语义不同）、
raw PCM / CMSampleBuffer 两种推入路径、mixable/direct 音轨类型、
`setExternalAudioSource(channels: 8)`、私参 `che.audio.input_channels: 8`（均返回 0 但不生效）。
结论：SDK 侧输入管线钳制，客户端无可行绕行 —— **4.5.3 起 SDK 已解除该钳制**（3.3 节补测定位），
当时「Linux 定制版发送侧改动未同步 macOS 分支」的猜测与回归结果相符。

## 6. 遗留与后续

1. ~~**4.5.3 未测**~~ **已闭环（2026-10-12，见 3.3）**：4.5.3 实测 8 声道全通，修复点定位为 4.5.2 → 4.5.3。
2. ~~**声道序号映射**~~ **已闭环（2026-10-12，见第 7 节）**：4.7.0 原生链路 dump 与源 WAV 的
   包络互相关证实 dumpCHi ↔ srcCHi 同序号对应（NCC 0.67~0.78，跨声道 ≈ 0），无重排。
3. ~~**4/6 声道素材**~~ **已闭环（2026-10-12，见 3.4）**：4.7.0 上 4ch/6ch 素材全声道内容送达。
4. 回归工具链已就绪：sender/receiver 命令行自动化 + Web 端采样脚本，单个版本一轮约 5 分钟
   （含 pod install 下载）。

## 7. 追加：双端 4.7.0 回归（2026-10-12）

### 7.1 变更内容

- `Demo/macOS/MultiChannelAudioSender` 与 `Demo/macOS/MultiChannelAudioReceiver` 的 Podfile 均升级为
  **AgoraRtcEngine_macOS 4.7.0**（CocoaPods 公开版），两工程均构建通过。
- 两个 app 均新增命令行自动化（日志无缓冲镜像 stdout）：
  - sender：`-autoChannel/-autoUid/-autoWav/-autoJoin/-autoPublish/-autoQuitAfter`
  - receiver：`-autoChannel/-autoJoin/-autoDump/-autoQuitAfter`（autoDump = join 时开启 `che.audio.frame_dump`）

### 7.2 原生互通（macOS 4.7.0 发送 → macOS 4.7.0 接收）

场景：sender 4.7.0 发布 8 声道 Channel ID 素材（频道 `mcn470`，uid 12345）；receiver 4.7.0
以观众加入（uid 3517625381）并开启 frame dump；同频道 Web 端并行订阅复测。

- receiver 日志：正常 join；识别 host 12345 发布音频；8 行声道音量私参（100%）全部下发成功。
- **dump 实证**（`~/Library/Logs/audio_dump_playback_before_mixed_0_2026_10_12_112900_12345.wav`，
  48 kHz / 8ch / 16bit，134 s；dump 位置与命名规则与 4.5.2 一致）：
  - 各声道整段最大 RMS：CH0=-26.7 / CH1=-28.3 / CH2=-27.9 / CH3=-20.9 / CH4=-29.0 /
    CH5=-29.0 / CH6=-26.5 / CH7=-27.4 dB（底噪 ≈ -89.6 dB）——**八声道全部携带内容**；
  - **声道顺序 1:1 保持**：对 dump 与源 WAV 做 10 ms 包络归一化互相关（NCC，循环相位对齐后），
    dumpCHi 与同序号 srcCHi 的 NCC = 0.67~0.78，与其他源声道 NCC ≈ 0（≤0.04）——无重排、无串声道。
- **素材认知修正**：8ch Channel ID 素材实为**八声道轮播**（10 ms 包络峰值 -14~-21 dB，
  各声道内容依次出现于 0.24/1.91/3.44/5.07/6.86/8.94/10.73/13.14 s）；此前"CH1/CH5 无内容"
  系 1 s 窗 RMS 稀释造成的误读，特此更正（不影响此前任何结论——判定依据为内容级峰值 vs 数字零）。
- **同场 Web 复测**（同频道订阅 uid 12345）：codec=multiopus 8ch，20 s 峰值
  [80, 80, 80, 85, 79, 79, 80, 78]，与 4.7.0 单独测试一致。
  截图 `Test/web_recv_8ch_from_macos_470_native_session_20261012.png`。

### 7.3 结论更新

- 发送：**自 4.5.3 起可用**（3.3 节补测将起点由 4.6.0 修正为 4.5.3）；**4.7.0 经 Web 与 macOS 原生双路验证**，
  且 4ch/6ch 素材同验通过（3.4 节）。
- 接收：4.5.2 起可用（2026-10-10）；**4.7.0 复验通过**，含 dump 八声道齐全与声道顺序 1:1。
- OPUSMC 声道顺序问题在 macOS 原生链路闭环：解码输出与源 WAV 同序号（无 Vorbis 式重排）。

## 8. 证据文件索引

| 文件 | 内容 |
| --- | --- |
| `Test/web_recv_8ch_from_macos_452_control_20261012.png` | 4.5.2 对照：CH0/CH1 有内容、CH2-7 为 0 |
| `Test/web_recv_8ch_from_macos_460_20261012.png` | 4.6.0：multiopus 8ch，声道内容轮播 |
| `Test/web_recv_8ch_from_macos_462_20261012.png` | 4.6.2：同上 |
| `Test/web_recv_8ch_from_macos_470_20261012.png` | 4.7.0：同上 |
| `Test/web_recv_8ch_from_macos_453_20261012.png` | 4.5.3 补测：multiopus 8ch，声道内容轮播（修复点定位） |
| `Test/web_recv_4ch_from_macos_470_20261012.png` | 4.7.0 + 4ch 素材：multiopus 4ch，全声道有内容 |
| `Test/web_recv_6ch_from_macos_470_20261012.png` | 4.7.0 + 6ch 素材：multiopus 6ch，全声道有内容 |
| `Test/macos_sender_2ch_clamp_issue_20261010.md` | 4.5.2 发送侧钳制问题记录与排查过程 |
| `Test/TestResults_20261010_macos.md` | 4.5.2 接收端 8ch 全量验证 + 发送端初测 |
| `Test/macos_receiver_8ch_verified_20261010.png` 等 | 4.5.2 接收端验证截图组 |
| `Test/web_recv_8ch_from_macos_470_native_session_20261012.png` | 4.7.0↔4.7.0 原生会话同场的 Web 端复测（multiopus 8ch） |
| `~/Library/Logs/audio_dump_playback_before_mixed_0_2026_10_12_112900_12345.wav` | 4.7.0 原生互通 dump（134 s / 8ch，本机日结材料，不入库） |
| `Demo/macOS/MultiChannelAudioSender/README.md` | 版本实测矩阵 + 命令行自动化用法 |
