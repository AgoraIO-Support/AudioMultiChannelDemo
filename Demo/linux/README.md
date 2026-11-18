# Agora Linux SDK 音频多声道（multiopus）Demo

本工程是 Agora Linux SDK（v4.4.30.149，x86_64 / aarch64）音频多声道收发的独立 C++ demo，
依据需求规格 `Docs/audio_multichannel_spec.md` 编写，覆盖发送 4 路径与接收 4 路径。
多声道编码基于 Opus 多流（OPUSMC，payload type 123/124/125），**仅支持 48 kHz 采样率**。

[English Documentation](README.en.md)

## 功能列表

| Sample | 方向 | 覆盖需求点 | 说明 |
| --- | --- | --- | --- |
| `sample_send_pcm_multiopus` | 发送 | 需求点1（PCM 发送）、需求点4（私参与协商） | raw PCM 多声道发送，10ms/帧，声道数可配 3~8 |
| `sample_send_wav_multiopus` | 发送 | 需求点2（WAV 发送）、需求点4 | 多声道 WAV 发送：RIFF 解析，16bit 直发、24bit 转 16bit，非法位深、声道数与非 48kHz 在连接前报错 |
| `sample_send_opus8` | 发送 | 需求点3（预编码 Opus 发送） | 8 声道 Opus 裸流（640 字节/20ms 帧），`AUDIO_CODEC_OPUSMC` + `MIX_DISABLED` |
| `sample_send_mkv_multiopus` | 发送 | 需求点1/4/10（MKV 4xstereo 素材） | ffmpeg 解 MKV：4 条 AC-3 stereo 轨重采样重排为 8 声道 48kHz 发送 + mpeg4 视频转 I420 按 PTS 发送（仅该 sample 依赖 ffmpeg 库） |
| `sample_receive_pcm_multiopus` | 接收 | 需求点5（解码 PCM 回调） | `onPlaybackAudioFrameBeforeMixing` 按用户回调多声道 PCM 并落盘（100MB 切换文件） |
| `sample_receive_encoded_audio` | 接收 | 需求点6（编码帧回调） | `audioRecvEncodedFrame=true` + `onEncodedAudioFrameReceived`，不解码不播放 |
| `sample_receive_mixed_audio` | 接收 | 需求点7（混音接收） | `onPlaybackAudioFrame` 混音输出（SDK 混音恒为 2 声道，奇/偶声道分路；demo `--numOfChannels` 支持 1/2，默认 2）并落盘（100MB 切换文件） |
| `sample_receive_av_multiopus` | 接收 | 需求点5 + 需求点8（AV 同收） | 多声道 PCM 落盘 + `IVideoFrameObserver2` 回调 I420 帧落盘 YUV，固定间隔打日志 |

> 需求点10（4×stereo → 8 声道布局映射）为集成侧约定，见下文「4×stereo 布局映射」。

## 目录结构

```
Demo/linux/
├── CMakeLists.txt            # 顶层，通过 AGORA_SDK_DIR 指定 SDK 路径
├── build.sh                  # 便捷脚本：./build.sh [x86_64|aarch64] [clean]
├── common/                   # 拷贝自 LinuxSDKFolder/X86_64 example/common（与 SDK v4.4.30.149 匹配）
├── multiopus/                # 发送端 4 个 sample
├── receive/                  # 接收端 4 个 sample
├── public/                   # 素材：8ch_2.opus、8_Channel_ID_signed_16_bit_pcm.wav（及同名 .raw.pcm）、
│                             # 4/6_Channel_ID_48k.wav（48kHz 重采样版）、8_Channel_ID_24bit.wav（24bit 验证素材）、
│                             # odd_3ch/odd_5ch/odd_7ch.wav（3/5/7 奇数声道素材，ffmpeg channelmap 自 8ch 素材截取）、
│                             # VictoriaMulti.mkv（1 路 mpeg4 1080p + 4 条 AC-3 44.1kHz stereo 轨 = 8 声道，已入库 Git LFS 约 133MB）
└── out/                      # 构建产物（二进制 + public 素材副本）
```

> 注：`common/` 与 SDK 自带 example 保持逐字一致，便于对照与后续同步。静态分析发现的以下
> **上游原始问题仅记录、不修改**：
>
> 1. `sample_local_user_observer.h/.cpp` 使用 `std::string` 但未自包含 `<string>`（依赖传递
>    include；在 include 图不同的 libc++ 环境下会编译失败，Linux GCC/当前 SDK 组合不受影响）；
> 2. `sample_local_user_observer.cpp` `calculate_layout` 中 `y_offset_ = y_offset_;` 自赋值
>   （无效语句，混流布局遗留代码，音频 demo 不会走到）；
> 3. `sample_connection_observer.cpp` `onTransferStatistics` 以 `%lu` 打印 `uint64_t`
>   （Linux x86_64/aarch64 上 `unsigned long` 恰为 64 位可正常工作，其他平台属未定义行为）；
> 4. `file_parser/helper_h264_parser.cpp`、`helper_h265_parser.cpp` 构造函数未初始化
>   `data_size_` 成员（file_parser 未编入本工程 sample，仅随 `common/` 一并拷贝）。

## 构建方法

### SDK 包准备：构建时自动解压

`LinuxSDKFolder/X86_64/` 与 `LinuxSDKFolder/Aarch64/` 只保留原始 SDK 压缩包
（`Agora-RTC-<arch>-linux-gnu-*.tgz`），解压后的 `agora_rtc_sdk/` 目录不纳入 Git。
`build.sh` 与 Dockerfile 会按目标架构自动定位并解压对应 tgz，再检查
`agora_rtc_sdk/agora_sdk/libagora_rtc_sdk.so` 和头文件是否存在，无需手工预处理。
解压目录同时被 `.gitignore` 与 `.dockerignore` 排除，避免重复入库或增大构建上下文。

只有手动执行 CMake 时才需要先自行解压：

```bash
# x86_64 为例；aarch64 同理，用 LinuxSDKFolder/Aarch64/ 下的压缩包
cd LinuxSDKFolder/X86_64
tar -xzf Agora-RTC-x86_64-linux-gnu-v4.4.30.149-20260127_144108-1004142.tgz
```

### 便捷脚本（推荐）

```bash
cd Demo/linux
./build.sh x86_64     # 或 ./build.sh aarch64；不传参数时按 uname -m 自动选择
./build.sh clean      # 清理 build/ 与 out/
```

脚本会自动选择 SDK 目录、按需解压 tgz，并校验 `agora_sdk/libagora_rtc_sdk.so` 是否存在。

可单独验证 MKV 临时文件的权限、符号链接防护与清理：

```bash
c++ -std=c++11 -Wall -Wextra -Werror tests/private_pcm_files_test.cpp -o build/private_pcm_files_test
./build/private_pcm_files_test
```

构建完成并准备好 `out/public/` 后，可用以下命令回归四个发送端的连接失败清理：

```bash
export LD_LIBRARY_PATH="$(pwd)/../../LinuxSDKFolder/Aarch64/agora_rtc_sdk/agora_sdk${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
bash tests/connection_failure_regression.sh
```

按实际架构将 `Aarch64` 改为 `X86_64`。测试使用无效 App ID，检查连接等待超时、
退出码和日志；日志目录由脚本打印，不需要真实 App ID 或 token。

### 手动 CMake

```bash
cd Demo/linux
mkdir build && cd build
cmake .. -DAGORA_SDK_DIR=../../LinuxSDKFolder/X86_64/agora_rtc_sdk
make -j8
```

`AGORA_SDK_DIR` 支持相对/绝对路径。链接方式与 SDK 自带 example 一致
（链接 `agora_rtc_sdk` + `pthread/dl/rt/m`，`libagora-fdkaac.so` 与 `libaosl.so`
通过 rpath 在运行时从 SDK 目录加载）。

## Docker 部署

在**仓库根目录**（`AudioMultiChannelDemo/`）执行（构建上下文需要同时包含
`Demo/linux` 与 `LinuxSDKFolder`）：

```bash
# 构建镜像（按构建机架构自动选择 X86_64/Aarch64 SDK）
docker build -f Demo/linux/Dockerfile -t agora-multichannel-linux .

# 交叉构建 ARM64 镜像（需 buildx + QEMU，或在 ARM 机器上原生构建）
docker buildx build --platform linux/arm64 \
    -f Demo/linux/Dockerfile -t agora-multichannel-linux:arm64 .
```

镜像内：二进制与素材在 `/app`（含 `public/`），SDK 动态库在 `/opt/agora/lib`
（已通过 `LD_LIBRARY_PATH` 配置）。镜像默认以非 root 用户（UID 10001）运行，
`/app` 可用于写入接收文件和 SDK 日志；若挂载输出目录，需确保该用户有写入权限。
先使用默认容器网络，并确认环境允许 RTC UDP 流量：

```bash
docker run --rm -it agora-multichannel-linux \
    ./sample_send_wav_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

所有 sample 的运行参数与下方「运行示例」一致，把 `docker run ... agora-multichannel-linux`
后面的部分换成对应命令即可。仅在实际网络环境需要时才使用 `--network host`。

## 运行示例

以下命令在 `out/` 目录下执行（token 与 appId 二选一传入 token 字段，与参考 sample 风格一致：
`--token` 实际承载 appId/token，无证书场景传 appId 即可）。

### 1. 发送 8 声道 WAV（16bit）

```bash
./sample_send_wav_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.wav" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

> 其他 WAV 验证素材：24bit 转换（场景2.2）用 `public/8_Channel_ID_24bit.wav`（命令同上）；
> 4/6 声道用 `public/4_Channel_ID_48k.wav` / `public/6_Channel_ID_48k.wav`（已由 44.1 kHz
> 重采样到 48 kHz），对应 `--numOfChannels 4 --custom_channel_num 4 --custom_payload_type 123`
> 与 `--numOfChannels 6 --custom_channel_num 6 --custom_payload_type 124`。
> 3/5/7 奇数声道用 `public/odd_3ch.wav` / `odd_5ch.wav` / `odd_7ch.wav`（ffmpeg channelmap 自
> 8ch 素材截取），如 5 声道：`--audioFile public/odd_5ch.wav --numOfChannels 5
> --custom_channel_num 5 --custom_payload_type 124 --sampleRate 48000`（互通已实测，见
> `Docs/audio_multichannel_test_summary.md`）。

### 2. 发送 raw PCM 多声道（3~8 声道）

```bash
# 默认素材 public/8_Channel_ID_signed_16_bit_pcm.raw.pcm（48kHz/8ch/16bit，由同名 WAV 提取）
./sample_send_pcm_multiopus --channelId <name> --token <token> \
    --audioFile "public/8_Channel_ID_signed_16_bit_pcm.raw.pcm" \
    --numOfChannels 8 --custom_channel_num 8 --custom_payload_type 125 --sampleRate 48000
```

### 3. 发送预编码 8 声道 Opus 裸流

```bash
./sample_send_opus8 --channelId <name> --token <token> --audioFile "public/8ch_2.opus"
```

### 4. 接收多声道 PCM（按用户回调 + 落盘）

```bash
./sample_receive_pcm_multiopus --channelId <name> --token <token> \
    --numOfChannels 8 --sampleRate 48000 --audioFile received_8ch.pcm
```

### 5. 接收编码帧（OPUSMC 裸帧落盘）

```bash
./sample_receive_encoded_audio --channelId <name> --token <token> --audioFile received.opus
```

### 6. 接收混音输出（SDK 混音恒 2 声道；demo 可选 mono）

```bash
./sample_receive_mixed_audio --channelId <name> --token <token> --audioFile received_mixed.pcm
```

### 7. 发送 MKV（4xstereo 音轨 + 视频）

素材 `public/VictoriaMulti.mkv`（1 路 mpeg4 1920x1080 + 4 条 AC-3 44.1kHz stereo 轨，
共承载 8 声道）。启动时 ffmpeg 一次性解码 4 条音轨、重采样到 48kHz 并重排为交错 8 声道
写入每次运行独立创建的 `/tmp/agora-multiopus-XXXXXX/` 私有目录（权限 0700；
PCM 文件权限最多为 0600），随后音频 10ms/帧、
视频按 PTS 节奏各自循环发送。声道数 8 / payload 125 / 48000 写死：

每次启动都会重新转换；正常退出或初始化失败时清理临时文件。收到默认终止进程的信号
（如 SIGTERM、SIGKILL）或转换期间被中断时可能留下该私有目录，需由运行用户或管理员检查后清理。

```bash
# 该 sample 依赖 ffmpeg（libavformat/libavcodec/libavutil/libswresample/libswscale），
# 本机构建需先装对应 -dev 包；Docker 镜像已内置
./sample_send_mkv_multiopus --channelId <name> --token <token> --userId 8899 \
    --videoFile public/VictoriaMulti.mkv
```

### 8. 接收多声道音频 + 视频（AV 同收落盘）

```bash
./sample_receive_av_multiopus --channelId <name> --token <token> --userId 8898 \
    --remoteUserId 8899 --audioFile received_av_8ch.pcm --videoFile received_video.yuv
```

- 音频和视频都按远端 UID 独立落盘，文件名分别为 `<audioFile>.uid-<UID>` 和
  `<videoFile>.uid-<UID>`，格式为 48 kHz / N 声道 / 16bit 交错 PCM 与 I420 裸帧序列；
  1080p 视频每帧约 3MB，100MB 自动切换新文件，可用
  `ffplay -f rawvideo -pix_fmt yuv420p -s 1920x1080 received_video.yuv.uid-8899` 回放核对；
- `--remoteUserId` 缺省时订阅频道内全部远端用户的音频与视频。

### 强制不回退（codec_unfallback）

所有发送 sample 支持 `--codecUnfallback` 开关（注意必须带值 `1`，无值用法会被参数解析器拒绝）：

```bash
./sample_send_pcm_multiopus ... --codecUnfallback 1
```

## payload type 与声道数映射

| payload type | 声道数 | 采样率 | channel_mapping | coupled_streams | num_streams |
| --- | --- | --- | --- | --- | --- |
| 123 | 3 | 48000 | 0,1,2 | 1 | 2 |
| 123 | 4 | 48000 | 0,1,2,3 | 2 | 2 |
| 124 | 5 | 48000 | 0,1,2,3,4 | 2 | 3 |
| 124 | 6 | 48000 | 0,1,2,3,4,5 | 3 | 3 |
| 125 | 7 | 48000 | 0,1,2,3,4,5,6 | 3 | 4 |
| 125 | 8 | 48000 | 0,1,2,3,4,5,6,7 | 4 | 4 |

- 123 默认 4 声道、124 默认 6 声道、125 默认 8 声道；**3/5/7 声道必须显式设置
  `che.audio.custom_channel_num`**（demo 中 `--custom_channel_num`，不设置时跟随 `--numOfChannels`）。
- `--custom_payload_type` 不设置时 demo 按声道数自动推导（≤4→123，≤6→124，否则→125）。
- 配置生效判据：日志出现
  `MultistreamEncoder channel num:N rate:48000 coupled:M channel_mapping:... bitrate:...`。

## 私参设置时机（重要）

| 私参 | 设置位置 | 时机 |
| --- | --- | --- |
| `che.audio.custom_payload_type` / `che.audio.custom_channel_num` / `che.audio.custom_sample_rate` | `connection->getAgoraParameter()` | `createRtcConnection` 之后、`connect()` 之前；connect 之后设置不生效 |
| `che.audio.codec_unfallback`（JSON 数组，如 `[123,124,125]`） | `service->getAgoraParameter()` | **`createRtcConnection()` 之前**；设在 connection 级不生效 |

发送端必须设置 payload type 系列参数；接收端在 123/124/125 协商机制下无需设置。

## 接收端验证方法

### PCM 落盘文件（sample_receive_pcm_multiopus）

输出为 48 kHz / N 声道 / 16bit 小端交错裸 PCM，可用 ffplay 直接播放验证。接收全部远端用户时，
每个用户独立写入 `<audioFile>.uid-<UID>`，分片文件追加 `.2`、`.3`；UID 中的路径字符会被替换为 `_`。
指定 `--remoteUserId` 时也使用相同命名规则，避免不同运行方式产生混淆：

```bash
ffplay -f s16le -ar 48000 -ch_layout 7.1 received_8ch.pcm.uid-8899   # 8 声道示例
# 或用 Audacity：File > Import > Raw Data，选 Signed 16-bit PCM / Little-endian / N channels / 48000 Hz
```

用 Channel ID 素材（`8_Channel_ID_signed_16_bit_pcm.wav`，各声道内容可区分）发送时，
逐声道核对落盘文件可确认无串声道、无声道丢失。

### Web demo 端到端验证

配合 Web 验证 demo（本仓库 `Demo/web/`，`agora-rtc-sdk-ng@4.24.10` 主线版，Chrome 96+）：

1. 使用 `AgoraRTC.createClient({ mode: "rtc", codec: "vp8", multiopus: true })` 创建 client；
2. 订阅 Linux 端发布的多声道流；
3. `getVolumeLevels()` 返回数组长度等于实际解码声道数，底层 codec 显示为 `multiopus`；
4. `selectChannels(channels, "surround"|"stereo"|"mono")` 可选择任意声道组合播放。

注意：Web 端仅支持偶数声道解码；3/5/7 声道流在 Web 端不报错但会被误解码为 N+1 声道（末声道复制出幻影、界面误判多声道 ✓），结果不可信，奇数声道请用 Native/Linux 端验证（详见 `Docs/audio_multichannel_test_summary.md` §5）；Safari/Firefox 行为未定义。

### 编码帧落盘（sample_receive_encoded_audio）

输出为按 UID 分开的 OPUSMC 裸帧流（640 字节/帧），可用于转发或自行解码。文件名为
`<audioFile>.uid-<UID>`，回调 `info` 仅携带
`codec` 与 `sendTs`（SDK v4.4.30.149 头文件 `AgoraMediaBase.h` 中
`AudioEncodedFrameInfo` 无声道数/采样率字段）。

### 混音落盘（sample_receive_mixed_audio）

该回调提供 SDK 的全局混音结果，没有远端 UID 维度，因此仍写入一个聚合文件。

输出为混音后的 48 kHz / 16bit 小端交错裸 PCM，默认 2 声道（多声道流混音恒为
2 声道，奇数声道混一路、偶数声道混一路；`--numOfChannels 1` 时输出 mono）：

```bash
ffplay -f s16le -ar 48000 -ch_layout stereo received_mixed.pcm
```

## 4×stereo 布局映射（需求点10）

源端 8 声道以 4 条双声道（stereo）音轨承载，按 **track 块序** 排列
（`[T1L,T1R]×N [T2L,T2R]×N [T3L,T3R]×N [T4L,T4R]×N`），**不能直接** 喂给
`sendAudioPcmData`。需先在业务侧重排为 Agora 8 声道 **逐样本交错** 布局：

```
声道顺序：CH1=T1L, CH2=T1R, CH3=T2L, CH4=T2R, CH5=T3L, CH6=T3R, CH7=T4L, CH8=T4R
缓冲区按 [T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R] 每样本重复排列
```

- 声道对 (0,1)(2,3)(4,5)(6,7) 分别对应轨 1~4，与 8 声道 OPUSMC 4 个 coupled stream 的配对一致。
- 源端单次写入 15360 字节 = 8 声道 × 16bit × 960 样本 = 20ms @ 48kHz，送入 SDK 前需拆分为 10ms 帧。
- 源为 44.1 kHz（AC-3 解码后 PCM）时必须先重采样到 48 kHz；重采样与重排顺序无关，但都须在 `sendAudioPcmData` 之前完成。
- 该映射为 2025-04 联调双方确认结论（此前接收端杂音的根因即把逐样本交错数据按平面块序读取）。

## 注意事项

- **仅支持 48 kHz**：多声道编解码（OPUSMC）仅支持 48000 Hz；44.1 kHz 素材（如
  `4_Channel_ID.wav`、`6_Channel_ID.wav`）须先重采样；WAV 发送 sample 会在连接前拒绝此类文件，
  不会自动重采样。
- **声道数一致性**：发送端 PCM 帧声道数、`--numOfChannels`、`custom_channel_num`、
  WAV 头声道数必须与接收端 `setPlaybackAudioFrameBeforeMixingParameters` 的声道数一致
  （>4 声道为硬约束），否则无法还原完整多声道。
- **codec_unfallback 设置位置**：必须在 `service->getAgoraParameter()` 上、
  `createRtcConnection()` 之前；设在 connection 级不生效（CSE 实测）。
- **对端不支持多声道时**：Linux 发送端会回退为普通 Opus（OPUSFB），多声道失效；
  需要全频道强制多声道时开启 `--codecUnfallback`，并保证频道内参与者均支持多声道。
- **预编码 Opus 输入格式**：`sample_send_opus8` 仅接受 640 字节/帧的 8 声道 48kHz Opus
  裸帧流（非 ogg 封装），帧格式须与 `EncodedAudioFrameInfo` 严格一致。
- **素材位置**：构建后 `build.sh` 会把 `public/` 拷贝到 `out/` 下，sample 默认相对路径读取。
- **MKV 发送 sample 依赖 ffmpeg**：`sample_send_mkv_multiopus` 是本工程唯一依赖
  ffmpeg（libavformat/libavcodec/libavutil/libswresample/libswscale）的 target，本机构建需
  `pkg-config` 与对应 `-dev` 包；缺少时 CMake 会打印告警并跳过该 target，其余 7 个 sample
  不受影响。Dockerfile 已在 builder/runtime 两阶段分别装好开发包与运行库。
  `public/VictoriaMulti.mkv`（约 133MB）已通过 Git LFS 入库（根 `.gitignore` 的 `*.mkv` 规则有
  例外 `!Demo/linux/public/VictoriaMulti.mkv`）；克隆后若 `public/` 下为指针文件，先 `git lfs pull`。
  Docker 构建会把它打进镜像。
- **视频发送仅限 MKV sample**：其余发送 sample 只做音频多声道（参考 sample 名中的 h264 已移除）；
  `sample_send_mkv_multiopus` 音/视频各一个线程、各自独立循环，不做严格 AV 同步。
- 混音/播放路径 SDK 输出恒为 2 声道（奇数声道混一路、偶数声道混一路；混音 sample 的
  `--numOfChannels 1` 可输出 mono）；需逐声道数据请用
  `sample_receive_pcm_multiopus` 或 `sample_receive_encoded_audio`。
