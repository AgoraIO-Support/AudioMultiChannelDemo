# Agora 音频多声道测试汇总

- 汇总日期：2026-11-17；最近当前版本回归更新：2026-11-24
- 覆盖时间：2026-10-08～2026-11-17
- 覆盖方向：Web、Linux、macOS、iOS 之间的音频多声道发布、订阅、解码、生命周期、边界与异常处理
- 统计原则：按各文档最新有效结论汇总，不将不同日期、版本和重叠场景的用例数简单相加，不计算一个失真的“全项目通过率”

## 1. 结论摘要

1. **正常 4/6/8 声道主链路已获得较充分的跨端验证。** Web↔Web、Web→Linux、Linux→Linux、Linux→Web、macOS→macOS/Web/Linux，以及 Linux MKV→Web 的 8 声道音频 + H.264 视频均有直接媒体证据。
2. **Linux 与 Native 的 3/5/7 奇数声道互通通过。** Linux↔Linux、Linux→macOS、macOS↔macOS、macOS→Linux 四类链路共 12/12 通过，逐声道内容与源素材同序号对应。
3. **Web 的奇数声道必须区分两个场景。** Web 自定义 3/5/7 声道发布属于 unsupported，可能出现 `publish onSuccess` 但没有有效远端流；订阅 Native/Linux 的 3/5/7 流则会按 N+1 偶数声道误解码并复制末声道，Demo 补丁只能作为过渡规避。
4. **旧版清理问题已有明确闭环。** 4.24.9 的 `InvalidStateError: Stream closed` 在 4.24.9-dev.2 回归中未再出现；Demo 2 的重复退订竞态已修复并定向通过；macOS 4.5.2 的发送侧 2 声道钳制自 4.5.3 起修复。
5. **仍有不能写成“全部通过”的项目。** Web→iOS 的间歇性 1ch 未闭环；2026-11-12 的 Web→macOS 矩阵有 3 项严格失败；多声道选择越界、声道数与素材不匹配、运行中改变声道数等行为仍需要集成层显式处理。
6. **纯音频容量边界已实测到 32/33 路。** 31 个 Linux Podman sender + 1 个 Web sender 共 32 路 4ch 同时上行通过；第 33 路出现 join/publish 成功、本端 RTP 增长但无有效远端媒体，释放一个名额后恢复。
7. **Web `4.24.10` 已完成本轮 P0 真实媒体回归，但仍有集成校验缺口。** 4/6/8ch 的 Web→Web、Web→Linux、Web→macOS/iOS、Linux→Web 均有新媒体证据，停发重发、取消重订、离开和页面卸载通过；`selectChannels` 越界及源/声明不匹配仍不能按通过处理，见第 9 节。

## 2. 版本与测试环境

| 组件 | 覆盖版本/环境 | 结论使用方式 |
| --- | --- | --- |
| Web SDK | 4.20.1-200、4.24.9、4.24.9-dev.2 | 构成主要媒体实测证据；限制是否已在后续主线修复需重新验证 |
| 当前 Web SDK | `agora-rtc-sdk-ng@4.24.10` 主线版 | 2026-11-17 已确认安装、实际版本和生产构建；2026-11-24 已完成第 9 节所列 P0 媒体回归，但全量历史矩阵和两项集成校验仍未闭环 |
| Linux SDK | 当前包为 4.4.30.152；历史媒体回归使用 4.4.30.149，ARM64 Ubuntu/Podman，另有 AWS x86_64 验证 | 4.4.30.152 已完成双架构构建、离线和真实媒体矩阵：ARM64 45/48、x86_64 48/48；NetEq、N/M 和 SIGSEGV 专项见第 9 节 |
| macOS SDK | 4.5.2、4.5.3、4.6.0、4.6.2、4.7.0 | 发送侧修复边界和 Native 接收能力已覆盖 |
| iOS SDK | 4.7.0，iPhone 16 Pro 模拟器，iOS 18.3 | 覆盖 Web→iOS；不等于真机验证 |
| 浏览器 | macOS Chrome 154～157，另有 headless Chrome | Safari、Firefox、iOS WebKit 不在已通过范围内 |
| 素材 | 4/6/8ch、3/5/7ch，48 kHz，16/24bit WAV，预编码 OPUS8，4×stereo MKV | 以完整 Channel ID 循环、PCM dump、编码帧、页面音量和实际输出 PCM 交叉判定 |

## 3. 分链路实测结果

> “历史通过”表示该轮已完成，但不自动等同于当前 4.24.10 的版本回归通过。定向复测通过也不改写成原矩阵全量重跑通过。

| 链路 | 最新有效结果 | 需要保留的边界 |
| --- | --- | --- |
| Web→Web（Demo 1/2） | 2026-11-05 执行 32 项：31 项通过、1 项越界选择限制；4/6/8ch 主链路与双向交叉 8/8 通过。4.24.9-dev.2 的基础 8ch、停发/重发、取消重订及清理回归通过 | SEL-07 的 `[7]` 越界选择会成功返回但输出数字静音；Web 自定义 3/5/7 发布为 unsupported |
| Web→Linux | 2026-11-05 执行 27 项：26 项通过、1 项刻意错配观察；4/6/8ch × PCM/encoded/mixed/AV 基础组合 12/12 通过。4.24.9-dev.2 的 8ch PCM 也通过 | encoded 只验证 OPUSMC 回调，mixed 只验证 1/2ch 混音；刻意错配不能算正常互通 |
| Web→macOS | 2026-10 已有 8ch dump、host 列表、逐声道音量与音视频接收通过证据；完整新 SDK 矩阵未完成 | 2026-11-12 有 3 项严格失败：W-M-08-1/2、W-M-09；W-M-19 部分覆盖，W-M-20 未完整覆盖 |
| Web→iOS | 最新累计口径为 16 项通过、1 项观察、3 项失败；不是 20 项全量重跑。WI-18 后续已通过 | WI-05/WI-13/WI-16 关联的间歇性 1ch 未闭环；WI-09 启动瞬态保留观察；后续 11 份 dump 全为 8ch 也不足以关闭历史间歇失败 |
| Linux→Linux | 34/34 通过：4 种发送 × 4 种接收基础矩阵 16/16，以及格式、声道、顺序和非法参数扩展 18/18 | 覆盖 4/6/8、3/5/7、24bit 转 16bit、mono/stereo、编码帧、AV 与连接前参数拒绝 |
| Linux→Web | 2026-11-05 为 15 项通过、2 项部分通过；L2W-13/L2W-16 的重复退订问题在 11-06 定向复测通过。4.24.9-dev.2 的 L-W-04/L-W-12 也通过 | 两项部分通过只涉及清理竞态，定向修复结果不改写成原 51 项全量重跑结果 |
| macOS→macOS | 2026-11-05 为 14/14 通过；4.7.0 原生 dump 证明 8ch 齐全、声道顺序 1:1 | 4.5.2 发送侧问题与接收能力必须分开评价 |
| macOS→Web | 12/12 通过，覆盖 4/6/8、3/5/7 补丁开关、选择、生命周期与多发布者 | 奇数声道通过口径是 Demo 补丁行为符合预期，不代表 Web 原生解码已修复 |
| macOS→Linux | 13 项通过、1 项刻意错配观察；四类 Linux receiver 均有覆盖 | ML-14 为实际 4ch、receiver 错配 8ch，不按正常 8ch 通过 |
| Linux MKV→Web Demo 3 | 30/30 自动通过：8ch OPUSMC + H.264 1920×1080，含实际视频渲染、4×stereo 映射、降级/补订与生命周期 | 后续 4.24.9-dev.2 的 L-W-16 仅为部分覆盖，stock sample 不支持仅视频/后补媒体构造；严格 lipsync 不在通过范围内 |
| 多 Web sender→单 Web receiver | 3 sender、17 sender 持续上行、18 sender、UID 切换、取消重订、网络瞬断恢复、页面重载、频道隔离、非目标停发、错误 token 恢复等通过 | 普通 mono/stereo 入口 MS-16、并发 join/publish MS-28、原 32-Web runner 的 32/33 边界未完成；重复 UID 为观察项 |

## 4. 重点专项结果

### 4.1 Native/Linux 奇数声道

- Linux↔Linux、Linux→macOS、macOS↔macOS、macOS→Linux 共 12/12 通过。
- 同序号源声道 NCC 达到文档门槛，跨声道相关接近零；没有重排、串声道或丢声道。
- 发送端必须设置 `che.audio.custom_channel_num`；payload 123/124/125 与 3/5/7 或 4/6/8 的对应配置必须一致。
- Linux receiver 的 `numOfChannels` 必须与发送端一致，不能用“文件非空”或“有音量”证明映射正确。

### 4.2 Web 奇数声道与补丁

| 场景 | 实测行为 | 当前判定 |
| --- | --- | --- |
| Web 自定义 3/5/7 发布 | 可以得到 `publish onSuccess`，但对端没有 `user-published` 或有效流 | `limitation/unsupported`；集成层应在创建音轨前拒绝 |
| Web 接收 Native/Linux 3/5/7 流 | 按 4/6/8 的 N+1 偶数声道解码，末两个声道内容相同，页面可能误判多声道成功 | 原始结果不可信；不能按正常 N 声道通过 |
| Demo `oddChannelFix.ts` | 检测末两声道恒等后去掉幻影声道，修正显示和播放；3/5/7 及正常 4/6/8 对照符合预期 | 仅为 Demo 过渡方案，根本修复仍需 SDK/VOS 按真实配置处理 |

补丁可能对“末两个声道恰好同内容”的真实偶数流误触发，且 codec 行仍可能显示 SDP 的偶数配置。主线 4.24.10 是否仍需要该补丁，必须重新做开关对照回归。

### 4.3 `selectChannels()` 与输出判定

- 4ch 流调用 `selectChannels([7], "mono")` 会返回成功，但实际输出 PCM 峰值和 RMS 均为零；回选 `[3]` 恢复。
- 原因是转换器只遍历真实声道，没有任何源声道命中 `[7]`，输出缓冲区保持全零。
- 混合合法与非法序号时，非法项可能不参与输出，因此“有声音”也不能证明整个列表合法。
- 集成层必须校验 `0 <= index < remoteChannelCount`，拒绝整个非法选择并保留之前的有效配置；重新订阅或声道数变化后重新校验。
- `getVolumeLevels()` 表示选择/混音前的源声道音量，不能单独证明选择后的实际播放输出。关键验收应测输出 PCM 或实际试听。

### 4.4 清理与生命周期

| 问题 | 历史证据 | 最新有效结论 |
| --- | --- | --- |
| `InvalidStateError: Stream closed` | 旧版 4.24.9 的本地多声道 transformer 清理与未处理 `pipeTo()` rejection；受控复现 22/22 | 4.24.9-dev.2 增加终止状态、输入/输出轨停止和 rejection 处理；30 秒、三次停发/重发、取消重订、同/换 UID 重发均未复现，视为旧版问题已闭环 |
| Demo 2 重复退订 | `user-unpublished` 与 `user-left` 重叠进入 `unsubscribe()`，第二次可能抛 `INVALID_REMOTE_USER` | 增加 in-flight 防重、同步清理和精确竞态处理；L2W-13/L2W-16 定向通过，后续回归也未再出现 |
| 运行中改变声道数 | 已有 Multiopus transformer 时不支持直接改声道数或降回 1/2ch，异常可能只表现为 warning | 应通过完整停发→新 track→重新订阅生命周期切换，不能复用旧 transformer |

### 4.5 音量柱底数

- Linux→Web 的静音声道出现约 6%～12% 读数，不是 SDK 的 Linux 分支缺陷，也不代表可听串音。
- `getVolumeLevels()` 使用 dB 宽量程：`(dBFS_RMS + 100) / 100`，只有精确数字零显示 0%；约 -90 dB 的残余就会显示约 10%。
- Linux 的 16bit 素材静音段含 -84 dB dither；Web 的 24bit 素材静音段为数字零。换用 24bit 素材后底数消失，完成端到端因果验证。
- 编码、传输、解码和实际播放 PCM 均无异常；逐声道内容应结合完整素材循环、输出 PCM 和试听判定。

### 4.6 macOS 版本边界

| SDK | 发送侧结果 | 接收侧结果 |
| --- | --- | --- |
| 4.5.2 | 协商正常，但自定义音轨输入被钳制为 2 声道，CH2 起数字静音 | 8ch 解码、dump、逐声道控制通过 |
| 4.5.3 | 8ch 全通，确认为发送侧修复起点 | 未单独做 Native 接收全量回归 |
| 4.6.0 / 4.6.2 | 8ch 全通 | 未单独做 Native 接收全量回归 |
| 4.7.0 | 8ch 全通，4/6ch 同验通过 | 原生 dump 8ch 齐全，声道顺序 1:1；3/5/7 Native 链路通过 |

因此发送多声道要求 macOS SDK ≥4.5.3；接收能力最早在 4.5.2 已验证可用。macOS SDK 不存在本次排查所涉及的 4.6.7 版本。

### 4.7 纯音频 32/33 路容量边界

- 默认容量按**频道和发送端**计算，不是整个 App ID 合计：音视频场景默认 17 路，纯音频默认 32 路；仅作为观众加入不占发流名额。
- 第 18 路音视频不保证所有流可见，也不保证固定错误或 `ERR_TOO_MANY_BROADCASTERS`；第 18 路纯音频仍在 32 路范围内。
- 一路 4/6/8 声道 OPUSMC 发布只占一个音频发送端名额，不按声道数倍增。

2026-11-17 的完整混合端轮次 `full-1794902759472`：

| 阶段 | 结果 | 关键证据 |
| --- | --- | --- |
| 31 Linux + Web 32 | MS-19-MIXED 通过 | 31 个 Linux 的 `txAudioBytes` 全部增长；Web `bytesSent=881→38495`、`packetsSent=23→325`；首/中/末 Linux UID 和 Web UID 均为真实 4ch multiopus |
| 增加 Web 33 | MS-20-MIXED 未通过有效媒体判据 | join/publish 均 `onSuccess`，Web `bytesSent=2800→30629`、`packetsSent=34→285`，但 receiver 25 秒无有效 track；前 32 路仍正常。另一轮 `full-1794902310234` 同样复现无远端 track |
| 停止 Linux UID 93170001 | MS-21-MIXED 通过 | 同一 Web 33 重新发布后恢复真实 4ch multiopus，四声道峰值均超过 0.76 |

该结果证明容量边界处可能出现“API 成功、本端上行增长，但没有有效远端流”的表现。业务不能依赖发布成功日志或本端 stats 控制上麦人数，必须由 receiver 验证真实媒体。该实验不覆盖 32 个 Web 客户端同时运行的资源与并发能力。

## 5. 严格失败、观察项与未闭环问题

### 5.1 需要继续跟进的真实失败

| 用例/问题 | 当前状态 | 归因与下一步 |
| --- | --- | --- |
| WI-05 / WI-13 / WI-16：Web→iOS 间歇性 1ch | **未闭环** | 曾出现连续三次发布 8ch→8ch→1ch，双击不是必要条件。后续 11 份 dump 全为 8ch 只能说明该轮未复现，不能关闭历史失败。需要失败窗口的 iOS SDK 日志、协商信息和服务端记录，并用 Native sender 做对照 |
| W-M-08-1/2：普通 mono/stereo 到 macOS dump | **严格失败** | dump 均为 8ch，属于 Native Receiver dump/控件配置问题，不是 Web multiopus 限制 |
| W-M-09：非法 WAV 输入 | **严格失败** | 24bit、44.1kHz、不足一帧 WAV 被实际发布并生成 dump；只有不存在 URL 正确失败，属于应用输入校验问题 |
| WI-09：4ch 素材声明 8ch 后的 iOS 启动瞬态 | **观察项，未闭环** | iOS 仅在约 41ms 内出现四路相同非零残值，之后为数字零；是否接受需产品/研发明确 |

2026-11-12 的归类统计曾记录：98 项通过、4 项 `limitation/unsupported`、3 项严格失败、2 项部分覆盖、1 项配置观察。该统计包含补充子项且与其他文档存在重叠，不能与其他轮次相加。其中严格失败仅为上述 W-M-08-1/2 和 W-M-09，不应误归因于 Web SDK 的正常 4/6/8ch 能力。

### 5.2 集成层必须处理的限制

- `selectChannels()` 不按当前流真实声道数校验，越界可能静默输出全零。
- Web 自定义奇数发布、源实际声道数与声明不一致、运行中改变声道数均不能只看 API 成功日志。
- 4ch 素材声明 8ch 时，额外路在不同链路可能表现为数字静音或复制，均不是独立 8ch 内容。
- 重复 UID 是应用身份约束问题，不能把两个同 UID 标签当成两个独立 sender。
- 混音文件只能验证混音，不能证明每个源的 N 声道无串音；编码帧文件不能按 PCM 分析。

## 6. 已闭环或不应误报为缺陷的项目

| 项目 | 最终结论 |
| --- | --- |
| 旧版 `Stream closed` | 4.24.9-dev.2 回归未复现，不再作为开放缺陷；主线版仍需常规清理回归 |
| Demo 2 重复退订 | 11-06 修复并定向通过，后续多项生命周期回归通过 |
| macOS 4.5.2 发送侧 2 声道钳制 | 4.5.3 起修复；4.5.3/4.6.0/4.6.2/4.7.0 均通过 |
| WI-18 逐声道开关/音量与多 host | 11-06 定向通过，具有实际 UI 操作和 PCM 时间窗证据 |
| Linux→Web 音量柱不归零 | dB 刻度与素材 dither 的正常表现，非 SDK 缺陷 |
| 原 17/18 路音频 runner 的容量断言 | 17 路不是纯音频上限；第 18 路纯音频成功属于预期，旧断言失败不计产品失败 |
| 首次 Linux sender 未显式 UID 导致订阅错目标 | 测试配置失败，补充正确 UID 后通过，不计产品缺陷 |
| 短采样窗口、固定 640B 编码帧长假设 | 测试判据错误，已纠偏；完整素材循环和真实编码回调才是有效判据 |
| Linux 16bit 固定频率点频谱门禁 | 已改为固定窗口、全频谱局部主峰、250ms 串音子窗口和启动/稳定双阶段，15 项单测通过。x86_64 修正后全量 48/48；ARM64 为 44/48，4 个失败均为真实启动强 300Hz、steady 通过。不归为编码器串音，但 SDK 内部补偿模块仍待查，不能用 x86_64 全绿抵消 ARM64 异常 |
| Linux N/M 与内容语义扩展 | 全部 30 个有序 N≠M 组合均无一对一内容通过；x86_64 `8to6` 首轮 SIGSEGV 已改判错误，不能算安全拒绝。混音双架构通过；x86_64 PCM/AV 和 MKV 通过；ARM64 时间戳连续性及 MKV steady 通过，startup 频谱仍失败。生命周期恢复内容为 ARM64 16/23、x86_64 23/23 |

## 7. 尚未完成或不能外推的范围

1. **4.24.10 主线版全量媒体回归**：第 9 节所列 P0 链路已完成，但不等于本文所有历史场景、旧版未覆盖项和完整矩阵均已重跑。
2. **4.24.9-dev.2 的五条主链路完整矩阵**：仍有未执行或部分覆盖项，包括 W-M-07-5/7、W-M-20、M-W-15、L-W-16 的完整构造等。
3. **多 sender**：MS-16 普通 mono/stereo 可验证发布入口、MS-28 真并发 join/publish、原 32-Web runner 的 32/33 边界；混合 32 路结果不能替代这些缺口。
4. **Token 与网络**：错误 token 和本地 route abort 瞬断已有覆盖，但真实 token 过期、物理链路断网、弱网/丢包和长时间重连仍需专项验证。
5. **平台与设备**：iOS 真机、Safari/Firefox、物理多声道输出设备、系统 down-mix、人工逐声道试听、音色/杂音和严格 lipsync 未完整覆盖。
6. **长稳**：2 小时以上连续发布订阅、内存和资源泄漏未形成通过结论。
7. **Web 奇数声道补丁**：主线版是否需要补丁、补丁对相同末两声道真实偶数流的误触发风险，仍需单独回归。
8. **Linux 多声道 NetEq/PLC**：4.4.30.152 为 ARM64 45/48、x86_64 48/48。新增对照已确认实际码流正常，首声道基频在启动补偿/同步阶段泄漏到其他声道；首声道静音可消除异常，首声道改为 400Hz 时异常跟随为 399.9Hz。具体内部模块和实际配置路径仍需 SDK 团队定位，不能用 x86_64 全绿抵消 ARM64 异常。
9. **Linux N/M 错配及间歇性崩溃**：双架构 30 个有序组合均未证明一对一内容映射；N>M 呈确定折叠混音，N<M 有重复/频移，应归为不支持且未安全拒绝。旧 x86_64 `8to6` 和新 ARM64 `7to4` 均有 SIGSEGV；100 轮 N/M ASan 对照及 20 轮连接超时对照未复现。ARM64 已取得 core 并收敛到音频回调注销/取流/销毁竞态造成的空指针访问；历史 x86_64 仍缺 core 和匹配符号，两起问题均不能关闭。

后续优先级建议：先由 SDK 团队定位首声道启动泄漏和 receiver SIGSEGV，并处理 `selectChannels` 与源/声明校验；随后定位 Web→iOS 间歇性 1ch 和 W-M-08/W-M-09，再补 MS-16/MS-28、真机、弱网与长稳。

## 8. 统一判定口径

- 发送端日志、join/publish 成功、本端 outbound stats 只能作为链路的一部分，不能替代接收端真实媒体证据。
- Web 至少记录 codec、信令 `channelCount`、音量数组长度和完整素材循环峰值；声道选择需额外验证输出 PCM 或试听。
- Native/Linux PCM dump 应为 48kHz、16bit、预期声道数，并检查逐声道峰值/RMS与同序号内容；NCC 只适用于时间和相位已对齐的比较，不能用混音或错配文件证明逐声道正确。
- 对 Opus/NetEq 输出做频谱验收时，应使用固定且频率对齐的窗口、全频谱局部主峰和合理频率容差，并分开启动补偿与稳定阶段。固定频率点短窗口漏掉偏移主峰时，不得直接判定声道错序或编码串音。
- 怀疑 OPUSMC 编码串音时，应保存实际编码包并离线解码：若码流各声道正常而 NetEq PCM 异常，应继续检查接收端丢包、NetEq/PLC 和后续处理。补偿瞬态需单独记录，既不能因后续恢复而删除，也不能用单窗口结果认定持续产品缺陷。详见 [Linux 测试覆盖评估](comments_for_linux_demo.md)。
- N/M 声道错配时，`degraded_to_requested_channels` 仅是 transport 行为，不等于一对一内容正确；需检查混音、复制、缺失、重排和未匹配非静音声道。只有显式 `PCM_FRAME_METADATA_MISMATCH` 才能记为安全拒绝，SIGSEGV 或其他非零退出必须记为错误。
- encoded receiver 只验证 OPUSMC 编码回调与落盘；mixed receiver 只验证 1/2ch 输出；AV 纯音频源不要求 YUV。
- 视频需检查 readyState、宽高、`currentTime`/解码帧持续前进及非黑帧/非静止帧，不能只看 subscribe success。
- 自动化 runner 超时、DOM 注入错误、权限不足、断言过严和旧容量假设应与产品失败分开记录。
- `limitation/unsupported`、刻意错配、观察项、未执行和严格失败必须分开统计；未执行不得写成通过。


## 9. 2026-11-24：当前版本 P0 回归与仍未闭环项

### 9.1 Web 4.24.10 与 Linux 4.4.30.152 主链路

本轮使用 Chrome 157、Web SDK 4.24.10 和 Linux SDK 4.4.30.152，按完整 Channel ID 循环采集 20 秒逐声道峰值；不再只有安装和生产构建结论。

| 链路 | 4ch | 6ch | 8ch | 证据口径 |
| --- | --- | --- | --- | --- |
| Web→Web | 通过 | 通过 | 通过 | 信令/解码声道数一致，逐声道峰值约 0.72～0.86 |
| Web→Linux PCM | 通过 | 通过 | 通过 | 48kHz、声明声道数的实际 PCM；各声道均有内容级峰值 |
| Linux→Web | 通过 | 通过 | 通过 | codec=multiopus，信令/解码声道数一致，逐声道峰值约 0.81 |
| Web→macOS | 通过 | 通过 | 通过 | 48kHz / 4、6、8ch dump，各声道均有内容 |
| Web→iOS 模拟器 | 通过 | 通过 | 通过 | 48kHz / 4、6、8ch dump，各声道均有内容；不等于真机结果 |

补充说明：

- Web→Linux 的 PCM 文件和 FFmpeg `astats` 位于 `/private/tmp/agora-sdk152-p0-20261124/web-linux*`。首轮 6→4 操作错配已单独保留为 `received-invalid-6to4.uid-93001`，没有冒充 4ch 通过证据；正确 4ch 已重跑。
- Native dump 和逐声道分析位于 `/private/tmp/agora-sdk152-p0-20261124/native/`。本轮 Web→iOS 未出现间歇性 1ch，但历史 WI-05/WI-13/WI-16 不能据此关闭；W-M-08 普通 mono/stereo dump 和 W-M-09 非法 WAV 也不在本轮通过范围内。
- Web 生产构建 `npm run build` 再次通过，构建只有 chunk 大小提示。

### 9.2 生命周期与异常输入

- **通过**：8ch 停发→重发→重新订阅、Demo 2 显式取消/重订、Linux sender 停发后 4/6/8ch 重订、显式离开、页面卸载。页面卸载后发布端收到 `user-left`，未观察到 `Stream closed`、重复退订或未处理 Promise。
- **SEL 越界仍失败**：4ch 流选择 `[7]` 会返回成功，实际 transformer 输出 PCM 八路均为数字零，回选 `[3]` 恢复。`selectChannels onSuccess` 仍不能代替真实声道数校验；集成层必须拒绝整个非法选择。证据：`web-selectchannels-4ch-out-of-range.json`。
- **源/声明错配仍失败**：4ch 素材声明 8ch 可以发布并协商为 8ch，但接收端只有前四路有内容，CH4～CH7 峰值均为 0，不是独立 8ch。证据：`web-source-4ch-declared-8ch.json`。
- 因此本轮 P0 媒体回归完成不等于所有异常验收通过；上述两项仍是集成层工作项。

### 9.3 Linux 新包与专项结论

- Linux 4.4.30.152：ARM64 **45/48**，x86_64 **48/48**。ARM64 三项失败均为 startup 强 300Hz、steady 恢复。
- 新增码流对照确认：离线 OPUSMC 全部正常，首声道基频在接收端启动补偿/同步阶段泄漏到其他声道；首声道静音 12/12，首声道改 400Hz 时异常跟随为 399.9Hz。函数级内部模块仍需 SDK 团队确认。
- N/M 在双架构均不满足一对一内容，但 N>M 呈确定折叠混音，N<M 有重复/频移；应视为不支持且未安全拒绝，而不是 30 个随机产品失败或 transport 通过。
- ARM64 `7to4` 新发生一次 SIGSEGV。新旧共 100 轮 N/M ASan/UBSan 对照和 20 轮连接超时对照未复现，历史与本次崩溃都不能关闭。
- 完整证据、诊断边界和更新后的顺序见 [Linux 多声道 Demo 测试覆盖评估](comments_for_linux_demo.md) 的 2026-11-24 章节。


### 9.4 解密与 coredump 后更新的根因边界

- NetEq 日志确认 `enable_time_stretching=false` 实际生效，但异常轮仍有 `expand=180ms` 和首声道泄漏；应重点检查 NetEq expand / OPUSMC 解码补偿路径，而不是把时间拉伸开关当成修复方案。
- 同一 OPUSMC 包在 libopus 1.4 中注入 5 个丢帧，标准离线 PLC 没有复现首声道泄漏，进一步指向 Agora 的接收补偿封装路径。
- ARM64 `7to4` 已取得真实 coredump：顶部为 `libagora_rtc_sdk.so + 0xcbc81c`，后续为 `+0x12e2ccc`、`+0x4aedd4`、`+0x12e3164`、`+0x3df180`，再进入 AOSL 队列。core 中 `x0 = 0`，故障指令读取 `[x0 + 16]`，直接故障是**空指针解引用**，不能继续把 use-after-free 写成已证实结论。
- 149 ARM64 与 x86_64 152 的符号对照将路径收敛到 `AudioTransportWrapper::NeedMorePlayData()`：注销回调会清空 transport 指针，取流末尾无判空调用 `GetDownlinkProcessingAverageTime()`；销毁线程也出现相同对象地址并进入 `operator delete`。真实 core 与研发反馈共同确认清理与在途任务的顺序，故障不是 N/M 映射算法。修复需同时考虑判空和对象生命周期，具体修复方式及引入版本仍需 SDK 团队结合源码和提交历史确认。
- **新增 x86_64 152 自然复现**：第 84 次真实 join/teardown 出现 SIGSEGV，频道 `audio-race-1795508775062900818-b21-w2`，SID `2D47781173174F3EA6AFE15763A4A3CE`。同时取得原生 dmp 和系统 ELF core，匹配符号直接还原为 `audio_transport_impl.cc:300` → `audio_transport_wrapper.cpp:1103` → `audio_device_buffer.cc:583` → `audio_transport_wrapper.cpp:737`；`rdi = 0`，wrapper 的 `+0x490` 成员也为零。20ms 完整批次另三次崩溃中的一次也还原为同一堆栈。
- x86_64 共执行 400 次真实 RTC 尝试，记录 9 次自然 SIGSEGV；20ms 为 **5/104**，仍不是 100% 复现。ARM64 无媒体 1000 次未复现，真实 RTC 200 次未出现 SIGSEGV但有 38 次连接失败，连接失败不计为崩溃。
- 历史 x86_64 149 只有日志，不能声称已精确还原其堆栈；但新的 x86_64 152 场景已直接证实同类故障。新增 runner 位于 `Demo/linux/tests/audio_teardown_race.py`，10 个离线单元测试通过。复现命令见 `Demo/linux/README.md`，函数级边界见 `Docs/comments_for_linux_demo.md` 和 `Docs/linux_lifecycle_regression_2026-11-19.md` 的最新补充。本项在修复和修复后回归前保持开放。
- 研发反馈确认清理时先置空 `audio_transport_`、但任务未退出，与 core 一致。新增 `--expect-join-timeout` 后，隔离网络下双架构各执行 40 次真实入会超时：ARM64 **3/40 SIGSEGV**，x86_64 **1/40 SIGSEGV**，故障偏移分别仍为 `0xcbc81c`、`0x13e8470`。这支持超时是高风险窗口，但不是必要条件或 100% 触发条件；成功入会后立即退出也已自然复现。退出阶段的崩溃、core 生成和自动化不稳定仍需修复，不能据此关闭问题。
- 两场独立 x86_64 崩溃的原生 dmp、日志、系统 core 和符号化输出均已按 PID 归档；这些原始二进制证据未纳入本仓库。
