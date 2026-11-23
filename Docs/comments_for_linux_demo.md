# Linux 多声道 Demo 测试覆盖评估

评估日期：2026-11-19；最近定向复跑更新：2026-11-23

当前包版本说明：`LinuxSDKFolder/` 中的两种架构 SDK 已由 4.4.30.149 TGZ 更新为 4.4.30.152 ZIP。ARM64 的 SDK 位于 `Agora_Native_SDK_for_Linux_SERVER/sdk`，x86_64 位于 `Agora_Native_SDK_for_Linux_SERVER/rtc/sdk`。构建脚本和 Dockerfile 已适配这两种 ZIP 布局及旧 TGZ，将库目录归一化到 `agora_rtc_sdk/agora_sdk`，并将 ARM64 的 `sdk/include` 或 x86_64 的 `rtc/sdk/low_level_api/include` 映射为 `agora_sdk/include`。除非另有明确说明，本文中的媒体回归结果仍使用 4.4.30.149，不能据此推断 4.4.30.152 的行为或 NetEq/PLC 问题已修复。

4.4.30.152 构建适配验证（2026-11-23）：ARM64 Podman 和独立 x86_64 Docker 均使用 Debian bookworm-slim，完成 `build.sh` 解包复用及连续两次构建，9 个检查目标生成成功；完整运行时镜像的 SDK 动态库和素材检查、PCM/MKV 二进制的动态链接检查、镜像内 8 声道 PCM 分析器对照均通过。双架构输入校验、PCM 内容分析和私有 PCM 文件 C++ 测试通过；43 项 Python 离线单测、3 项生命周期工具正反例及 Web 文档站构建也通过。这些是构建、离线和本地内容检查，**没有使用 4.4.30.152 重跑完整 RTC 媒体矩阵**，历史 44/48、48/48 及其他媒体结果仍属于 4.4.30.149。

评估范围：`Demo/linux/` 下的发送端、接收端、测试脚本、素材和现有回归记录。本文区分“代码提供了测试入口”“已有实际回归证据”和“已形成可重复自动化测试”，避免把 sample 存在误认为功能已经完整验证。

## 结论

Linux Demo 的功能入口比较完整，已经覆盖 4 类发送路径和 4 类接收路径。`Demo/linux/tests/` 现已包含连接失败回归、私有 PCM 文件安全测试、输入校验单测、PCM 逐声道内容分析单测和 MKV 退出回归脚本；`Demo/linux/out/pcm_channel_analyzer` 提供真实接收文件的分析入口。

2026-11-19 的 ARM64 全量回归已经覆盖主工程编译、4×4 实际收发矩阵、非法输入、生命周期和 MKV teardown。正式脚本首次执行为 45/48 通过：奇数声道 WAV 的输出已按完整 PCM 帧对齐，3～8 声道 16bit 频谱通过；当时的 24bit 频谱和 codec 时间戳结果仍需复跑。

2026-11-23 的定向复跑中，24bit 独立频率用例 4/4 通过，发送侧转换结果与 16bit 参考逐字节一致。codec 已拆分为默认功能门禁和显式传输连续性门禁：调整后的 monitor 模式 13/13 项断言通过，严格模式在当前非受控网络下为 9/13，4 个失败均为实际时间戳连续性缺口。定向结果不能改写为全量 48 项通过。

随后在相同 ARM64 Ubuntu/Podman 环境、提交 `d10a5ff` 上完成更新后脚本的完整复跑，原始结果仍为 **45/48**，失败项变为 `tone-3ch-16bit`、`tone-8ch-16bit` 和 `odd-5ch`。后续实际码流对照证明：部分失败是频谱门禁误报；真实强 300Hz 瞬态来自接收端网络交付及 NetEq/PLC 补偿路径，不是 Demo 输入或 OPUSMC 编码码流串音。该分析改变结果解释，但不把原始 45/48 改写成全部通过。

频谱门禁修正后又在同一 ARM64 环境实际重跑完整 48 项，结果为 **44/48**：`tone-7ch-16bit`、`odd-3ch`、`odd-5ch`、`odd-7ch` 失败，且四例均为 `startup_pass=false、steady_pass=true`。原 `tone-3ch-16bit`、`tone-8ch-16bit` 本轮通过，`odd-5ch` 仍失败。所有 tone/odd 用例稳定阶段均通过；计数没有被人为改成 48/48，新增门禁继续把真实启动强 300Hz 判为失败。

随后在提交 `8a8536a` 对应的源码上，使用 x86_64 Ubuntu 24.04.5 Docker 和 SDK 4.4.30.149 完成对等回归：完整 **48/48** 通过，所有 startup/steady 通过；MKV 4 个退出场景和 2 轮生命周期回归也通过。x86_64 结果不能抵消 ARM64 的启动异常，两个架构及对应网络/时序条件分别记录。

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

### 2026-11-23 ARM64 全量复跑与 16bit 频谱专项

本轮使用提交 `d10a5ff`、Linux SDK 4.4.30.149 和 Ubuntu 24.04.5 ARM64 Podman 容器，通过正式 `tests/full_regression.sh` 入口执行默认 monitor 全量回归：

- 完整结果为 **45/48**，失败为 `tone-3ch-16bit`、`tone-8ch-16bit`、`odd-5ch`；其余 4×4 基础收发矩阵、24bit、codec monitor、声道错配和非法输入通过。前两次构建缓存或动态库配置造成的无效 `0/32` 已单独归档，不计入媒体结果。
- 8 组 WAV/裸 PCM 配对复测按原门禁分别为 **5/8、5/8**，合计 **10/16**。所有发送 PCM 的 SHA-256 均为 `6cafa706e9b3f16d30295efd3f7a702de3e3fc4d54321d9ec110280d1dbc80b9`，源频谱正确，排除 24bit 转换或 WAV 读取作为统一根因。
- 单发送者、双 PCM 接收端复测为 **2/6**，两个接收端 6/6 同轮通过或失败；该现象用于分层定位，但原门禁的同轮通过/失败不能直接等同于真实串音。
- 另做 6 轮 PCM receiver + encoded receiver 同频道采集，实际 OPUSMC 包均成功离线解码，**6/6、零解码错误**。在 250ms、500ms 窗口，离线解码各声道主频正确，其他声道 300Hz 分量低于各自主峰的 1%。

### 频谱门禁误报与真实 NetEq/PLC 异常

以下特殊情况必须分开记录，不能统一写成“SDK 编解码串扰”，也不能统一当成测试误报：

1. **固定频率点和可变窗口会误报。** `full_regression.py` 的 `tone_spectrum()` 只检查 300、470、640……1490Hz 固定频率点，且窗口长度随输出文件长度变化。非频率对齐窗口会产生泄漏；NetEq 时间拉伸/重采样还会使实际主峰偏移，例如 470Hz→约 475Hz、640Hz→约 633Hz。原算法可能漏掉偏移后的真实主峰，再把较小的 300Hz 分量误判为主频。本次全量三个失败的全频谱主峰仍位于正确声道，因此原始 45/48 不能直接解释成三个真实声道映射失败。
2. **启动和稳定阶段必须分开。** 旧 `round-2` 在 250ms 窗口的其他声道出现约 48%～91% 的 300Hz/主峰比例，但 500ms 后基本消失；通过组在启动窗口也可能出现较高比例。单次短文件结果会随窗口起点、长度和补偿过程变化，不能外推为持续串音。
3. **真实强 300Hz 瞬态确实存在，不能因门禁有缺陷就全部放行。** `framed-round-05` 的实际 OPUSMC 包离线解码在 500ms 时各声道正常，但 RTC/NetEq PCM 在相同阶段全部声道被 300Hz 主导，750ms 窗口恢复正常。这是码流对照确认的真实接收输出异常。
4. **异常与接收端补偿同步。** 该轮 PCM receiver 记录 `expand=332ms`、首秒 `loss decode=15`、`lost frame rate=19`，并发生音频冻结/恢复状态变化；同轮 encoded receiver 为 `lost frame rate=0、jitter=0`，编码帧 `codec=13` 且起始时间戳连续。强 300Hz 因此定位在接收端网络交付、NetEq/PLC 或后续 PCM 处理路径，而不是发送编码码流。
5. **直接时域相关性不是独立判据。** `pcm_channel_analyzer` 已运行，但 Opus 相位、起始偏移和 NetEq 时间拉伸都会降低源到接收 NCC；通过和失败样本都可能返回低相关性。分析器输出应与全频谱、实际码流和 SDK 日志共同使用。

当前根因边界是：**发送 PCM、WAV 读取、16bit 转换和 OPUSMC 编码码流均未产生强串音；真实异常发生在接收端 NetEq/PLC 补偿链路。** 具体由 SDK 哪个内部补偿模块生成或复制 300Hz，尚未通过源码或二进制路径证实，仍需 SDK 团队进一步定位。

专项采集时为每个编码包临时增加 4 字节长度前缀以便离线解码；诊断源码已恢复并重新编译，最终仓库不保留该临时改动。全部产物约 683MB，位于 `/private/tmp/agora-linux-p0-rerun.u0wnJs/`：

- `full/results.json`、`full-run.log`：完整 48 项结果和日志；
- `focused16/focused-results.json`、`dual16/dual-results.json`：WAV/raw 与双接收端结果；
- `framed16/framed-results.json`：实际码流采集和离线解码结果；
- `analysis/framed16/framed-round-05/comparison-500ms.png`：离线码流与 NetEq PCM 的直接对比图；
- `decrypted/`：14 份解密 `agorasdk.log`，包含通过、异常及历史对照。

### 频谱门禁修正及修正后全量复跑

`full_regression.py` 已改为以下判据，不再使用固定频率点加可变长度窗口：

- 使用固定 500ms Hann 窗和纯 Python FFT，在预期频率 `max(6Hz, 2%)` 容差内寻找全频谱局部主峰，兼容 NetEq 时间拉伸造成的合理频移。
- 除 500ms 主窗口外，补扫两个 250ms 子窗口，避免短时强 300Hz 被长窗口稀释。非第一声道的 300Hz/预期主峰比例不得超过 0.2，真实强串入仍判失败。
- 启动窗口从 250ms 开始，稳定窗口从 1000ms 开始，均为 500ms；采集目标从 1 秒增加到 2 秒。`startup_pass` 与 `steady_pass` 分别写入结果，整例要求两阶段都通过。

`full_regression_test.py` 现有 **15 项测试**，包括 7 项编码日志断言和 8 项频谱门禁测试，覆盖频率偏移、强/弱 300Hz、长窗口稀释的短时突发、启动/稳定分别失败、短文件和超容差频率；宿主机和 ARM64 容器均全部通过。历史样本离线验证确认：误报样本通过，旧真实异常及 `framed-round-05` 启动仍失败，稳定恢复和离线码流通过。

随后在 ARM64 Podman 中构建并通过正式 `full_regression.sh` 重跑完整 48 项，结果为 **44/48**：

- `tone-7ch-16bit`：startup 失败、steady 通过；独立 NumPy FFT 在 250ms 子窗口测得第 6 声道 300Hz 比例约 22.8%。
- `odd-3ch`：startup 失败、steady 通过；第 2/3 声道启动比例约 104%/86.7%。
- `odd-5ch`：startup 失败、steady 通过；第 2～5 声道启动比例约 44.5%～72.7%。
- `odd-7ch`：startup 失败、steady 通过；第 2～7 声道启动比例约 61.1%～91.7%。
- 原三项中 `tone-3ch-16bit`、`tone-8ch-16bit` 本轮通过，`odd-5ch` 仍因真实启动异常失败。所有 tone/odd 用例的 steady 阶段均通过，独立 FFT 复核未发现稳定阶段超限。

本轮产物位于 `/private/tmp/agora-linux-gate-fixed.guwa95/`：`full/results.json`、`full-run.log`、`unit-tests-final.log` 和 `numpy-verification.json`。44/48 是实际执行结果，不能因 steady 全部恢复而改写为 48/48。

### x86_64 对等回归

在独立 x86_64 主机的 Ubuntu 24.04.5 Docker 容器中，使用提交 `8a8536a` 和 Linux SDK 4.4.30.149 执行同一修正版脚本，未使用 ARM64 的二进制或结果替代：

- 全部 sample 和 `pcm_channel_analyzer` 构建通过；15 项单测、输入校验、PCM 分析器测试、生命周期离线 harness 和 shellcheck 均通过。
- 完整 **48/48** 通过，包含 4×4 媒体矩阵、3～8 声道 16bit、8 声道 24bit、奇数声道、codec、N→M 错配和 14 个非法输入。所有 tone/odd 用例均为 `startup_pass=true、steady_pass=true`。
- 普通 Opus 为 `codec=1`、OPUSMC 为 `codec=13`，未观察整体回退；`mismatch-8to3` 和 `mismatch-3to8` 均为 `degraded_to_requested_channels`。
- `mkv_exit_regression.sh` 的 normal、sender SIGINT、sender SIGTERM、receiver 先退出 **4/4** 通过，覆盖真实临时文件和 `/dev/null`。
- `lifecycle_regression.sh` 的发布/取消发布、订阅/取消订阅 **2 轮**通过，发送端同 UID 重进、接收端同 UID 重进和快速控制/退出信号均通过。
- 本轮没有失败样本；完整结果、日志、PCM/YUV 和独立 FFT 复核 `numpy-verification.json` 保存在本机 `/private/tmp/agora-linux-x86_64-peer.JNI33I/`，远端原始目录为 `/home/ubuntu/agora-x86_64-results/`。

x86_64 全绿只代表该次主机、架构和网络条件。ARM64 的四个 startup 强 300Hz 仍作为独立 NetEq/PLC 调查项保留，不能用本次 48/48 将其关闭。

### 扩展生命周期、N/M 与内容语义回归

本轮增加 `lifecycle_content_regression.py`、`mismatch_content_regression.py` 和 `semantic_regression.py`，并将 PCM/AV 的可选逐帧时间戳日志用于连续性检查。相关 **43 项离线测试**（18 项基础回归、9 项 N/M 内容、5 项生命周期内容、11 项语义分析）均通过。

#### 长生命周期和其他接收路径

- ARM64 的 PCM 发布/订阅及重进流程执行 10 轮，脚本和进程退出均成功；23 个初始/恢复/重进分析点中 **16/23 通过**。7 个失败均为实际频谱异常：6 个 startup 失败、1 个 steady 失败，没有采样不足。每次恢复保留 2 秒媒体，不能只凭文件增长或退出码判为内容恢复。
- x86_64 同样执行 10 轮，**23/23 恢复点的 startup/steady 全部通过**。
- ARM64 的 PCM、encoded、mixed、AV 四接收路径同时恢复时，取消发布/订阅期间 PCM 停止、其他接收端继续、重新发布后的内容均通过；但重新订阅后的 steady 第 2 声道出现约 24.1% 的 300Hz 比例，因此该矩阵整例仍失败。

#### N/M 声道错配内容

已覆盖 3～8 声道全部 **30 个有序 N≠M 组合**，不再只检查接收端请求的声道数：

- ARM64 **0/30** 通过；x86_64 首轮也只有 `8to6` 被标为 `rejected`，其余组合均出现混音、未匹配内容、声道缺失或重排。
- 对 x86_64 `8to6` 的日志复核发现当时接收端退出码为 **-11（SIGSEGV）**，并非安全拒绝。分类器已修正为只有显式 `PCM_FRAME_METADATA_MISMATCH` 才记 `rejected`，崩溃和其他非零退出记为 `error`。
- 定向复跑 `8to6` 时接收端退出码为 0，但仍有 `mixed_source_channels`，内容门禁失败。首轮的 SIGSEGV 属于间歇性异常，仍需单独调查，不能用本轮未崩溃关闭。
- 因此“按请求声道数输出”只代表 transport 行为，不能证明一对一内容映射、无复制或无错序。原有 8→3、3→8 也只能保留该传输层结论。

#### 混音、PCM/AV 连续性和 MKV 映射

- stereo 混音的奇偶声道隔离和 mono 的全部 8 个源频率检查在 **ARM64 和 x86_64 均通过**。
- ARM64 的 PCM/AV 时间戳连续性均通过：无回退、无异常大间隔，重复/非均匀 RTP 时间戳被单独记录，不把重复时间戳误判为丢帧；但两路 startup 频谱均失败，steady 通过，整例仍不能标为通过。
- x86_64 的 PCM/AV 连续性和频谱均通过。
- 合成 4 条独立 stereo 音轨的 MKV 实测中，x86_64 的 4×stereo→8ch 映射全部通过；ARM64 的 steady 映射通过，但 startup 出现跨声道 300Hz 异常，整例仍失败。文件非空不再作为 MKV 映射验收依据。

扩展证据位于：

- ARM64：`/private/tmp/agora-linux-mismatch-arm64.MICdXE/`、`/private/tmp/agora-linux-pcm-lifecycle-arm64-r4.qb8CJQ/`、`/private/tmp/agora-linux-lifecycle-arm64.LMLBfK/`、`/private/tmp/agora-linux-semantic-arm64-r2.Ffq5wo/`、`/private/tmp/agora-linux-mkv-mapping-arm64.LOKpDb/`。
- x86_64：`/private/tmp/agora-linux-mismatch-x86_64.iXEP5v/`、`/private/tmp/agora-linux-lifecycle-x86_64.Ug28zt/`、`/private/tmp/agora-linux-semantic-x86_64.MysvGk/`；`8to6` 定向复核另存于 `/private/tmp/agora-linux-mismatch-x86_64.iXEP5v/8to6-fixed/`。

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
- [full_regression_test.py](../Demo/linux/tests/full_regression_test.py)：共 18 项单测，其中 7 项编码日志断言覆盖 monitor/strict、warmup、非单调时间戳和 codec 错配，8 项频谱门禁测试覆盖局部主峰、频率容差、强/弱 300Hz、短时突发、启动/稳定阶段和短文件，3 项错配行为分类测试区分安全拒绝与崩溃。
- [lifecycle_content_regression.py](../Demo/linux/tests/lifecycle_content_regression.py) 及对应 5 项单测：执行 10 轮生命周期并逐点验收恢复内容，覆盖四接收路径。
- [mismatch_content_regression.py](../Demo/linux/tests/mismatch_content_regression.py) 及对应 9 项单测：覆盖全部 30 个有序 N≠M 组合，检查混音、复制、缺失、重排和未匹配内容。
- [semantic_regression.py](../Demo/linux/tests/semantic_regression.py) 及对应 11 项单测：验证 stereo 奇偶混音隔离、mono 全源、PCM/AV 连续性和合成 4×stereo MKV 映射。
- 本轮全量 C/C++ 回归见 [Linux 全量回归记录](linux_full_regression_2026-11-19.md)：主工程 9 个目标和 4 个独立解析器均编译通过，4×4 真实收发矩阵、生命周期、MKV teardown、N→M 降级和扩展非法输入通过；2026-11-23 的 24bit 和 codec 定向复跑结果见上节。

## 部分覆盖

### 声道内容分析器已完成，真实媒体门禁部分通过

逐声道分析器已经可以对 3/4/5/6/7/8 声道计算 RMS、峰值和源到接收的相关性，并以一对一映射检查顺序、丢失、复制和明显串音。独立频率的 16bit 合成 WAV 以及重新生成的 `odd_3ch.wav`、`odd_5ch.wav`、`odd_7ch.wav` 已通过帧对齐检查；原始 FFT 门禁也曾给出通过结果，但这些结果必须结合后续发现的窗口和时间拉伸限制重新解释。分析器适用于时间/相位已对齐的合成 PCM 对照，不能把真实 RTC 中未经对齐的 NCC 单独作为链路失败依据。

频谱门禁现已修正为固定窗口、全频谱局部主峰、250ms 串音子窗口和启动/稳定双阶段，15 项单测及历史样本验证通过。修正后的完整复跑为 **44/48**，四个失败全部是真实启动强 300Hz，稳定阶段均恢复；不能标成 48/48，也不能把这些失败继续归为旧门禁误报。

### 4×stereo 到 8ch 的布局

MKV sample 实现了 4×stereo 重采样、重排和 10ms 拆帧。真实 4×4 矩阵中的 MKV→AV 组合已收到完整 I420 帧并验证前两帧不同。新增合成 4 条独立 stereo 音轨的映射门禁后，x86_64 的 4×stereo→8ch 映射通过，ARM64 的 steady 映射通过、startup 仍有 300Hz 异常。文件非空和视频推进不再单独作为映射验收依据。

### 24bit 转换

24bit WAV 的本地单测已覆盖负值、极值和右移语义。2026-11-23 的真实链路逐声道频谱 4/4 通过，发送侧转换 PCM 与 16bit 参考逐字节一致，此前的 24bit 转换失败未再复现。16bit 后续调查进一步确认实际 OPUSMC 码流正常，强 300Hz 属于接收端 NetEq/PLC 瞬态，不能反向推翻 24bit 转换结果；24bit 仍不替代 x86_64 回归。

### 混音和编码帧

混音路径已验证 stereo 奇偶声道隔离和 mono 的全部 8 个源频率，双架构均通过。编码帧路径已按 UID 分文件并验证普通 Opus `codec=1`、OPUSMC `codec=13`。PCM/AV 新增可选逐帧时间戳日志，允许重复及非均匀 RTP 时间戳，但拒绝回退和异常大间隔；x86_64 的连续性和频谱均通过，ARM64 的时间戳连续性通过、startup 频谱仍失败。AV 视频路径继续检查完整 I420 帧、前两帧差异和视频时间戳递增。

### 生命周期和架构

正常退出、无效凭据清理和生命周期竞态已有 ARM64 真实验证。MKV 发送端和 AV 接收端现已统一使用原子退出标志、SIGINT/SIGTERM 处理、线程失败传播、可中断视频 PTS 等待和完整 teardown；`mkv_exit_regression.sh` 的正常退出、sender SIGINT、sender SIGTERM、receiver 先退出 4 个场景均通过。仍需在 x86_64 复测，并扩大到更多收发路径和长时间竞态，确认无卡死、残留线程和未关闭文件。

ARM64 与 x86_64 均已有实际容器回归：x86_64 完整 48/48，ARM64 修正后 44/48 且四个失败均为 startup 异常。双架构覆盖已经建立，但 NetEq/PLC 瞬态和更多网络条件仍需单独验证。

## 缺失场景与优先级

表格中的“已实现”只表示代码或本地测试已经具备；只有“真实回归通过”才表示在有效凭据、实际 SDK 和网络链路上完成了验收。

| 优先级 | 场景 | 当前状态 | 下一步验收 |
| --- | --- | --- | --- |
| P0 | 逐声道内容正确性 | 频谱门禁已修正并有 15 项单测；x86_64 完整 48/48、16/24bit 和 startup/steady 全部通过；ARM64 修正后 44/48，4 个失败均为真实启动强 300Hz，steady 全部通过 | 继续由 SDK 团队定位 ARM64 NetEq/PLC 内部补偿模块，在受控网络复测并保留码流离线对照；不得用 x86_64 全绿抵消 ARM64 异常 |
| P0 | 可重复的 Linux 多声道矩阵 | 修正版 `full_regression.sh`/`full_regression.py` 已在 x86_64 完成 48/48，在 ARM64 完成 44/48；两边均有 startup/steady、结果 JSON、日志和媒体证据 | 保留双架构独立结果，继续覆盖更多网络条件；不能把 x86_64 的 48/48 或任一架构的 steady 通过外推为所有环境通过 |
| P0 | 非法输入和参数组合 | 输入校验代码与本地单测已完成；ARM64 和 x86_64 的 14 个真实端到端场景均在 `onConnecting` 前以退出码 255 拒绝 | 继续覆盖更多参数组合、随机截断和 parser fuzz，保持无发布、无远端文件的证据 |
| P0 | MKV 音视频退出稳定性 | `mkv_exit_regression.sh` 的 normal、sender SIGINT、sender SIGTERM、receiver 先退出在 ARM64 和 x86_64 均通过，覆盖真实临时文件和 `/dev/null` | 在更多收发路径和长稳中复测，继续确认无卡死、残留线程、未关闭文件和异常子进程 |
| P0（条件） | ARM64/x86_64 对等回归 | 两架构均已执行相同修正版媒体、输入校验和退出矩阵：x86_64 48/48，ARM64 44/48 且四个失败均为 startup 异常 | 保留分架构结果，继续调查 ARM64 NetEq/PLC 瞬态；新增架构或网络条件时复跑同一矩阵 |
| P1 | codec fallback / `codec_unfallback` | 2026-11-23 monitor 定向 13/13 项断言通过，普通流保持 `codec=1`、多声道流保持 `codec=13`，未观察整体回退；严格连续性在当前网络为 9/13 | 在可控无丢包网络执行严格连续性门禁，并补 capability/远端媒体证据，明确 SDK 实际协商行为 |
| P1 | 生命周期和订阅竞态 | 已扩至 10 轮及逐点内容验收：x86_64 23/23 恢复点通过，ARM64 16/23 通过且 7 个失败均为实际频谱异常；ARM64 四接收矩阵另有 resubscribe steady 异常 | 继续在更多网络条件和长时间竞态下复测，保留每次恢复的 PCM、频谱和日志，不能只检查文件增长 |
| P1 | 接收端声道数错配 | 已覆盖全部 30 个有序 N≠M 组合：两架构均无组合通过一对一内容门禁；x86_64 `8to6` 首轮还出现 SIGSEGV，已修正分类器避免把崩溃误记为安全拒绝 | 由 SDK 团队调查错配混音/映射和间歇性崩溃；保留每次 PCM、频谱和日志，transport 的 `degraded_to_requested_channels` 不代表内容正确 |
| P1 | 网络和认证恢复 | 已捕获真实丢包下 NetEq/PLC 产生强 300Hz 瞬态及恢复的码流证据；Token 过期/续期、可控丢包/抖动和完整恢复矩阵仍未覆盖 | 覆盖 Token 过期/续期、断网、受控丢包/抖动和重连，分别记录补偿瞬态与稳定后的声道数、频谱和内容恢复 |
| P1 | 混音/编码/AV 语义断言 | stereo 奇偶隔离和 mono 全源双架构通过；x86_64 PCM/AV 连续性与 MKV 映射通过；ARM64 时间戳连续性及 MKV steady 通过，但 startup 频谱仍失败 | 继续调查 ARM64 启动 NetEq/PLC 异常，并在更多网络条件下验证 PCM/AV 连续性和 MKV 映射 |
| P2 | 长稳和资源 | 没有长时间自动化记录 | 连续发布订阅至少 2 小时，观察 CPU、内存、线程、文件句柄和 SDK 日志增长 |
| P2 | 文件输出边界 | 已有部分分片和路径安全逻辑，异常磁盘场景未覆盖 | 覆盖 100MB 分片、磁盘写满、权限不足、UID 特殊字符、输出冲突和多远端并发写盘 |
| P2 | 解析器和帧边界 fuzz | WAV chunk、Extensible PCM、PCM 尾部和 Opus 非整帧单测已完成 | 增加 parser fuzz/property tests，重点检查任意截断、超大 chunk、奇数对齐和随机扩展 chunk 不崩溃、不越界 |

## 当前遗留问题与后续顺序

以下 8 项均为尚未完成的事项，分为需要 SDK 团队定位的实际问题和后续验证/覆盖工作，不能用 x86_64 全绿、transport 成功或文件非空代替验收。

### 需要 SDK 团队定位

1. **P0：NetEq/PLC 内部补偿机制。** 将 `framed-round-05`、ARM64 四个启动失败及扩展回归中的频谱异常交给 SDK 团队，明确具体产生模块和触发条件；在可控无丢包及受控丢包环境复测，并保留 encoded 包离线解码对照。不能用 x86_64 的 48/48 抵消 ARM64 的 44/48 异常。
2. **N/M 错配映射和间歇性 SIGSEGV。** 全部 30 个有序 N≠M 组合均未通过一对一内容验收，需要继续调查混音、复制、缺失和重排；x86_64 `8to6` 首轮还出现 SIGSEGV，后续未复现不能关闭问题，必须保留原始崩溃日志和后续内容失败证据。

### 继续验证和扩大覆盖

3. **严格时间戳连续性及 codec 协商证据。** 在可控无丢包网络执行 `--strict-timestamp-continuity`，此前 strict 为 9/13；继续区分网络、容器调度和 SDK transport 排队造成的缺口。普通 Opus 对端未触发 fallback 时，补 capability 和远端媒体证据。
4. **更多网络条件下的生命周期与内容恢复。** 复测发布/订阅、重进、混音、PCM/AV 连续性和 MKV 映射，保留每次恢复的 PCM、频谱和日志，不能只检查文件增长；ARM64 四接收矩阵的 resubscribe steady 异常也需继续定位。
5. **Token 与网络恢复矩阵。** 覆盖 Token 过期/续期、断网、受控丢包/抖动和重连，分别记录补偿瞬态与稳定后的声道数、频谱和内容恢复。
6. **长稳与资源。** 连续发布订阅至少 2 小时，观察 CPU、内存、线程、文件句柄和 SDK 日志增长，并复测 MKV 退出和长时间竞态。
7. **文件输出边界。** 覆盖 100MB 分片、磁盘写满、权限不足、UID 特殊字符、输出冲突和多远端并发写盘。
8. **解析器 fuzz/property tests。** 覆盖任意截断、超大 chunk、奇数对齐和随机扩展 chunk，确认不崩溃、不越界，同时保持非法输入无发布、无远端文件的证据。

建议顺序：NetEq/PLC 调查 → N/M 映射与 SIGSEGV 调查 → strict/codec 验证 → 网络恢复与长稳 → 文件边界和 fuzz。

非测试待办：`Resource/` 仍为未跟踪目录，未包含在提交中；如需长期保留在本机，应评估是否加入 `.gitignore`，避免误提交其中的凭据。

验收时应遵守统一口径：join/publish 成功、本端发送统计和日志只能作为辅助证据；Native/Linux PCM 必须检查实际声道数和逐声道内容。对 Opus/NetEq 输出，严格 NCC 只适用于时间和相位已对齐的比较；频谱应检查全频谱局部主峰、跨声道比例和多个时间窗。怀疑编码串音时，应保存实际编码包并离线解码，先确认异常是否已存在于码流。NetEq/PLC 启动或丢包补偿瞬态必须单独记录，既不能因随后恢复而删除异常，也不能用单次短窗口结果认定持续产品缺陷。编码帧、混音文件和音视频文件分别代表不同验证层级，不能互相替代。
