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

## 追加：远端视频显示验证（2026-10-10 15:00，spec v0.3 需求点6）

app 增加远端视频显示后重新构建，重新加入 `mkvav1010a`（本端 uid 3892160999）：

| 验证项 | 方法 | 结果 |
| --- | --- | --- |
| host 视频状态检测 | `remoteVideoStateChangedOfUid` | ✅ 日志 `Remote host is publishing video: uid=777001`（加入后约 1s） |
| 视频渲染绑定 | `setupRemoteVideo` | ✅ 返回 0（日志无 ret 后缀报错） |
| 视频实时出图 | 间隔 5s 两次窗口截图对比 | ✅ 画面内容不同（秀场特写 → 宴会厅全景），为实时播放而非首帧定格；等比缩放居中显示 |
| 音频能力不受影响 | 8 行声道控制/码率显示 | ✅ 全部 100% 启用，Recv bitrate ~259 kbps 正常 |

证据截图：`Test/macos_receiver_video_20261010.png`

## 追加：UI 布局改版验证（2026-10-10 15:13，spec v0.4）

| 验证项 | 结果 |
| --- | --- |
| 视频区按需内嵌 | ✅ 视频出现在「Remote Multi-Channel Control」区右侧，无独立常驻黑框 |
| 视频宽度贴合流宽高比 | ✅ 1920x1080 流按 240 高度等比显示，无大黑边 |
| 声道滑杆宽度收敛 | ✅ 滑杆限宽 260pt，不再拉伸满行 |
| 码率实时走势图 | ✅ 接收码率数值（272 kbps）下方 sparkline 实时增长（60 样本窗口） |

证据截图：`Test/macos_receiver_layout_v2_20261010.png`

注：本次加入后视频发布状态回调较晚（join 后 ~38s 才报 publishing），视频区出现随之延迟，属 SDK 回调时序；功能本身正常。

## 追加：UI 细化验证（2026-10-10 15:22，spec v0.5）

| 验证项 | 结果 |
| --- | --- |
| UID 显示格式 | ✅ 下拉列表显示 "UID 777001"，不再有千分位 "777,001" |
| 码率图 Y 轴 | ✅ 右侧标注最大值（272）与 0，标题行 "Recv bitrate: 265 kbps · last ~2 min, kbps" |
| 功能分组 | ✅ 选择远端 → 监控（码率图+视频）→ 逐声道播放控制，组间 Divider + 小组标题 |

证据截图：`Test/macos_receiver_layout_v3_20261010.png`

## 追加：「控制 | 监控」双栏信息架构（2026-10-10 15:35，spec v0.6）

| 验证项 | 结果 |
| --- | --- |
| 双栏布局 | ✅ 左栏控制（430pt 固定：选择远端 + 声道数 + 逐声道控制），右栏监控（弹性宽：Monitor — UID 777001 标题联动） |
| 视频等比撑满栏宽 | ✅ 16:9 流在右栏大尺寸居中渲染，无大黑边 |
| 码率走势图通栏 + Y 轴 | ✅ 视频下方通栏显示，Y 轴 max/0 标注，可见真实码率波动（264~412 kbps） |
| 纯音频状态 | ✅ 视频状态回调前监控栏正确显示 "Audio only" + 码率图，无视频占位（见 15:35 首帧截图） |

证据截图：`Test/macos_receiver_layout_v4_20261010.png`

## 追加：视频接收码率走势图（2026-10-10 15:42，spec v0.7）

| 验证项 | 结果 |
| --- | --- |
| 视频码率图 | ✅ "Video recv: 3983 kbps" 蓝色 sparkline，Y 轴 max/0，紧随视频画面下方，仅在 host 发布视频时出现 |
| 音频码率图 | ✅ 更名 "Audio recv: 287 kbps"（绿色）消歧，与视频图并列 |
| 双图实时性 | ✅ 两图均可见真实波动（视频 3983~5303 kbps，音频 ~260~357 kbps） |

证据截图：`Test/macos_receiver_layout_v5_20261010.png`

## 追加：「声道数」选择器归位（2026-10-10 15:48，spec v0.8）

| 验证项 | 结果 |
| --- | --- |
| 声道数选择器与声道行同组 | ✅ "Channels 8" 移至 "Channel playback" 标题行右侧，紧贴其控制的 8 行声道控制，关联关系直观 |

证据截图：`Test/macos_receiver_layout_v6_20261010.png`

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
