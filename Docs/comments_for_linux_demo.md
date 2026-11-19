# Linux 多声道 Demo 测试覆盖评估

评估日期：2026-11-19

评估范围：`Demo/linux/` 下的发送端、接收端、测试脚本、素材和现有回归记录。本文区分“代码提供了测试入口”“已有实际回归证据”和“已形成可重复自动化测试”，避免把 sample 存在误认为功能已经完整验证。

## 结论

Linux Demo 的功能入口比较完整，已经覆盖 4 类发送路径和 4 类接收路径。`Demo/linux/tests/` 现已包含连接失败回归、私有 PCM 文件安全测试、输入校验单测、PCM 逐声道内容分析单测和 MKV 退出回归脚本；`Demo/linux/out/pcm_channel_analyzer` 提供真实接收文件的分析入口。

目前最大的验证风险是把“收到数据、每个声道有非静音信号”当成多声道正确。这个判据不能发现声道顺序错误、串声道、声道复制或声道丢失。

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

2026-11-18 的 ARM64 Ubuntu 容器回归已验证：

- WAV 3、4、5、6、7、8 声道到 PCM 接收；
- 24bit 8 声道 WAV 转换和接收；
- PCM 与预编码 Opus 双发送者按 UID 分文件接收；
- 双发送者编码帧接收；
- Opus 到混音接收；
- MKV 到音频加 I420 视频接收；
- 四种发送端使用无效凭据时超时退出并完成清理。

详见 [linux_ubuntu_regression_2026-11-18.md](linux_ubuntu_regression_2026-11-18.md)。

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

## 部分覆盖

### 声道内容分析器已完成，真实媒体门禁仍待执行

逐声道分析器已经可以对 3/4/5/6/7/8 声道计算 RMS、峰值和源到接收的相关性，并以一对一映射检查顺序、丢失、复制和明显串音。当前缺的是在有效凭据下运行各发送/接收组合，把真实落盘 PCM 交给分析器并保存通过结果；Opus 编码、网络抖动和启动延迟也需要按场景选择比较窗口或 `--source-offset-ms`。合成测试不能替代真实 SDK、网络和解码链路验证。

### 4×stereo 到 8ch 的布局

MKV sample 实现了 4×stereo 重采样、重排和 10ms 拆帧，跨端记录也有 4×stereo 映射证据。逐声道分析器现在可以作为 Linux 接收端的内容 oracle，但仍需用真实 MKV 接收文件执行并保存结果。

### 24bit 转换

24bit WAV 已经可以发送并在接收端产生 8 路信号，但还没有验证符号扩展、右移 8bit 后的数值误差和极值样本。

### 混音和编码帧

混音路径已经验证文件生成，但还没有验证多声道流的奇数/偶数声道混音内容、mono 输出和多用户混音隔离。编码帧路径已经验证按 UID 有文件增长，但还没有检查 codec、时间戳连续性、帧间隔和异常帧。

### 生命周期和架构

正常退出及无效凭据清理已有验证。MKV 发送端和 AV 接收端现已统一使用原子退出标志、SIGINT/SIGTERM 处理、线程失败传播、可中断视频 PTS 等待和完整 teardown；`mkv_exit_regression.sh` 已覆盖正常退出、sender SIGINT、sender SIGTERM、receiver 先退出、真实临时文件和 `/dev/null`。仍需在具备有效凭据的 ARM64/x86_64 环境执行脚本，确认运行时无卡死、残留线程和未关闭文件。

当前实际容器回归只覆盖 ARM64；x86_64 仍需要执行同一套回归才能支撑 README 中的双架构支持声明。

## 缺失场景与优先级

表格中的“已实现”只表示代码或本地测试已经具备；只有“真实回归通过”才表示在有效凭据、实际 SDK 和网络链路上完成了验收。

| 优先级 | 场景 | 当前状态 | 下一步验收 |
| --- | --- | --- | --- |
| P0 | 逐声道内容正确性 | 分析器、命令行入口和 3/4/5/6/7/8ch 合成测试已完成；真实媒体尚未形成结果记录 | 对每个声道数运行 WAV/PCM 发送和 `sample_receive_pcm_multiopus`，用 `pcm_channel_analyzer` 检查声道数、RMS、峰值、映射、丢失、复制和串音 |
| P0 | 可重复的 Linux 多声道矩阵 | 发送/接收 sample 和素材齐全，但还没有统一编排 4×4 组合的脚本 | 固定 UID、日志、输出目录和超时，覆盖 4 类 sender × 4 类 receiver、3/4/5/6/7/8ch、16/24bit，并区分通过、错配、限制和失败 |
| P0 | 非法输入和参数组合 | 输入校验代码与本地单测已完成，包含 malformed/truncated WAV、采样率/位深/声道数错配、短 PCM 和非整帧 Opus | 用真实二进制逐项执行，确认在 `connect()` 前退出、无发布、无远端文件，并记录退出码和日志 |
| P0 | MKV 音视频退出稳定性 | 代码和 `mkv_exit_regression.sh` 已覆盖 teardown、正常退出、SIGINT/SIGTERM、发送端/接收端先退出、真实文件和 `/dev/null` | 使用有效 `APP_TOKEN` 在 ARM64、x86_64 各执行多轮，确认无卡死、残留线程、未关闭文件和异常子进程 |
| P0（条件） | ARM64/x86_64 对等回归 | 当前实际容器证据主要是 ARM64；分析器单测可跨平台编译 | 若两种架构都是支持目标，执行相同媒体、输入校验、逐声道分析和退出矩阵 |
| P1 | codec fallback / `codec_unfallback` | sender 参数入口和 `setArray`/`setInt` 返回值检查已完成；没有不支持多声道端参与的真实协商记录 | 加入普通 Opus 对端，分别验证默认回退和 `--codecUnfallback` 强制不回退时的 codec、远端媒体、声道数和失败行为 |
| P1 | 生命周期和订阅竞态 | 基础退出和无效凭据清理已有覆盖，重复发布/订阅场景未形成脚本 | 覆盖停止/重发、离开/重进、远端离开/重新发布、重复订阅/取消订阅和快速信号退出，检查回调、线程和文件 |
| P1 | 接收端声道数错配 | CLI 已拒绝部分非法配置，真实 N→M 输出行为尚未系统记录 | 发送 N 声道并以 M 声道接收，明确拒绝、降级或输出行为；验收必须包含实际文件声道数和内容 |
| P1 | 网络和认证恢复 | 当前主要覆盖无效凭据超时，未覆盖恢复链路 | 覆盖 Token 过期/续期、断网、丢包/抖动和重连，确认声道数与媒体内容恢复 |
| P1 | 混音/编码/AV 语义断言 | 混音文件、编码帧和 AV 文件已有生成验证，但缺内容级断言 | 验证奇偶声道混音、mono 输出、编码帧 codec/时间戳/帧间隔、视频帧尺寸/推进和音频连续性 |
| P2 | 长稳和资源 | 没有长时间自动化记录 | 连续发布订阅至少 2 小时，观察 CPU、内存、线程、文件句柄和 SDK 日志增长 |
| P2 | 文件输出边界 | 已有部分分片和路径安全逻辑，异常磁盘场景未覆盖 | 覆盖 100MB 分片、磁盘写满、权限不足、UID 特殊字符、输出冲突和多远端并发写盘 |
| P2 | 解析器和帧边界 fuzz | WAV chunk、Extensible PCM、PCM 尾部和 Opus 非整帧单测已完成 | 增加 parser fuzz/property tests，重点检查任意截断、超大 chunk、奇数对齐和随机扩展 chunk 不崩溃、不越界 |

## 建议的第一阶段落地顺序

1. 先固定 ARM64 的 P0 矩阵，逐项运行 `pcm_channel_analyzer` 并保存 3/4/5/6/7/8ch 的真实映射和统计结果。
2. 在同一环境执行非法输入端到端验证和 `mkv_exit_regression.sh`，保留日志、退出码和文件清单。
3. 在 x86_64 执行相同媒体、输入校验、逐声道分析和退出场景。
4. 增加真实 codec fallback/`codec_unfallback` 协商测试，断言 codec、远端媒体和失败行为。
5. 补生命周期、接收端错配、网络恢复、混音/编码/AV 内容断言、长稳和 parser fuzz 测试。

验收时应遵守统一口径：join/publish 成功、本端发送统计和日志只能作为辅助证据；Native/Linux PCM 必须检查实际声道数和逐声道内容。编码帧、混音文件和音视频文件分别代表不同验证层级，不能互相替代。
