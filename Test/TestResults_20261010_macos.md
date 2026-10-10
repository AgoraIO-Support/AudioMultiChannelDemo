# macOS 接收端真频道验证记录（2026-10-10）

验证对象：`Demo/macOS` MultiChannelAudioReceiver（AgoraRtcEngine_macOS 4.5.2）
发送源：AWS 测试机 Docker 运行的 `sample_send_mkv_multiopus`（8 声道 OPUSMC payload 125 + H264 视频，频道 `mkvav1010a`，uid 777001）
对应需求：`Spec/native_sdk_audio_multichannel_spec.md` v0.2

## 结论：全部通过

| 验证项 | 方法 | 结果 |
| --- | --- | --- |
| 观众纯接收加入 | App ID + 频道名加入，uid=0 自动分配 | ✅ Connected，本端 uid 3927546670，不发布音视频 |
| host 列表 | 观察远端列表 | ✅ 仅收录 uid 777001（发布音频的 host）；观众不入列 |
| 接收码率旁证声道数 | 界面 Recv bitrate | ✅ ~262 kbps，落在 8 声道 OPUSMC 250~280 kbps 区间 |
| 逐声道音量私参下发 | 开关/滑杆触发 `adjust_decoded_volume` | ✅ 全部下发返回 0（日志无 setParams ret 报错） |
| 私参即时生效（客观） | ch1 置 0 → 6s → 恢复 100，对 per-user dump 做逐秒 RMS | ✅ dump 起点 14:43:15；ch1 RMS 在 +33s~-36s（=14:43:48~51）为数字静音，与操作时刻 14:43:47.767→53.965 秒级吻合；恢复后约 1s 内回到 ~-13 dB |
| frame dump 声道数/内容 | ffprobe + astats 分析 `audio_dump_playback_before_mixed_0_*_777001.wav` | ✅ 48 kHz / **8 声道** / 16bit；八声道 RMS -13.6~-15.5 dB 各不相同，无串声道、无丢声道 |
| 混音输出恒 2 声道 | ffprobe `audio_dump_playback_mixer_0.wav` | ✅ 48 kHz / 2 声道 |
| dump 位置/命名/封顶 | 观察 `~/Library/Logs/` | ✅ 位置 `~/Library/Logs/`；命名 `audio_dump_playback_before_mixed_0_<yyyy_MM_dd_HHmmss>_<uid>.wav`；约 120 MB 封顶停止，Leave 重进按新时间戳另建 |

证据截图：`Test/macos_receiver_8ch_verified_20261010.png`

## 附带发现

- `che.audio.frame_dump` 重开（off→on）不会重建已封顶的 per-user dump 文件，需 Leave/Join 另起新文件。
- dump 反映的是 `adjust_decoded_volume` 作用后的解码数据（置 0 的声道在 dump 中为数字静音）——dump 可用于核对"本端实际听到的"逐声道状态。
- 场景 1.2（普通 stereo/mono 流兼容）本次未覆盖（发送源为 8 声道流）。

## 测试操作备忘（UI 自动化）

- SwiftUI 无障碍树中 `entire contents` 间歇性失效，稳定路径为 `window 1 → group 1 → group 1`（join 区，含 4 个 text field、Join 按钮、dump 勾选框）与 `group 2`（声道控制区，8 个声道开关 checkbox）。
- `set AXValue` 只能改显示值、不进 `@State` 绑定（Join 按钮 disabled 状态可佐证）；必须用真实鼠标事件（cliclick）聚焦后粘贴/按键。
- 系统输入法若为中文，按键会触发候选窗；用 pbcopy + Cmd+V 粘贴可完全绕开 IME。
- 每个 Bash 调用都是独立进程，activate + 点击 + 输入必须放在同一个 osascript 进程内完成，避免焦点被 VS Code 抢回。
