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
