# Linux 多声道 Demo 测试覆盖评估

评估日期：2026-11-19；最近定向复跑更新：2026-11-23

评估范围：`Demo/linux/` 下的发送端、接收端、测试脚本、素材和现有回归记录。本文区分“代码提供了测试入口”“已有实际回归证据”和“已形成可重复自动化测试”，避免把 sample 存在误认为功能已经完整验证。

## 结论

Linux Demo 的功能入口比较完整，已经覆盖 4 类发送路径和 4 类接收路径。`Demo/linux/tests/` 现已包含连接失败回归、私有 PCM 文件安全测试、输入校验单测、PCM 逐声道内容分析单测和 MKV 退出回归脚本；`Demo/linux/out/pcm_channel_analyzer` 提供真实接收文件的分析入口。

2026-11-19 的 ARM64 全量回归已经覆盖主工程编译、4×4 实际收发矩阵、非法输入、生命周期和 MKV teardown。正式脚本首次执行为 45/48 通过：奇数声道 WAV 的输出已按完整 PCM 帧对齐，3～8 声道 16bit 频谱通过；当时的 24bit 频谱和 codec 时间戳结果仍需复跑。

2026-11-23 的定向复跑中，24bit 独立频率用例 4/4 通过，发送侧转换结果与 16bit 参考逐字节一致。codec 已拆分为默认功能门禁和显式传输连续性门禁：调整后的 monitor 模式 13/13 项断言通过，严格模式在当前非受控网络下为 9/13，4 个失败均为实际时间戳连续性缺口。定向结果不能改写为全量 48 项通过。

## 已覆盖

### 功能入口

`Demo/linux/README.md` 中列出的发送和接收 sample 已覆盖：

- raw PCM 多声道发送；
- WAV 多声道发送，包含 16bit 直发和 24bit 转 16bit；
- 预编码 8 声道 Opus 裸流发送；
- MKV 的 4×stereo 到 8 声道音频发送，以及视频发送；
- 按用户接收解码后的多声道 PCM；
- 接收 OPUSMC 编码帧；
- 接收 SDK 混音结果；
- 多声道音频和 I420 视频同时接收。

详见 [Demo/linux/README.md](../Demo/linux/README.md#功能列表)。

### 已有实际回归证据

2026-11-19 的 ARM64 Ubuntu 24.04 Podman 容器全量回归已验证：

- 独立频率合成 16bit WAV 的 3、4、5、6、7、8 声道到 PCM 接收，并通过逐声道 FFT 检查；
- 仓库 24bit 8 声道 WAV 的转换、发送和文件生成；逐声道内容门禁仍未通过；
- PCM 与预编码 Opus 双发送者按 UID 分文件接收；
- 双发送者编码帧接收；
- Opus 到混音接收；
- MKV 到音频加 I420 视频接收；
- 四种发送端使用无效凭据时超时退出并完成清理；
- 14 个非法参数端到端场景在 `onConnecting` 前拒绝；
- 生命周期 2 轮、MKV 退出 4 个场景，以及 4×4 sender × receiver 的 16/16 真实组合。
- 发送 8 声道、接收 3 声道和发送 3 声道、接收 8 声道均实际降级为接收端请求的声道数，并记录了 PCM metadata。
- 普通 Opus 对端已加入 codec 回归：编码帧中普通流为 `codec=1`、多声道流为 `codec=13`；当前测试尚未观察到多声道流因普通对端而整体回退。

详见 [Linux 全量回归记录](linux_full_regression_2026-11-19.md) 和 [生命周期回归记录](linux_lifecycle_regression_2026-11-19.md)。

### 2026-11-23 定向复跑与门禁调整

- 24bit 独立频率 8 声道用例连续 4/4 通过；使用与发送端共用的 C++ 转换函数导出的 PCM 与 16bit 参考均为 6,144,000 字节，SHA-256 一致。严格时域相关性在 16bit 和 24bit 上都会受 Opus 相位影响，仍不能单独作为门禁。
- 调整前 codec 场景合计 5/8 通过。解密失败日志显示接收端成批交付、`AudioPktConverter ... dropped with old seq` 和重传耗尽；两路发送端的 PCM/编码生产计数连续。直接原因定位在传输/接收重排，不是 codec fallback 或发送端主动跳变。
- 调整后 monitor 模式要求时间戳严格递增并检查 codec、输出和退出码，传输缺口记录为 warning。PCM 矩阵及双发送者合计 13/13 项断言通过，其中实际出现的 2880～4800 tick 缺口未再误判为 codec 失败。
- 显式严格模式 9/13 项断言通过，4 个失败均记录了实际连续性缺口和接收端退出码 1；当前非受控网络下的严格连续性仍不能标记为通过。
- 产物保存在本机临时目录 `/private/tmp/agora-linux-p0.ERq5S9/` 和 `/private/tmp/agora-linux-codec-split.g6CghE/`；后者包含 monitor/strict 结果 JSON、应用日志和 SDK 日志。

现有汇总文档还记录过 Linux→Linux 34/34 通过，包含 4 类发送端、4 类接收端基础矩阵，以及格式、声道、顺序和非法参数扩展。该结果属于历史实测记录，尚未在 `Demo/linux/tests/` 中形成同等范围的可重跑测试套件。

### 已有自动化测试

- [connection_failure_regression.sh](../Demo/linux/tests/connection_failure_regression.sh)：对四种发送端使用无效 App ID，检查连接超时日志和退出码。
- [private_pcm_files_test.cpp](../Demo/linux/tests/private_pcm_files_test.cpp)：检查临时目录 0700、文件权限、重复文件、符号链接防护和析构清理。
- [multichannel_input_validation_test.cpp](../Demo/linux/tests/multichannel_input_validation_test.cpp)：覆盖 RIFF/WAV chunk 边界、奇数 chunk 对齐、Extensible PCM GUID、非法格式、CLI/声道/采样率错配、短 PCM、PCM 尾帧补零和非整帧 Opus。
- [run_input_validation_tests.sh](../Demo/linux/tests/run_input_validation_tests.sh)：无需 Agora SDK 即可运行上述输入校验测试。
- [pcm_channel_analyzer.cpp](../Demo/linux/common/pcm_channel_analyzer.cpp) 与 [pcm_channel_analyzer](../Demo/linux/tools/pcm_channel_analyzer.cpp)：读取 16/24bit WAV 或裸 16bit PCM，计算每路 RMS、峰值、源到接收的相关性和一对一映射，检查声道乱序、丢失、复制和明显串音。
- [pcm_channel_analyzer_test.cpp](../Demo/linux/tests/pcm_channel_analyzer_test.cpp)：对 3/4/5/6/7/8 声道覆盖正确映射、乱序、复制、静音丢失、声道数错配和比较偏移。
- [run_pcm_channel_analysis_tests.sh](../Demo/linux/tests/run_pcm_channel_analysis_tests.sh)：无需 Agora SDK 即可运行逐声道分析合成测试。
- [mkv_exit_regression.sh](../Demo/linux/tests/mkv_exit_regression.sh)：在提供 `APP_TOKEN` 后重复运行正常退出、sender SIGINT、sender SIGTERM、receiver 先退出，并分别覆盖真实临时文件和 `/dev/null`。
- [lifecycle_regression.sh](../Demo/linux/tests/lifecycle_regression.sh)：编排 PCM 发布/取消发布、订阅/取消订阅、双方同 UID 重进及快速退出，检查 API、回调、PCM 文件增长和退出码；ARM64 Ubuntu 的 8 声道真实回归已通过，见 [回归记录](linux_lifecycle_regression_2026-11-19.md)。
- [full_regression_test.py](../Demo/linux/tests/full_regression_test.py)：7 项编码日志断言单测，覆盖 monitor 记录缺口、strict 拒绝缺口、订阅 warmup、非单调时间戳和 codec 错配。
- 本轮全量 C/C++ 回归见 [Linux 全量回归记录](linux_full_regression_2026-11-19.md)：主工程 9 个目标和 4 个独立解析器均编译通过，4×4 真实收发矩阵、生命周期、MKV teardown、N→M 降级和扩展非法输入通过；2026-11-23 的 24bit 和 codec 定向复跑结果见上节。

## 部分覆盖

### 声道内容分析器已完成，真实媒体门禁部分通过

逐声道分析器已经可以对 3/4/5/6/7/8 声道计算 RMS、峰值和源到接收的相关性，并以一对一映射检查顺序、丢失、复制和明显串音。独立频率的 16bit 合成 WAV 以及重新生成的 `odd_3ch.wav`、`odd_5ch.wav`、`odd_7ch.wav` 已在真实 SDK 链路上通过帧对齐和 FFT 检查；严格时域相关性仍会因 Opus 相位和串扰失败，不能单独作为链路失败依据。2026-11-23 的 24bit 用例 4/4 通过，但同日 16bit 对照只有 2/4 通过，失败轮次出现主频串扰，频谱门禁的稳定性仍需单独调查。

### 4×stereo 到 8ch 的布局

MKV sample 实现了 4×stereo 重采样、重排和 10ms 拆帧。真实 4×4 矩阵中的 MKV→AV 组合已收到 22 个完整的 1920×1080 I420 帧，前两帧内容不同；但目前只断言文件非空、视频尺寸和推进，没有针对 4×stereo 到 8ch 的逐声道内容映射门禁，仍需保存接收文件并执行分析器。

### 24bit 转换

24bit WAV 的本地单测已覆盖负值、极值和右移语义。2026-11-23 的真实链路逐声道频谱 4/4 通过，发送侧转换 PCM 与 16bit 参考逐字节一致，此前的 24bit 转换失败未再复现。该结果不覆盖 16bit 对照中观察到的间歇性频谱串扰，也不替代 x86_64 回归。

### 混音和编码帧

混音路径已在 4×4 矩阵中验证文件有数据、输出格式和 RMS 非零；混音隔离和逐声道内容仍缺。编码帧路径已按 UID 分文件并验证普通 Opus `codec=1`、OPUSMC `codec=13`。默认 monitor 模式检查时间戳单调性、codec、输出和退出码，传输缺口记录 warning；`--strictTimestampContinuity 1` 与回归脚本 `--strict-timestamp-continuity` 才将后续缺口判为失败。AV 路径已验证音频输出及完整 I420 帧、前两帧差异和视频时间戳递增；音频连续性和完整内容语义仍缺。

### 生命周期和架构

正常退出、无效凭据清理和生命周期竞态已有 ARM64 真实验证。MKV 发送端和 AV 接收端现已统一使用原子退出标志、SIGINT/SIGTERM 处理、线程失败传播、可中断视频 PTS 等待和完整 teardown；`mkv_exit_regression.sh` 的正常退出、sender SIGINT、sender SIGTERM、receiver 先退出 4 个场景均通过。仍需在 x86_64 复测，并扩大到更多收发路径和长时间竞态，确认无卡死、残留线程和未关闭文件。

当前实际容器回归只覆盖 ARM64；x86_64 仍需要执行同一套回归才能支撑 README 中的双架构支持声明。

## 缺失场景与优先级

表格中的“已实现”只表示代码或本地测试已经具备；只有“真实回归通过”才表示在有效凭据、实际 SDK 和网络链路上完成了验收。

| 优先级 | 场景 | 当前状态 | 下一步验收 |
| --- | --- | --- | --- |
| P0 | 逐声道内容正确性 | 部分通过：24bit 在 2026-11-23 连续 4/4 通过且转换样本与 16bit 参考一致；同日 16bit 对照 2/4，失败轮次存在频谱串扰；严格时域相关性不能单独作为判据 | 调查 16bit 对照的频谱门禁稳定性，保留失败样本，并在 x86_64 复测 16/24bit |
| P0 | 可重复的 Linux 多声道矩阵 | `Demo/linux/tests/full_regression.sh` 和 `full_regression.py` 已固化 4×4 矩阵、内容断言、错配、codec 和非法输入；2026-11-23 monitor 定向 13/13 项断言通过，但不是全量 48 项重跑 | 在 ARM64 全量复跑并在 x86_64 执行同一脚本，保留结果 JSON、日志和失败样本 |
| P0 | 非法输入和参数组合 | 输入校验代码与本地单测已完成；14 个 ARM64 真实端到端场景均在 `onConnecting` 前以退出码 255 拒绝 | 在 x86_64 重跑，并继续确认无发布、无远端文件，记录退出码和日志 |
| P0 | MKV 音视频退出稳定性 | `mkv_exit_regression.sh` 的正常退出、sender SIGINT、sender SIGTERM、receiver 先退出 4 个场景在 ARM64 全部通过，覆盖真实临时文件和 `/dev/null` | 使用有效凭据在 x86_64 复测多轮，确认无卡死、残留线程、未关闭文件和异常子进程 |
| P0（条件） | ARM64/x86_64 对等回归 | ARM64 已有完整媒体和退出证据；x86_64 尚未执行 | 若两种架构都是支持目标，执行相同媒体、输入校验、逐声道分析和退出矩阵 |
| P1 | codec fallback / `codec_unfallback` | 2026-11-23 monitor 定向 13/13 项断言通过，普通流保持 `codec=1`、多声道流保持 `codec=13`，未观察整体回退；严格连续性在当前网络为 9/13 | 在可控无丢包网络执行严格连续性门禁，并补 capability/远端媒体证据，明确 SDK 实际协商行为 |
| P1 | 生命周期和订阅竞态 | ARM64 Ubuntu 上 PCM 发布/取消发布、订阅/取消订阅、双方同 UID 重进及快速信号退出已完成 2 轮真实回归并通过 | 在 x86_64 和更多收发路径复测，进一步验证逐声道内容及长时间竞态 |
| P1 | 接收端声道数错配 | 已实测 8→3 和 3→8：两者均连接成功并按接收端请求输出 3/8 声道，metadata 与文件帧大小一致 | 在 x86_64 和更多 N/M 组合复测，并增加逐声道内容断言，确认降级不会复制或错序 |
| P1 | 网络和认证恢复 | 当前主要覆盖无效凭据超时，未覆盖恢复链路 | 覆盖 Token 过期/续期、断网、丢包/抖动和重连，确认声道数与媒体内容恢复 |
| P1 | 混音/编码/AV 语义断言 | 已加入输出格式、PCM metadata、编码 codec/timestamp delta、混音输出和 AV 帧尺寸/差异/时间戳断言；编码功能与传输连续性已拆分门禁，混音隔离与音频连续性仍缺 | 增加奇偶声道混音、mono 混音隔离、PCM 连续性和 AV 音频连续性断言 |
| P2 | 长稳和资源 | 没有长时间自动化记录 | 连续发布订阅至少 2 小时，观察 CPU、内存、线程、文件句柄和 SDK 日志增长 |
| P2 | 文件输出边界 | 已有部分分片和路径安全逻辑，异常磁盘场景未覆盖 | 覆盖 100MB 分片、磁盘写满、权限不足、UID 特殊字符、输出冲突和多远端并发写盘 |
| P2 | 解析器和帧边界 fuzz | WAV chunk、Extensible PCM、PCM 尾部和 Opus 非整帧单测已完成 | 增加 parser fuzz/property tests，重点检查任意截断、超大 chunk、奇数对齐和随机扩展 chunk 不崩溃、不越界 |

## 建议的第一阶段落地顺序

1. 在 ARM64 全量复跑更新后的 `full_regression.py`，随后在 x86_64 执行同一套矩阵、输入校验、逐声道分析和退出场景。
2. 调查 16bit 对照的间歇性频谱串扰，避免只凭单次 FFT 结果判定链路通过或失败。
3. 在可控无丢包网络执行 `--strict-timestamp-continuity`，继续区分网络、容器调度和 SDK transport 排队造成的缺口。
4. 若普通 Opus 对端仍不触发回退，补 capability 和远端媒体证据，明确记录 SDK 的实际协商行为。
5. 补混音隔离、PCM/AV 音频连续性和 N/M 错配的逐声道内容断言，继续推进网络恢复、长稳和 parser fuzz 测试。

验收时应遵守统一口径：join/publish 成功、本端发送统计和日志只能作为辅助证据；Native/Linux PCM 必须检查实际声道数和逐声道内容。编码帧、混音文件和音视频文件分别代表不同验证层级，不能互相替代。
