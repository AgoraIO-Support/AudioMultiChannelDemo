# Linux 生命周期回归：2026-11-19

> 版本说明：仓库当前携带 SDK 已更新为 4.4.30.152 ZIP；本文结果为对应历史版本 4.4.30.149 的回归，不把包升级视为新 SDK 已验证。

在 Podman Ubuntu 24.04 ARM64 容器中，使用 Agora Linux SDK 4.4.30.149、未开启证书的
App ID 和真实 RTC 网络运行 `Demo/linux/tests/lifecycle_regression.sh`。样例为 8 声道、
48 kHz PCM sender 与按 UID 接收的 PCM receiver；发送端 UID 71001，接收端 UID 71002。

最终回归结果：

| 场景 | 结果 |
| --- | --- |
| 同一连接取消发布/重新发布，3 轮 | 通过；停止后文件不再增长，恢复后增长 |
| 同一连接取消订阅/重新订阅，3 轮 | 通过；停止后文件不再增长，恢复后增长 |
| 发送端退出后以相同 UID 重进 | 通过；收到 `onUserLeft`，重进后 PCM 恢复增长 |
| 接收端退出后以相同 UID 重进 | 通过；重新订阅并收到 PCM |
| 控制信号后立即发退出信号 | 双方按时退出且退出码为 0；不保证退出前控制信号一定被处理 |

最后一次接收文件分别为 1,059,840 和 184,320 字节。脚本检查各次 API 返回值、
相关回调、文件增长、进程退出码，以及进程日志中的应用错误和 SDK 日志致命错误。
每个进程使用单独的 SDK 日志目录。脚本汇总、应用日志、SDK 日志和 PCM 文件保存在
本机临时目录 `/private/tmp/agora-lifecycle-ubuntu.feNO3s/artifacts/agora-lifecycle.VnoImk/`。

调试过程中发现并修复两处问题：远端音轨替换时反复注销本地 PCM observer 导致重新发布后
PCM 回调不再恢复；退出时提前注销连接 observer 并释放资源，导致远端离线回调未在 20 秒内
到达。现在保留 PCM observer 直到显式清理，并等待 `onDisconnected` 后再释放连接。
此外，并发进程共用 SDK 日志路径时出现过 `spdlog fatal error`；最终回归为每个进程隔离
日志目录后未再出现，单独运行 sender 也未复现。

上述 ARM64 结果仅覆盖 8 声道 PCM sender/receiver 的对应轮次；PCM 文件增长不等于逐声道
内容正确，也没有覆盖其他收发路径、长稳或网络故障恢复。

## 2026-11-23 x86_64 对等回归

在独立 x86_64 主机的 Ubuntu 24.04.5 Docker 容器中，使用提交 `8a8536a`、Linux SDK
4.4.30.149 和真实 RTC 网络执行同一 `Demo/linux/tests/lifecycle_regression.sh`。发送端和接收端
仍分别使用 UID 71001、71002，`CYCLES=2`。

| 场景 | 结果 |
| --- | --- |
| 同一连接取消发布/重新发布，2 轮 | 通过；停止后文件不再增长，恢复后增长 |
| 同一连接取消订阅/重新订阅，2 轮 | 通过；停止后文件不再增长，恢复后增长 |
| 发送端退出后以相同 UID 重进 | 通过；收到 `onUserLeft`，重进后 PCM 恢复增长 |
| 接收端退出后以相同 UID 重进 | 通过；重新订阅并收到 PCM |
| 控制信号后立即发退出信号 | 双方按时退出且退出码为 0；不保证退出前控制信号一定被处理 |

脚本汇总见本机 `/private/tmp/agora-linux-x86_64-peer.JNI33I/lifecycle.log`，应用日志、SDK 日志和
PCM 文件位于 `/private/tmp/agora-linux-x86_64-peer.JNI33I/tmp/lifecycle/agora-lifecycle.4oCAGn/`；
远端原始产物保存在 `/home/ubuntu/agora-x86_64-results/`。本轮没有失败场景，全部退出码和日志
检查均通过。

该结果与 [Linux 全量回归记录](linux_full_regression_2026-11-19.md) 中的 x86_64 **48/48** 和
MKV **4/4** 结果对应。它仍只覆盖本次 8 声道 PCM 生命周期路径；逐声道频谱由全量回归单独验收，
不能外推到其他收发路径、长稳或网络故障恢复。

## 2026-11-23 扩展生命周期与恢复内容验收

使用 `lifecycle_content_regression.py` 将流程扩展到 10 轮，并在每个初始、重新发布、重新订阅及重进点
保留 2 秒媒体，逐点检查 startup/steady 频谱：

| 架构 | 流程结果 | 恢复内容结果 | 说明 |
| --- | --- | --- | --- |
| ARM64 | 10 轮脚本和进程退出均成功 | **16/23 通过** | 7 个失败均为实际频谱异常：6 个 startup、1 个 steady；没有采样不足 |
| x86_64 | 10 轮脚本和进程退出均成功 | **23/23 通过** | 所有恢复点的 startup/steady 均通过 |

ARM64 证据位于 `/private/tmp/agora-linux-pcm-lifecycle-arm64-r4.qb8CJQ/`，x86_64 证据位于
`/private/tmp/agora-linux-lifecycle-x86_64.Ug28zt/`。每次恢复的字节偏移和分析结果均保留在
`recovery-offsets.txt` 与 `extended-lifecycle-results.json` 中。

另对 ARM64 的 PCM、encoded、mixed、AV 四接收路径做同时恢复验证：取消发布/订阅时 PCM 停止、
其他接收端继续、重新发布后的内容均通过；重新订阅后的 steady 第 2 声道出现约 24.1% 的 300Hz 比例，
因此该矩阵整例仍失败，证据位于 `/private/tmp/agora-linux-lifecycle-arm64.LMLBfK/`。

这些结果继续遵守“文件增长不等于逐声道内容恢复”的口径，不能用流程成功或 x86_64 全绿抵消
ARM64 的具体异常。
