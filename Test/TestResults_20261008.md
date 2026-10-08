# 多声道音频测试报告（2026-10-08）

> 覆盖链路：Linux→Web、Web→Web、Web→macOS App（Native），以及为定位问题所做的
> Linux→Linux 对照验证。问题详录见 `Test/web_volume_meter_floor_linux2web.md`
> （English: `web_volume_meter_floor_linux2web_en.md`）。

## 1. 测试环境

| 项 | 值 |
| --- | --- |
| App ID | `43f818097c124394821bd44598b7ca9d`（项目 "Test 128 Hosts Env"，无证书，Token 留空） |
| 频道 | `multichannel8ch` |
| Argus callId | `6ac74e1c33d2695a000022b8` |
| Linux 发送/接收 | AWS 测试服务器（54.152.163.4，弗吉尼亚），Docker 镜像 `agora-multichannel-linux`，Linux SDK 4.4.30.149，payload 125 / 8ch / 48 kHz |
| Web 端 | https://xiaoyingtao.convoai.club/multiopus/ （Live Demo 1 / 2），agora-rtc-sdk-ng 4.20.1-200，Chrome 154，上海侧 |
| macOS 端 | `Demo/macOS/` MultiChannelAudioReceiver，AgoraRtcEngine_macOS 4.5.2（CocoaPods），本日构建 |
| 素材 | 8ch Channel ID：`8_Channel_ID_signed_16_bit_pcm.wav`（16bit，Linux 发送）/ `8_Channel_ID.wav`（24bit，Web 发布）/ `8_Channel_ID_24bit.wav`（24bit，对照实验）/ `8ch_2.opus`（预编码直通） |

## 2. 测试结论总表

| # | 链路 | 发送端 | 接收端 | 测试点 | 结果 | 关键证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Linux→Web | WAV sample（uid 8888，8ch OPUSMC） | Live Demo 2（uid 433911） | 订阅解码、逐声道试听 | **通过** | 解出 8 声道（音量柱 8 条）；逐声道可听辨；听感无串声道 |
| 2 | Linux→Web | 同上 | Live Demo 1（uid 479912，自动化测量） | 同上 + 传输质量 | **通过** | Argus：接收 0% 丢包、~256 kbps 稳定、播放延迟 56~172ms |
| 3 | Linux→Web | PCM sample（uid 8889） | Live Demo 1 | 订阅解码 | **通过** | 8 声道解码正常，音量随报号轮询起伏 |
| 4 | Linux→Web | opus8 预编码 sample（uid 8890） | Live Demo 1 | 编码帧直通路径 | **通过** | 8 声道解码正常（素材全声道同时有声，各柱 32~91% 随内容起伏） |
| 5 | Linux→Web | WAV sample + 24bit 素材（uid 8891） | Live Demo 1 | 素材静音底对照实验 | **通过（定位根因）** | 静音声道音量表 min=0.00%，底数消失（见问题 3） |
| 6 | Linux→Linux | WAV sample（uid 8888） | PCM 接收 sample（uid 9999，落盘） | 解码 PCM 逐声道核对 | **通过** | 落盘 25s：静音声道 RMS≈0.003%（数字静音），无泄漏无串声道 |
| 7 | Linux→Linux | WAV / PCM / opus8 三路径 | 编码帧接收 sample（uid 9998） | OPUSMC 编码帧接收 | **通过** | 帧日志 codec=13（OPUSMC）、恒 640B/帧、sendTs 间隔 960 样本（20ms）；opus8 独占频道时 12s 收 592 帧（≈50fps），三路径逐一路测 |
| 8 | Linux→Linux | WAV sample（uid 8888） | 混音接收 sample（uid 9997，落盘） | 混音输出（恒 2 声道，奇/偶声道分路） | **通过** | 落盘 26s：输出 2ch/48k/16bit；任一 0.5s 窗内容只出现在 L 或 R 单侧（另一侧 0.00），随报号轮转交替，分路干净无互串 |
| 9 | Web→Web | Live Demo 1 发布（uid 7777，24bit 素材） | Live Demo 1 订阅（uid 479912） | 双标签页发布/订阅 | **通过** | 8 声道解码；静音声道音量表精确归零；峰值 78~80% |
| 10 | Web→macOS App | Web 发布多声道流 | MultiChannelAudioReceiver（观众角色） | Native 接收、逐声道选择收听/音量 | **通过（用户自测）** | host 列表正确收录发布端；`adjust_decoded_volume` 逐声道生效 |
| 11 | —（macOS 工程） | — | — | app 构建与启动 | **通过** | `xcodebuild` BUILD SUCCEEDED（0 错误 0 警告），启动冒烟正常 |

## 3. 分场景明细

### 3.1 Linux → Web（本日新增验证的链路）

- 发送端三个 sample（WAV / PCM / 预编码 opus8）逐一验证，均正常：8 声道 OPUSMC
  （payload 125，48 kHz）发布成功，Web 端 `getVolumeLevels()` 数组长度 = 8、
  `channelCount` = 8、codec = multiopus。
- 接收质量（Argus，两段 Web 会话）：0% 丢包、码率 ~256 kbps 稳定（符合 8 声道档位）。
- 逐声道试听：报号素材各声道内容可区分，听感无串声道。
- 交叉验证：Linux 接收端落盘解码 PCM 逐声道测量，静音声道为数字静音（0.003% RMS），
  与源素材一致——编码→传输→解码链路无泄漏。
- 接收三路径在 Linux→Linux 下全部覆盖：解码 PCM 回调（#6）、编码帧回调（#7，含 opus8
  直通路径）、混音输出（#8，恒 2 声道奇偶分路，分路干净无互串）。
- 为覆盖混音路径，服务器上的 `agora-multichannel-linux` 镜像已按当前源码重建
  （新增 `sample_receive_mixed_audio` 二进制）。

### 3.2 Web → Web（双标签页自验证）

- 发布端（uid 7777）发布 24bit `8_Channel_ID.wav` 8 声道，订阅端（uid 479912）正常接收：
  8 声道解码、发声声道峰值 78~80%、静音声道音量表精确 0%。
- 发布/订阅链接（Open Publisher / Open Subscriber）用途说明已加到页面（本日 UI 变更 2）。

### 3.3 Web → macOS App（Native）

- 用户自测通过：app 以观众角色加入频道，仅收录真正发布音频的 host（观众不入列），
  逐声道开关/音量经 `che.audio.neteq.adjust_decoded_volume` 生效；兼容普通立体声流。
- 本日完成该 app 的从零构建（SwiftUI + CocoaPods，手写工程文件）、中英双语 UI
  （默认英文）、默认 App ID 预填；构建 0 错误 0 警告，启动冒烟正常。

## 4. 发现的问题与处理

| # | 问题 | 根因 | 处理 | 状态 |
| --- | --- | --- | --- | --- |
| 1 | Live Demo 2 音量条无标签/数值，与 Live Demo 1 不一致 | 沿用参考布局的裸进度条 | 对齐 Live Demo 1（CH 标签 + 百分比），已部署 | 已关闭 |
| 2 | Live Demo 1 发布/订阅链接用途不明 | 缺 UI 说明 | 链接上方加双标签页自验证说明（中英），已部署 | 已关闭 |
| 3 | Linux→Web 静音声道音量柱不归零（~6~10%），web→web 无此现象 | **非 SDK 缺陷**：音量表为 dB 宽量程刻度（`(dBFS+100)/100`，仅数字零显示 0%）；16bit 发送素材静音段含 -84dB dither（不可闻），被忠实显示为 ~10%。24bit 素材对照实验坐实（底数消失）。完整 RCA 见 `Test/web_volume_meter_floor_linux2web.md` | 两页面音量条下加 dB 刻度提示（中英）；FAQ 写入两份 web README | 已关闭（仪表语义提示到位） |

## 5. 本日部署与代码变更

- Web demo 默认 App ID 改为 `43f818097c124394821bd44598b7ca9d`（两个页面组件），已部署。
- Live Demo 1：发布/订阅链接说明、`window.__mcRemoteTrack` 调试钩子、音量表 dB 刻度提示。
- Live Demo 2：音量条 CH 标签 + 百分比、dB 刻度提示。
- 新增 `Demo/macOS/` Native 接收验证 app（含双语 README）；新增 `Spec/native_sdk_audio_multichannel_spec.md`。
- 文档：根 README 更新（目录/快速开始/声道数说明）；全仓 md 去除客户名（开源准备）。

## 6. 遗留与后续

| 项 | 状态 |
| --- | --- |
| 3/5/7 奇数声道端到端验证 | 未测（Native/Linux 设计支持自适应解码；Web 仅偶数声道） |
| 编码器对素材 dither 残余的抑制随会话状态变化的触发条件 | 未隔离（-90dB 级细节，不影响结论） |
| `che.audio.bitrate_mode` / `dtx_mode` / `acm_ptime` 私参语义 | 暂停（需带符号表 SDK 包或研发确认） |
| 服务器发送端容器 `multiopus-sender`（uid 8888） | 仍在运行，供后续复测；停止：`docker stop multiopus-sender` |
| Web SDK 文档建议：注明 `getVolumeLevels()` 为 dB 刻度 | 可选反馈项 |
