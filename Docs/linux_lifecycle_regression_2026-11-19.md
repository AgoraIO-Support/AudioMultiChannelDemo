# Linux 生命周期回归：2026-11-19

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

此结果仅覆盖 ARM64、8 声道 PCM sender/receiver 的本次测试；PCM 文件增长不等于逐声道
内容正确，也没有覆盖 x86_64、其他收发路径、长稳或网络故障恢复。
