# Linux 多声道 Demo 测试覆盖评估

评估日期：2026-11-19

评估范围：`Demo/linux/` 下的发送端、接收端、测试脚本、素材和现有回归记录。本文区分“代码提供了测试入口”“已有实际回归证据”和“已形成可重复自动化测试”，避免把 sample 存在误认为功能已经完整验证。

## 结论

Linux Demo 的功能入口比较完整，已经覆盖 4 类发送路径和 4 类接收路径。但当前 `Demo/linux/tests/` 只有连接失败回归和临时 PCM 文件安全测试，没有可重复的多声道媒体正确性分析器。

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
- [mkv_exit_regression.sh](../Demo/linux/tests/mkv_exit_regression.sh)：在提供 `APP_TOKEN` 后重复运行正常退出、sender SIGINT、sender SIGTERM、receiver 先退出，并分别覆盖真实临时文件和 `/dev/null`。

## 部分覆盖

### 声道内容只验证到“非静音”

当前回归检查每个声道是否有信号，但没有自动比较源文件和接收文件的声道对应关系。现有记录也明确说明，这不能证明逐声道内容、声像定位或无串音。

### 4×stereo 到 8ch 的布局

MKV sample 实现了 4×stereo 重采样、重排和 10ms 拆帧，跨端记录也有 4×stereo 映射证据。但 Linux 接收端没有独立的逐声道内容 oracle，不能仅凭文件可播放或各声道有能量确认映射正确。

### 24bit 转换

24bit WAV 已经可以发送并在接收端产生 8 路信号，但还没有验证符号扩展、右移 8bit 后的数值误差和极值样本。

### 混音和编码帧

混音路径已经验证文件生成，但还没有验证多声道流的奇数/偶数声道混音内容、mono 输出和多用户混音隔离。编码帧路径已经验证按 UID 有文件增长，但还没有检查 codec、时间戳连续性、帧间隔和异常帧。

### 生命周期和架构

正常退出及无效凭据清理已有验证。MKV 发送端和 AV 接收端现已统一使用原子退出标志、SIGINT/SIGTERM 处理、线程失败传播、可中断视频 PTS 等待和完整 teardown；仍需在具备有效凭据的 ARM64/x86_64 环境按 P0 矩阵重复运行确认运行时行为。

当前实际容器回归只覆盖 ARM64；x86_64 仍需要执行同一套回归才能支撑 README 中的双架构支持声明。

## 缺失场景与优先级

| 优先级 | 缺失场景 | 建议验收标准 |
| --- | --- | --- |
| P0 | 逐声道内容正确性 | 对 3/4/5/6/7/8ch 计算每路 RMS、峰值和与源声道的相关性，检查顺序、丢失、复制和串音 |
| P0 | 可重复的 Linux P0 矩阵 | 将 4 类发送端 × 4 类接收端、偶数/奇数声道、16/24bit 纳入可重跑脚本；结果区分通过、错配、限制和失败 |
| P0 | 非法输入和参数组合 | 覆盖 malformed/truncated WAV、44.1kHz、8/32bit/float、2/9ch、WAV 头与 CLI 不一致、payload 与声道不匹配、短 PCM、非整帧 Opus；确认连接前拒绝且不发布 |
| P0 | MKV 音视频退出稳定性 | 代码已覆盖统一 teardown、SIGINT/SIGTERM、发送/接收线程失败传播和 `/dev/null` 输出路径；仍需重复测试正常退出、信号退出、发送端先退出、接收端先退出、真实写盘，确认无卡死、残留线程和未关闭文件 |
| P0（条件） | x86_64 与 ARM64 对等回归 | 如果 x86_64 是正式支持目标，两个架构执行相同的媒体、输入和清理矩阵 |
| P1 | codec fallback / `codec_unfallback` | 加入不支持多声道的端，验证普通 Opus 回退；开启强制不回退后验证 codec、远端媒体和失败行为 |
| P1 | 生命周期和订阅竞态 | 覆盖停止/重发、离开/重进、远端离开/重新发布、重复订阅/取消订阅、快速信号退出，检查无残留回调、线程和文件 |
| P1 | 接收端声道数错配 | 发送 N 声道、接收端配置 M 声道，明确拒绝、降级或输出行为，不只看进程退出码 |
| P1 | 网络和认证恢复 | 覆盖 Token 过期/续期、物理断网、弱网/丢包/抖动、重连后声道数和媒体恢复 |
| P1 | 混音/编码/AV 语义断言 | 验证奇偶声道混音、mono 输出、编码帧时间连续性、视频帧尺寸/持续前进和音频文件连续性 |
| P2 | 长稳和资源 | 执行 2 小时以上连续发布订阅，观察 CPU、内存、线程、文件句柄和 SDK 日志增长 |
| P2 | 文件输出边界 | 覆盖 100MB 分片、磁盘写满、权限不足、UID 特殊字符、输出路径冲突和多远端并发写盘 |
| P2 | 解析器和帧边界 | 已加入 WAV RIFF 扩展 chunk、奇数 chunk 对齐、截断 chunk、非 PCM extensible、PCM 尾部补零和 Opus 非整帧单元测试；后续可继续增加 fuzz 覆盖 |

## 建议的第一阶段落地顺序

1. 增加一个 Linux 端到端回归脚本，统一启动 sender/receiver、分配 UID、保存独立日志，并等待完整素材循环结束。
2. 增加 PCM 分析器：确认采样率、位深、声道数，计算逐声道峰值/RMS，并用 Channel ID 素材做源到接收声道的相关性比较。
3. 增加非法输入回归：自动生成或准备错误 WAV、短 PCM、非整帧 Opus 和参数错配组合，断言连接前失败。
4. 将 MKV 退出卡死和 x86_64 架构加入发布门禁。
5. 再补生命周期、fallback、网络恢复和长稳测试。

验收时应遵守统一口径：join/publish 成功、本端发送统计和日志只能作为辅助证据；Native/Linux PCM 必须检查实际声道数和逐声道内容。编码帧、混音文件和音视频文件分别代表不同验证层级，不能互相替代。
