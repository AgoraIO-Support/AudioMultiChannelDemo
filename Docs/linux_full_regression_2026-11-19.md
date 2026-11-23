# Linux 全量 C/C++ 回归：2026-11-19

本轮在 Podman 的 Ubuntu 24.04.5 ARM64 容器中执行，使用 Linux SDK 4.4.30.149 和未启用电子证书的 App ID `43f818097c124394821bd44598b7ca9d`（无需 token）。测试镜像为 `localhost/agora-ubuntu-regression:24.04`。

## 编译覆盖

- CMake Debug + coverage 构建成功，9 个目标全部生成：4 个 sender、4 个 receiver、`pcm_channel_analyzer`。
- `Demo/linux` 下 24 个 C/C++ 源文件均完成编译：主工程 17 个、3 个测试程序、4 个未接入 CMake 的 `common/file_parser` 解析器。解析器使用 SDK 自带 opusfile 头文件单独编译通过。
- `shellcheck` 检查现有回归脚本和生命周期脚本通过，`git diff --check` 通过。

## 离线测试

以下测试通过：

- 输入校验测试：WAV chunk、Extensible PCM、非法格式、声道/采样率/位深错配、短 PCM、非整帧 Opus。
- PCM 逐声道分析器测试。
- 私有 PCM 文件目录权限、重复文件和符号链接防护测试。
- 生命周期脚本离线正例，以及“取消订阅后仍持续收到 PCM”和“SDK fatal 日志”两个反例。

5 个非法参数端到端场景也通过：sender 声道数、WAV 声道错配、payload 错配、receiver 采样率和 mixed receiver 声道数均在 `onConnecting` 前以退出码 255 拒绝。

## 真实媒体回归

### 4×4 收发矩阵

4 种 sender（raw PCM、WAV、预编码 Opus、MKV）分别连接 4 种 receiver（解码 PCM、编码帧、混音 PCM、AV），共 16 个真实组合全部通过。每个组合的 sender/receiver 退出码为 0，输出文件有数据且无应用错误或 SDK fatal 日志。

- 编码帧接收均记录 `codec=13`（OPUSMC）。
- PCM 和混音输出均有非零 RMS；预编码 Opus 输出编码帧和解码 PCM 均正常增长。
- MKV→AV 输出 68,428,800 字节，即 22 个完整的 1920×1080 I420 帧；前两帧内容不同。

### 生命周期、退出和认证失败

- 生命周期真实回归（2 轮）：同一连接反复取消/重新发布、取消/重新订阅、sender 同 UID 重进、receiver 同 UID 重进和快速信号退出，全部通过。
- `mkv_exit_regression.sh` 的 normal、sender SIGINT、sender SIGTERM、receiver 先退出 4 个场景全部通过。
- `connection_failure_regression.sh` 的 4 个 sender 无效 App ID 超时清理全部通过。

### 逐声道内容专项

- 使用独立频率的合成 16bit WAV，3/4/5/6/7/8 声道输出的直接 FFT 检查均能找到对应主频（约 300、470、640、810、980、1150、1320、1490 Hz），各路 RMS 保持在可用范围。
- 仓库中的 `odd_3ch.wav`、`odd_5ch.wav`、`odd_7ch.wav` 长时间回归没有通过现有 `pcm_channel_analyzer`：输出文件长度分别对 `N×16bit` 帧大小余 4、8、12 字节，且分析结果出现声道内容不匹配。该结果表明奇数声道 WAV 的实际回调帧布局仍需调查，不能标记为真实内容通过。
- 独立频率的 24bit WAV 能产生 8 声道输出文件，但接收内容的各路频谱出现相同/错误的主频，未通过逐声道内容门禁。24bit 文件生成和传输通过，符号扩展、右移 8bit 后的声道语义仍未验收通过。

## 结果和限制

主工程编译、4×4 实际媒体链路、非法参数、生命周期、MKV teardown 和无效凭据清理均已在 ARM64 Ubuntu 通过。奇数声道仓库素材的实际 PCM 帧布局和 24bit 的逐声道内容仍是失败项；它们不能用“文件非空”替代验收。x86_64、本轮之外的长稳、Token 续期、断网恢复和 fuzz 尚未执行。

本轮主要日志、输出 PCM/YUV 和中间结果保存在本机临时目录：`/private/tmp/agora-full-linux-20261119.rfID77/`。其中 `media.log`、`results/media/results.json`、`results/oddlong/`、`results/failure/`、`results/lifecycle.log` 和 `results/mkv-exit.log` 对应上述场景。

## 2026-11-23 后续复跑与历史口径修正

以上内容是 2026-11-19 的历史快照，后续结果如下，不能据此把历史失败或后续通过混写成同一轮结果：

- 24bit 独立频率 8 声道用例后续连续 **4/4** 通过，发送侧转换 PCM 与 16bit 参考逐字节一致，SHA-256 相同。此前“24bit 转换语义未通过”不再作为当前开放项，但不替代 x86_64 回归。
- 奇数声道 WAV 的输出已按完整 PCM 帧对齐。2026-11-23 的更新后全量复跑中，`odd-3ch`、`odd-7ch` 按原频谱门禁通过，`odd-5ch` 仍失败，需按下述频谱特殊情况重新验收。
- 在提交 `d10a5ff`、Ubuntu 24.04.5 ARM64 Podman 和 SDK 4.4.30.149 下，通过正式 `tests/full_regression.sh` 完成全量复跑，原始结果为 **45/48**。失败为 `tone-3ch-16bit`、`tone-8ch-16bit`、`odd-5ch`；其余 4×4 基础矩阵、24bit、codec monitor、错配和非法输入通过。结果与日志保存在 `/private/tmp/agora-linux-p0-rerun.u0wnJs/full/`。

### 频谱失败与 NetEq/PLC 特殊情况

后续 WAV/裸 PCM 配对、双接收端和实际码流对照发现，频谱结果包含两类不同情况：

1. **测试门禁误报**：固定频率点 DFT 使用可变长度窗口，未考虑非频率对齐泄漏及 NetEq 时间拉伸。实际主峰可能从 470Hz 偏至约 475Hz、640Hz 偏至约 633Hz，固定频率点会漏掉真实主峰。上述三个全量失败的全频谱主峰仍在正确声道，原始 45/48 不能直接解释成三个真实声道映射失败，也不能未经修正门禁复跑就改写成 48/48。
2. **真实接收补偿瞬态**：6 轮实际 OPUSMC 包离线解码全部成功，250ms、500ms 窗口各声道正常，其他声道 300Hz 分量低于主峰 1%。但 `framed-round-05` 的 RTC/NetEq PCM 在 500ms 全部声道被 300Hz 主导，750ms 恢复；PCM receiver 同期有 `expand=332ms`、首秒 `loss decode=15`、`lost frame rate=19` 和冻结/恢复状态变化，同轮 encoded receiver 为 `lost frame rate=0、jitter=0`。强 300Hz 因此发生在接收端网络交付、NetEq/PLC 或后续 PCM 路径，不是 Demo 输入或 OPUSMC 编码码流串音。

直接时域 NCC 也受 Opus 相位、起始偏移和 NetEq 时间拉伸影响，不能独立作为通过或失败依据。具体由 SDK 哪个内部补偿模块产生强 300Hz 尚未证实，仍需 SDK 团队调查。完整证据、结果解释和产物索引见 [Linux 多声道 Demo 测试覆盖评估](comments_for_linux_demo.md)。

## 2026-11-23 频谱门禁修正与再次全量复跑

`full_regression.py` 随后完成门禁修正：固定 500ms Hann 窗、FFT 局部主峰、预期频率 `max(6Hz, 2%)` 容差、两个 250ms 串音子窗口、300Hz/预期主峰比例上限 0.2，以及 250ms/1000ms 启动/稳定双阶段。采集目标增加到 2 秒，整例要求两阶段都通过。`full_regression_test.py` 的 **15 项测试**在宿主机和 ARM64 容器全部通过，历史样本验证保留了真实强 300Hz 失败，没有靠放宽阈值消除。

在同一 ARM64 Ubuntu/Podman 环境通过正式 `full_regression.sh` 再次实际执行完整 48 项，结果为 **44/48**：

| 用例 | startup | steady | 独立 NumPy FFT 复核 |
| --- | --- | --- | --- |
| `tone-7ch-16bit` | 失败 | 通过 | 第 6 声道启动 300Hz 比例约 22.8% |
| `odd-3ch` | 失败 | 通过 | 第 2/3 声道约 104%/86.7% |
| `odd-5ch` | 失败 | 通过 | 第 2～5 声道约 44.5%～72.7% |
| `odd-7ch` | 失败 | 通过 | 第 2～7 声道约 61.1%～91.7% |

原 `tone-3ch-16bit`、`tone-8ch-16bit` 本轮通过，`odd-5ch` 仍失败。所有 tone/odd 用例的 steady 阶段均通过。该结果说明旧门禁误报已修正，同时新的双阶段检查捕获了更多真实启动补偿异常；不能把 44/48 改写成 48/48，也不能把 steady 恢复等同于 startup 没有异常。

本轮结果和证据位于 `/private/tmp/agora-linux-gate-fixed.guwa95/`，包括 `full/results.json`、`full-run.log`、`unit-tests-final.log` 和 `numpy-verification.json`。
