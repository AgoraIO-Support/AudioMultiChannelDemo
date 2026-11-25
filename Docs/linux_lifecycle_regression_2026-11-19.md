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


## 2026-11-24 receiver teardown 崩溃专项

- 4.4.30.152 ARM64 的 N/M 回归中，`7to4` receiver 在连接超时、`onDisconnected` 后退出 -11（SIGSEGV），尚未启动 sender；`7to5` 为连接超时退出 1。该现象与旧 x86_64 `8to6` 在断开后的崩溃一样，不能仅凭声道组合归因于映射算法。
- 新 SDK x86_64 的 20 轮 `8to6`、ARM64 的 30 轮 `7to4` 和 20 轮强制连接超时均未出现 ASan/UBSan 内存错误；其中 ARM64 `7to4` 为 29 轮退出 0、1 轮连接超时退出 1。
- 从 Git 历史恢复匹配的 4.4.30.149 后，旧版 x86_64 再跑 50 轮 `8to6` ASan/UBSan，全部退出 0，无内存错误报告。
- 初次 UBSan vptr 在未插桩 SDK 边界误报，后续只关闭 vptr 检查，地址越界和 use-after-free 检查仍开启。
- 结论：历史与本次 SIGSEGV 均保持开放。首次复现阶段没有有效崩溃堆栈，不修改应用 teardown；下一轮需可复用 core 采集或 SDK 团队带符号二进制定位。后续已取得 ARM64 core，见下节。产物分别位于 `/private/tmp/agora-sdk152-p0-20261124/arm64-asan/` 和远端 `/home/ubuntu/agora-x86_64-sdk149-asan/asan-runs/`。


## 2026-11-24 实际 coredump 补充

Podman VM 的 `systemd-coredump` 已捕获 ARM64 `7to4` 的 SIGSEGV（PID 28637、线程 987）：

```text
#0 libagora_rtc_sdk.so + 0xcbc81c
#1 libagora_rtc_sdk.so + 0x12e2ccc
#2 libagora_rtc_sdk.so + 0x4aedd4
#3 libagora_rtc_sdk.so + 0x12e3164
#4 libagora_rtc_sdk.so + 0x3df180
#5 libaosl.so + 0x23a4c
#6 libaosl.so + 0x23b94
#7 libaosl.so + 0x23d98
#8 libaosl.so + 0x23eb4
#9 libaosl.so + 0x26210
```

随后进入 libc。完整 `coredumpctl info`、原始 `.zst` core、寄存器和销毁线程回溯已集中归档；该 core 是 systemd 生成的 ELF core，不是 SDK 原生 `.dmp`，原始二进制证据未纳入本仓库。

### 符号对照后的函数级边界

- core 证明 `x0 = 0`，故障指令为 `ldr x0, [x0, #16]`，访问地址 `0x10` 不可读，**直接故障是空指针解引用**。此前仅凭日志推测的 use-after-free 不再是已证实结论。
- 149 ARM64 符号中的 `AudioTransportWrapper::NeedMorePlayData()` 与 152 ARM64 调用者在长度、参数处理和调用序列上逐段吻合。该函数从 `this + 1176` 读取 `AudioTransportImpl*`，无判空调用 `GetDownlinkProcessingAverageTime()`；后者读取 `this + 16` 的 `DownlinkAudioProcessingProxy*`。core 中 `x19 = 0xffffb05f2510`，其 `+1176` 成员为零。下一层与 `AudioDeviceBuffer::RequestPlayoutData()` 相符，不是 `LocalUserImpl::onPollingTimer()`。
- `AudioTransportWrapper::UnregisterPlaybackAudioCallback()` 在 149 ARM64 中会将该 transport 指针清零。x86_64 152 符号也显示同型行为：注销在 `0xaf1509` 清零，取流在 `0xaf32ae` 无判空读取，getter 在 `0x13e8470` 继续解引用。应优先检查回调注销、设备取流和销毁之间缺少的同步。
- 销毁线程的回溯中出现同一个 `0xffffb05f2510` 并进入 `operator delete`，其清理顺序与 `AudioTransportWrapper::~AudioTransportWrapper()` 相符，支持取流与销毁并发。修复既要考虑判空，也要处理 wrapper 的生命周期，不能仅修改应用 teardown。
- 当前最窄的故障路径是：**注销回调清零 transport 指针后，设备回调继续进入 `NeedMorePlayData()`，最终空调用 `GetDownlinkProcessingAverageTime()`。** 真实 core 与后续研发反馈共同确认了清理与在途任务的顺序；具体修复和引入版本仍需 SDK 团队结合源码及提交历史确认。

两份日志均经过 `SetPlayout(0)` 和 `AgoraService::release` → `LocalUserImpl destroying`，与上述方向吻合。但历史 x86_64 149 没有 core 和匹配符号，不能声称其堆栈已还原或已证明与本次相同；x86_64 152 的同型代码也不等于历史问题已经复现。N/M 映射仍是不支持且未安全拒绝的独立问题，不是本次空指针的直接原因。

修复后必须重跑连接超时和正常断开场景，不能用后续 ASan 未复现关闭问题。

## 2026-11-24 x86_64 152 自然复现与测试入口

新增 `Demo/linux/tests/audio_teardown_race.py` 和 `audio_teardown_race` C++ target。真实复现使用原 receiver，在 `Start receiving` 后发送正常 SIGINT，执行原 `disconnect → release`，不使用 debugger、内存注入或伪造信号，不要求 sender 或 N/M 错配。

- x86_64 使用 SDK 4.4.30.152、build 1392522。第 **84** 次真实 join/teardown 出现 SIGSEGV，频道 `audio-race-1795508775062900818-b21-w2`，SID `2D47781173174F3EA6AFE15763A4A3CE`，PID 168838、故障线程 168904，时间为 `2026-11-24 08:26:21.958 UTC`。
- 同时取得 SDK 原生 `7C66F755D0144CFFE67CF7B796AB9720.dmp` 和 Apport 系统 ELF core。core 的远端/本地 SHA-256 均为 `283d5a5621f13ebd369387e1ada1216686945fca3f1e2f0e002dd2ba015fd2d7`。
- 匹配符号直接还原为 `GetDownlinkProcessingAverageTime()`（`audio_transport_impl.cc:300`）→ `NeedMorePlayData()`（`audio_transport_wrapper.cpp:1103`）→ `RequestPlayoutData()`（`audio_device_buffer.cc:583`）→ `getPlaybackAudioDataTimer()`（`audio_transport_wrapper.cpp:737`）。`rdi = 0`，wrapper 的 `+0x490` transport 成员也为零，直接证实前述空指针路径。
- 20ms 完整批次另三次崩溃也取得独立原生 dmp，其中 PID 175787 的符号堆栈与上述源码行完全相同。
- 全部 x86_64 自然 RTC 测试共执行 400 次，记录 9 次 SIGSEGV；20ms 为 **5/104**，5ms 为 **2/136**，10ms 为 **0/40**。这些是当前环境的实际批次，**不是 100% 复现，也不是固定概率**。5ms 的完整 80 轮未复现，不能用小样本高比例外推。
- ARM64 无媒体 1000 次 connection 创建/销毁未复现；真实 RTC 200 次未出现 SIGSEGV，但有 38 次连接失败。连接失败不计为崩溃，不能用这些结果关闭此前的 ARM64 core 问题。

运行器退出 `10` 表示检测到自然 SIGSEGV，连接失败、超时和版本不匹配分别统计。早期 `--keep-going` 提前停止已修正，10 个离线运行器单元测试通过；实际执行次数均保存在对应 `summary.json`。复现命令见 `Demo/linux/README.md`。

### 研发反馈与真实入会超时验证

研发反馈进一步确认了清理顺序：先置空 `audio_transport_`，但内部自动拉音频任务尚未退出，随后调用 `GetDownlinkProcessingAverageTime()`。这与 core 的函数级证据一致。研发认为该问题在分支长期存在且主要影响退出阶段；149/152 的代码形态支持这一排查方向，但具体引入版本仍需提交历史确认。退出阶段的影响不能等同于无影响：`-11`、core 生成、退出状态破坏和自动化测试不稳定仍需修复。

新增 `--expect-join-timeout` 要求实际出现 `Wait for connection timeout or failure!`，意外成功入会将判场景无效。本地 Podman `--network none` 与 AWS 用户态网络命名空间各执行 40 次，全部实际发生入会超时：

- ARM64 build 1397089：**3/40 SIGSEGV**。代表频道 `audio-race-1795512233351085696-b7-w4`，PID 499；`x0 = 0`，故障 PC 为 `+0xcbc81c`，实际执行 `ldr x0, [x0, #0x10]`，下一帧为 `+0x4aedd4`。
- x86_64 build 1392522：**1/40 SIGSEGV**。频道 `audio-race-1795512238619761392-b6-w1`，PID 178563；`rdi = 0`，故障 RIP 为 `+0x13e8470`，匹配符号仍经过 `audio_transport_impl.cc:300`、`audio_transport_wrapper.cpp:1103`、`audio_device_buffer.cc:583`、`audio_transport_wrapper.cpp:737`。

这些结果支持超时是高风险窗口，但不是必要条件，也不是 100% 触发条件。网络隔离是受控环境，不与公网自然超时混写；成功入会后立即退出同样已有自然 core。`completed_iterations=0` 是因为全部未成功入会，普通连接失败和意外成功入会均未混入。各批次明细由对应 `summary.json` 记录。

### 集中证据与结论边界

- 两场独立 x86_64 崩溃的原生 dmp、SDK/应用日志、系统 core、批次汇总、可执行文件和符号化输出均已按 PID 归档；第二份系统 core 的远端与本地 SHA-256 均为 `b3c01c88beac1c981f76801c28880f680f683beddf17e69f716541bb3517553a`。原始二进制证据未纳入本仓库。
- 文档仍以真实复现和证据闭环为边界，不代表 SDK 修复完成。
