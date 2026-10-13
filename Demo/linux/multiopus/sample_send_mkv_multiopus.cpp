//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: MKV 文件发送（4xstereo 音轨 -> 8 声道 + 视频）
//
//  针对 1 路视频 + 4 条立体声音轨的 MKV 素材（如 public/VictoriaMulti.mkv：
//  mpeg4 1920x1080 + 4 x AC-3 44.1kHz stereo，共承载 8 声道内容）：
//   - 启动时用 ffmpeg 一次性解码 4 条 AC-3 音轨，逐轨 swresample 到 48kHz stereo s16，
//     再按需求点10 的 4xstereo 布局重排为逐样本交错 8 声道 PCM，写入临时文件
//     （/tmp/<mkv名>_8ch48k.pcm，243s 素材约 187MB，避免全量驻留内存）；
//     声道顺序 CH1=T1L, CH2=T1R, ..., CH8=T4R；各轨长度差异（~35ms 级）末尾补零对齐。
//     临时文件若比 MKV 新则直接复用，跳过重复转码。
//   - 音频发送：10ms/帧（480 样本/声道），PacerInfo 节奏，到文件末尾循环
//   - 视频发送：边解边发（av_read_frame -> mpeg4 decode -> sws_scale 转 I420 ->
//     sendVideoFrame），按帧 PTS 节奏，到文件末尾循环
//   - 音/视频各一个线程、各自独立循环，demo 不做严格 AV 同步
//
//  声道数 8 / payload type 125 / 采样率 48000 由 4xstereo 布局决定，写死；
//  私参设置时机与 sample_send_wav_multiopus 一致（spec 需求点4）。
//

#include <csignal>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/helper.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraVideoTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"

#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_VIDEO_FILE "public/VictoriaMulti.mkv"

// 4xstereo 布局决定的固定参数（spec 需求点10）
#define EXPECTED_AUDIO_TRACKS (4)   // MKV 中必须恰好 4 条立体声音轨
#define CHANNELS_PER_TRACK (2)
#define TOTAL_CHANNELS (8)
#define AUDIO_SAMPLE_RATE (48000)
#define CUSTOM_PAYLOAD_TYPE (125)   // 7/8 声道
#define SAMPLES_PER_10MS (AUDIO_SAMPLE_RATE / 100)

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string videoFile = DEFAULT_VIDEO_FILE;
  bool codecUnfallback = false;  // che.audio.codec_unfallback，service 级、createRtcConnection 前设置
};

// MKV 视频流信息（probe 阶段获得）
struct MkvVideoInfo {
  int width = 0;
  int height = 0;
  double fps = 0.0;
};

// 取文件名（去目录、去扩展名），用于派生临时 PCM 文件名
static std::string basenameNoExt(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

// 转换产物已存在且不比 MKV 旧时直接复用，避免每次启动重复解码
static bool pcmUpToDate(const std::string& pcmPath, const std::string& mkvPath) {
  struct stat stPcm, stMkv;
  if (stat(pcmPath.c_str(), &stPcm) != 0 || stat(mkvPath.c_str(), &stMkv) != 0) return false;
  return stPcm.st_size > 0 && stPcm.st_mtime >= stMkv.st_mtime;
}

// 前置校验：ffmpeg 能打开、恰好 1 路视频、恰好 4 条 stereo 音轨
static bool probeMkvFile(const std::string& path, MkvVideoInfo& videoInfo) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
    AG_LOG(ERROR, "ffmpeg failed to open video file %s", path.c_str());
    return false;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    AG_LOG(ERROR, "ffmpeg failed to read stream info from %s", path.c_str());
    avformat_close_input(&fmt);
    return false;
  }

  int videoStreams = 0;
  int audioStreams = 0;
  bool audioLayoutOk = true;
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    AVCodecParameters* cp = fmt->streams[i]->codecpar;
    if (cp->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStreams++;
      videoInfo.width = cp->width;
      videoInfo.height = cp->height;
      AVRational fr = fmt->streams[i]->avg_frame_rate;
      videoInfo.fps = (fr.num > 0 && fr.den > 0) ? (double)fr.num / fr.den : 25.0;
    } else if (cp->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStreams++;
      if (cp->ch_layout.nb_channels != CHANNELS_PER_TRACK) audioLayoutOk = false;
    }
  }
  avformat_close_input(&fmt);

  if (videoStreams != 1) {
    AG_LOG(ERROR, "MKV must contain exactly 1 video stream, got %d", videoStreams);
    return false;
  }
  if (audioStreams != EXPECTED_AUDIO_TRACKS || !audioLayoutOk) {
    AG_LOG(ERROR, "MKV must contain exactly %d stereo audio tracks (4xstereo layout), "
                  "got %d audio stream(s), stereo check %s",
           EXPECTED_AUDIO_TRACKS, audioStreams, audioLayoutOk ? "ok" : "failed");
    return false;
  }
  AG_LOG(INFO, "MKV probe ok: video %dx%d @ %.2f fps, %d stereo audio tracks",
         videoInfo.width, videoInfo.height, videoInfo.fps, audioStreams);
  return true;
}

// 单条音轨的解码上下文
struct TrackDecodeCtx {
  AVCodecContext* dec = nullptr;
  SwrContext* swr = nullptr;
  FILE* out = nullptr;
  int64_t writtenSamples = 0;  // 已写出的样本数（每声道）
};

// 解码一帧 -> swresample 到 48kHz stereo s16 -> 追加写临时文件
static bool decodeResampleWrite(TrackDecodeCtx& ctx, AVFrame* frame) {
  int outMax = swr_get_out_samples(ctx.swr, frame->nb_samples);
  if (outMax <= 0) return true;
  uint8_t* outBuf = nullptr;
  if (av_samples_alloc(&outBuf, nullptr, CHANNELS_PER_TRACK, outMax, AV_SAMPLE_FMT_S16, 0) < 0) {
    AG_LOG(ERROR, "Failed to alloc resample buffer");
    return false;
  }
  int got = swr_convert(ctx.swr, &outBuf, outMax,
                        (const uint8_t**)frame->extended_data, frame->nb_samples);
  if (got > 0) {
    size_t bytes = (size_t)got * CHANNELS_PER_TRACK * sizeof(int16_t);
    if (fwrite(outBuf, 1, bytes, ctx.out) != bytes) {
      AG_LOG(ERROR, "Error writing track pcm data: %s", std::strerror(errno));
      av_freep(&outBuf);
      return false;
    }
    ctx.writtenSamples += got;
  }
  av_freep(&outBuf);
  return true;
}

// 一次性解码 4 条音轨：单遍读取 MKV，按流分发到 4 个解码器，
// 每轨输出 48kHz stereo s16 临时文件
static bool decodeAudioTracks(const std::string& path, const std::string tmpFiles[EXPECTED_AUDIO_TRACKS],
                              int64_t outSamples[EXPECTED_AUDIO_TRACKS]) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
    AG_LOG(ERROR, "ffmpeg failed to open video file %s", path.c_str());
    return false;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    avformat_close_input(&fmt);
    return false;
  }

  int audioIdx[EXPECTED_AUDIO_TRACKS];
  int trackCount = 0;
  for (unsigned i = 0; i < fmt->nb_streams && trackCount < EXPECTED_AUDIO_TRACKS; i++) {
    if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioIdx[trackCount++] = (int)i;
    }
  }
  if (trackCount != EXPECTED_AUDIO_TRACKS) {
    AG_LOG(ERROR, "Expected %d audio tracks, got %d", EXPECTED_AUDIO_TRACKS, trackCount);
    avformat_close_input(&fmt);
    return false;
  }

  TrackDecodeCtx tracks[EXPECTED_AUDIO_TRACKS];
  // 统一清理：关闭已创建的临时文件并释放解码/重采样上下文（函数末尾清理同款），
  // 错误路径先调用它再 avformat_close_input，避免已打开轨的资源泄漏
  auto cleanupTracks = [&]() {
    for (int k = 0; k < EXPECTED_AUDIO_TRACKS; k++) {
      if (tracks[k].out) fclose(tracks[k].out);
      if (tracks[k].swr) swr_free(&tracks[k].swr);
      if (tracks[k].dec) avcodec_free_context(&tracks[k].dec);
    }
  };
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
    AVCodecParameters* cp = fmt->streams[audioIdx[t]]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
    if (!codec) {
      AG_LOG(ERROR, "No decoder for audio track %d (codec id %d)", t, cp->codec_id);
      cleanupTracks();
      avformat_close_input(&fmt);
      return false;
    }
    tracks[t].dec = avcodec_alloc_context3(codec);
    if (!tracks[t].dec ||
        avcodec_parameters_to_context(tracks[t].dec, cp) < 0 ||
        avcodec_open2(tracks[t].dec, codec, nullptr) < 0) {
      AG_LOG(ERROR, "Failed to open decoder for audio track %d", t);
      cleanupTracks();
      avformat_close_input(&fmt);
      return false;
    }
    // 重采样目标固定 48kHz stereo s16（交错）
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, CHANNELS_PER_TRACK);
    AVChannelLayout inLayout = tracks[t].dec->ch_layout;
    if (inLayout.nb_channels == 0) {
      // 个别流未声明布局时按 2 声道处理
      av_channel_layout_default(&inLayout, CHANNELS_PER_TRACK);
    }
    if (swr_alloc_set_opts2(&tracks[t].swr, &outLayout, AV_SAMPLE_FMT_S16, AUDIO_SAMPLE_RATE,
                            &inLayout, tracks[t].dec->sample_fmt, tracks[t].dec->sample_rate,
                            0, nullptr) < 0 ||
        !tracks[t].swr || swr_init(tracks[t].swr) < 0) {
      AG_LOG(ERROR, "Failed to init swresample for audio track %d", t);
      cleanupTracks();
      avformat_close_input(&fmt);
      return false;
    }
    if (!(tracks[t].out = fopen(tmpFiles[t].c_str(), "wb"))) {
      AG_LOG(ERROR, "Failed to create temp pcm file %s", tmpFiles[t].c_str());
      cleanupTracks();
      avformat_close_input(&fmt);
      return false;
    }
    AG_LOG(INFO, "Audio track %d: %d Hz %d ch -> %d Hz stereo s16", t,
           tracks[t].dec->sample_rate, tracks[t].dec->ch_layout.nb_channels, AUDIO_SAMPLE_RATE);
  }

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  bool ok = true;
  while (ok && av_read_frame(fmt, pkt) >= 0) {
    for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
      if (pkt->stream_index != audioIdx[t]) continue;
      if (avcodec_send_packet(tracks[t].dec, pkt) == 0) {
        while (avcodec_receive_frame(tracks[t].dec, frame) == 0) {
          if (!decodeResampleWrite(tracks[t], frame)) { ok = false; break; }
        }
      }
      break;
    }
    av_packet_unref(pkt);
  }

  // flush 解码器与重采样器残余数据
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS && ok; t++) {
    avcodec_send_packet(tracks[t].dec, nullptr);
    while (avcodec_receive_frame(tracks[t].dec, frame) == 0) {
      if (!decodeResampleWrite(tracks[t], frame)) { ok = false; break; }
    }
    // swr 内部 delay 排空
    int got = 0;
    do {
      uint8_t* outBuf = nullptr;
      int outMax = swr_get_out_samples(tracks[t].swr, 0);
      if (outMax <= 0) break;
      if (av_samples_alloc(&outBuf, nullptr, CHANNELS_PER_TRACK, outMax, AV_SAMPLE_FMT_S16,
                           0) < 0) {
        AG_LOG(ERROR, "Failed to alloc resample buffer");
        ok = false;
        break;
      }
      got = swr_convert(tracks[t].swr, &outBuf, outMax, nullptr, 0);
      if (got > 0) {
        fwrite(outBuf, 1, (size_t)got * CHANNELS_PER_TRACK * sizeof(int16_t), tracks[t].out);
        tracks[t].writtenSamples += got;
      }
      av_freep(&outBuf);
    } while (got > 0);
    outSamples[t] = tracks[t].writtenSamples;
    AG_LOG(INFO, "Audio track %d decoded: %lld samples (%.1f s)", t,
           (long long)tracks[t].writtenSamples,
           (double)tracks[t].writtenSamples / AUDIO_SAMPLE_RATE);
  }

  av_packet_free(&pkt);
  av_frame_free(&frame);
  cleanupTracks();
  avformat_close_input(&fmt);
  return ok;
}

// 4 条 stereo 轨 -> 逐样本交错 8 声道（需求点10 布局：
// CH1=T1L, CH2=T1R, ..., CH8=T4R）；各轨长度不一致时短轨末尾补零
static bool interleaveTo8Ch(const std::string tmpFiles[EXPECTED_AUDIO_TRACKS],
                            const int64_t trackSamples[EXPECTED_AUDIO_TRACKS],
                            const std::string& outPath, int64_t& totalSamples) {
  totalSamples = 0;
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
    if (trackSamples[t] > totalSamples) totalSamples = trackSamples[t];
  }
  if (totalSamples <= 0) {
    AG_LOG(ERROR, "No audio samples decoded");
    return false;
  }

  FILE* in[EXPECTED_AUDIO_TRACKS] = {nullptr};
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
    if (!(in[t] = fopen(tmpFiles[t].c_str(), "rb"))) {
      AG_LOG(ERROR, "Failed to open temp pcm file %s", tmpFiles[t].c_str());
      for (int k = 0; k < t; k++) fclose(in[k]);
      return false;
    }
  }
  FILE* out = fopen(outPath.c_str(), "wb");
  if (!out) {
    AG_LOG(ERROR, "Failed to create 8ch pcm file %s", outPath.c_str());
    for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) fclose(in[t]);
    return false;
  }

  const int chunk = AUDIO_SAMPLE_RATE / 10;  // 每次处理 100ms
  std::vector<int16_t> trackBuf[EXPECTED_AUDIO_TRACKS];
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) trackBuf[t].resize(chunk * CHANNELS_PER_TRACK, 0);
  std::vector<int16_t> outBuf(chunk * TOTAL_CHANNELS);

  bool ok = true;
  int64_t written = 0;
  while (ok && written < totalSamples) {
    int n = (int)std::min<int64_t>(chunk, totalSamples - written);
    for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
      // 短轨读不到的部分保持为 0（补零对齐）
      std::fill(trackBuf[t].begin(), trackBuf[t].begin() + n * CHANNELS_PER_TRACK, 0);
      size_t want = (size_t)n * CHANNELS_PER_TRACK;
      if (fread(trackBuf[t].data(), sizeof(int16_t), want, in[t]) != want && ferror(in[t])) {
        AG_LOG(WARNING, "Short read on track %d temp pcm file: %s", t, std::strerror(errno));
      }
    }
    for (int i = 0; i < n; i++) {
      for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
        outBuf[i * TOTAL_CHANNELS + t * 2] = trackBuf[t][i * 2];
        outBuf[i * TOTAL_CHANNELS + t * 2 + 1] = trackBuf[t][i * 2 + 1];
      }
    }
    size_t bytes = (size_t)n * TOTAL_CHANNELS * sizeof(int16_t);
    if (fwrite(outBuf.data(), 1, bytes, out) != bytes) {
      AG_LOG(ERROR, "Error writing 8ch pcm data: %s", std::strerror(errno));
      ok = false;
    }
    written += n;
  }

  fclose(out);
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) fclose(in[t]);
  if (ok) {
    AG_LOG(INFO, "Interleaved 8ch pcm written: %s (%lld samples, %.1f s, %.1f MB)",
           outPath.c_str(), (long long)written, (double)written / AUDIO_SAMPLE_RATE,
           (double)written * TOTAL_CHANNELS * sizeof(int16_t) / 1024 / 1024);
  }
  return ok;
}

// 启动准备：probe -> 解码 4 音轨 -> 重排 8 声道，返回临时 PCM 文件路径
static bool prepareAudioPcm(const SampleOptions& options, std::string& pcmPath) {
  std::string base = basenameNoExt(options.videoFile);
  pcmPath = "/tmp/" + base + "_8ch48k.pcm";
  if (pcmUpToDate(pcmPath, options.videoFile)) {
    AG_LOG(INFO, "Reuse existing converted pcm %s (newer than mkv)", pcmPath.c_str());
    return true;
  }

  std::string tmpFiles[EXPECTED_AUDIO_TRACKS];
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) {
    tmpFiles[t] = "/tmp/" + base + "_track" + to_string(t) + "_48k2ch.pcm";
  }

  AG_LOG(INFO, "Decoding %d AC-3 tracks from %s ...", EXPECTED_AUDIO_TRACKS,
         options.videoFile.c_str());
  int64_t trackSamples[EXPECTED_AUDIO_TRACKS] = {0};
  if (!decodeAudioTracks(options.videoFile, tmpFiles, trackSamples)) return false;

  int64_t totalSamples = 0;
  bool ok = interleaveTo8Ch(tmpFiles, trackSamples, pcmPath, totalSamples);
  for (int t = 0; t < EXPECTED_AUDIO_TRACKS; t++) remove(tmpFiles[t].c_str());
  return ok;
}

// 音频发送线程：10ms/帧读 8 声道临时 PCM，到末尾循环
static void SampleSendAudioTask(
    const std::string& pcmPath,
    agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender, bool& exitFlag) {
  // Currently only 10 ms PCM frame is supported. So PCM frames are sent at 10 ms interval
  PacerInfo pacer = {0, 10, 0, std::chrono::steady_clock::now()};
  const int frameBytes = SAMPLES_PER_10MS * TOTAL_CHANNELS * sizeof(int16_t);  // 7680
  std::vector<uint8_t> frameBuf(frameBytes);
  int64_t framesSent = 0;

  FILE* file = fopen(pcmPath.c_str(), "rb");
  if (!file) {
    AG_LOG(ERROR, "Failed to open converted pcm file %s", pcmPath.c_str());
    return;
  }
  AG_LOG(INFO, "Opened converted pcm file: %s", pcmPath.c_str());

  while (!exitFlag) {
    if (fread(frameBuf.data(), 1, frameBytes, file) != (size_t)frameBytes) {
      if (feof(file)) {
        AG_LOG(INFO, "End of audio file, loop from beginning (frames sent: %lld)",
               (long long)framesSent);
        fclose(file);
        file = fopen(pcmPath.c_str(), "rb");
        if (!file) {
          AG_LOG(ERROR, "Failed to reopen pcm file %s", pcmPath.c_str());
          return;
        }
        continue;
      }
      AG_LOG(ERROR, "Error reading pcm data: %s", std::strerror(errno));
      fclose(file);
      return;
    }

    if (audioFrameSender->sendAudioPcmData(frameBuf.data(), 0, 0, SAMPLES_PER_10MS,
                                           agora::rtc::TWO_BYTES_PER_SAMPLE, TOTAL_CHANNELS,
                                           AUDIO_SAMPLE_RATE) < 0) {
      AG_LOG(ERROR, "Failed to send audio frame!");
    }
    framesSent++;
    if (framesSent % 500 == 0) {  // 每 5s 打一次
      AG_LOG(INFO, "Audio frames sent: %lld (%dch %dHz, %.1f s)",
             (long long)framesSent, TOTAL_CHANNELS, AUDIO_SAMPLE_RATE, framesSent / 100.0);
    }
    waitBeforeNextSend(pacer);  // sleep for a while before sending next frame
  }
  fclose(file);
}

// 视频单遍发送：打开 MKV -> 逐帧解码 -> sws 转 I420 -> 按帧 PTS 节奏发送
// 返回 false 表示解码环境初始化失败（不应重试）；正常到文件末尾返回 true
static bool sendVideoPass(const std::string& path, int width, int height, double fps,
                          agora::agora_refptr<agora::rtc::IVideoFrameSender> videoFrameSender,
                          bool& exitFlag, int64_t& totalSent) {
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
    AG_LOG(ERROR, "ffmpeg failed to open video file %s", path.c_str());
    return false;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    avformat_close_input(&fmt);
    return false;
  }
  int videoIdx = -1;
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { videoIdx = (int)i; break; }
  }
  if (videoIdx < 0) {
    AG_LOG(ERROR, "No video stream in %s", path.c_str());
    avformat_close_input(&fmt);
    return false;
  }

  AVCodecParameters* cp = fmt->streams[videoIdx]->codecpar;
  const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
  AVCodecContext* dec = avcodec_alloc_context3(codec);
  if (!codec || !dec || avcodec_parameters_to_context(dec, cp) < 0 ||
      avcodec_open2(dec, codec, nullptr) < 0) {
    AG_LOG(ERROR, "Failed to open video decoder");
    if (dec) avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return false;
  }

  const double timeBaseSec = av_q2d(fmt->streams[videoIdx]->time_base);
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  SwsContext* sws = nullptr;
  std::vector<uint8_t> i420Buf;
  int64_t firstPts = AV_NOPTS_VALUE;
  int64_t framesThisPass = 0;
  bool dimWarned = false;
  // PTS 锚点：第一帧 PTS 对应当前墙钟
  std::chrono::steady_clock::time_point wallStart = std::chrono::steady_clock::now();

  // 单帧处理：尺寸检查 -> sws 转 I420 -> PTS 节奏 -> sendVideoFrame
  auto handleFrame = [&]() {
    if (frame->width != width || frame->height != height) {
      if (!dimWarned) {
        AG_LOG(WARNING, "Decoded frame %dx%d differs from probed %dx%d, send as decoded",
               frame->width, frame->height, width, height);
        dimWarned = true;
      }
      width = frame->width;
      height = frame->height;
      if (sws) { sws_freeContext(sws); sws = nullptr; }
    }
    if (!sws) {
      sws = sws_getContext(width, height, (AVPixelFormat)frame->format, width, height,
                           AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
      i420Buf.resize((size_t)width * height * 3 / 2);
      if (!sws) {
        AG_LOG(ERROR, "Failed to create sws context");
        return;
      }
    }
    uint8_t* dst[4] = {i420Buf.data(), i420Buf.data() + (size_t)width * height,
                       i420Buf.data() + (size_t)width * height * 5 / 4, nullptr};
    int dstStride[4] = {width, width / 2, width / 2, 0};
    sws_scale(sws, frame->data, frame->linesize, 0, height, dst, dstStride);

    // 按帧 PTS 节奏发送：target = 首帧墙钟 + (pts - firstPts) * timebase
    if (frame->pts != AV_NOPTS_VALUE) {
      if (firstPts == AV_NOPTS_VALUE) {
        firstPts = frame->pts;
        wallStart = std::chrono::steady_clock::now();
      }
      double offsetMs = (frame->pts - firstPts) * timeBaseSec * 1000.0;
      auto target = wallStart + std::chrono::milliseconds((long long)offsetMs);
      auto now = std::chrono::steady_clock::now();
      if (target > now) std::this_thread::sleep_until(target);
    } else {
      // 无 PTS 时按固定帧率节奏
      auto target =
          wallStart + std::chrono::milliseconds((long long)(framesThisPass * 1000.0 / fps));
      auto now = std::chrono::steady_clock::now();
      if (target > now) std::this_thread::sleep_until(target);
    }

    agora::media::base::ExternalVideoFrame videoFrame;
    videoFrame.type = agora::media::base::ExternalVideoFrame::VIDEO_BUFFER_RAW_DATA;
    videoFrame.format = agora::media::base::VIDEO_PIXEL_I420;
    videoFrame.buffer = i420Buf.data();
    videoFrame.stride = width;
    videoFrame.height = height;
    videoFrame.cropLeft = 0;
    videoFrame.cropTop = 0;
    videoFrame.cropRight = 0;
    videoFrame.cropBottom = 0;
    videoFrame.rotation = 0;
    videoFrame.timestamp = 0;  // 与参考 sample 一致，由 SDK 内部打时间戳

    if (videoFrameSender->sendVideoFrame(videoFrame) < 0) {
      AG_LOG(ERROR, "Failed to send video frame!");
    }
    framesThisPass++;
    totalSent++;
    if (totalSent % 150 == 0) {  // 每 ~5s 打一次
      AG_LOG(INFO, "Video frames sent: %lld (%dx%d @ %.2f fps)", (long long)totalSent,
             width, height, fps);
    }
  };

  // 读到末尾后排空解码器（flush 帧同样发送）
  while (!exitFlag) {
    if (av_read_frame(fmt, pkt) < 0) {
      avcodec_send_packet(dec, nullptr);
      while (!exitFlag && avcodec_receive_frame(dec, frame) == 0) handleFrame();
      break;
    }
    if (pkt->stream_index == videoIdx && avcodec_send_packet(dec, pkt) == 0) {
      while (!exitFlag && avcodec_receive_frame(dec, frame) == 0) handleFrame();
    }
    av_packet_unref(pkt);
  }

  if (sws) sws_freeContext(sws);
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&dec);
  avformat_close_input(&fmt);
  AG_LOG(INFO, "End of video pass (%lld frames this pass)", (long long)framesThisPass);
  return true;
}

// 视频发送线程：文件循环，边解边发
static void SampleSendVideoTask(
    const SampleOptions& options, int width, int height, double fps,
    agora::agora_refptr<agora::rtc::IVideoFrameSender> videoFrameSender, bool& exitFlag) {
  int64_t totalSent = 0;
  while (!exitFlag) {
    if (!sendVideoPass(options.videoFile, width, height, fps, videoFrameSender, exitFlag,
                       totalSent)) {
      return;  // 初始化类错误，不再重试
    }
    if (!exitFlag) AG_LOG(INFO, "Loop video file from beginning (total sent: %lld)",
                          (long long)totalSent);
  }
}

static bool exitFlag = false;
static void SignalHandler(int sigNo) { exitFlag = true; }

int main(int argc, char* argv[]) {
  SampleOptions options;
  opt_parser optParser;

  optParser.add_long_opt("token", &options.appId, "The token for authentication / must");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id / must");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("videoFile", &options.videoFile,
                         "The MKV file to be sent (1 video + 4 stereo audio tracks)");
  optParser.add_long_opt("codecUnfallback", &options.codecUnfallback,
                         "Pass 1 to set che.audio.codec_unfallback=[123,124,125] on service "
                         "level before createRtcConnection to force multi-channel codec");

  if ((argc <= 1) || !optParser.parse_opts(argc, argv)) {
    std::ostringstream strStream;
    optParser.print_usage(argv[0], strStream);
    std::cout << strStream.str() << std::endl;
    return -1;
  }

  if (options.appId.empty()) {
    AG_LOG(ERROR, "Must provide appId!");
    return -1;
  }

  if (options.channelId.empty()) {
    AG_LOG(ERROR, "Must provide channelId!");
    return -1;
  }

  AG_LOG(INFO, "input video file location: %s", options.videoFile.c_str());

  // 前置校验：ffmpeg 打不开 / 无视频流 / 音轨不是 4xstereo -> 报错退出（不连网）
  MkvVideoInfo videoInfo;
  if (!probeMkvFile(options.videoFile, videoInfo)) return -1;

  // 启动时一次性完成音频转码：4xAC-3 -> 交错 8 声道 48kHz s16 临时文件
  std::string pcmPath;
  if (!prepareAudioPcm(options, pcmPath)) return -1;

  std::signal(SIGQUIT, SignalHandler);
  std::signal(SIGABRT, SignalHandler);
  std::signal(SIGINT, SignalHandler);

  // Create Agora service
  auto service = createAndInitAgoraService(false, true, true);
  if (!service) {
    AG_LOG(ERROR, "Failed to creating Agora service!");
    return -1;
  }

  // che.audio.codec_unfallback 必须在 service->getAgoraParameter() 上、
  // createRtcConnection() 之前设置；设置在 connection 级不生效（spec 场景4.4）
  if (options.codecUnfallback) {
    auto sp = service->getAgoraParameter();
    sp->setArray("che.audio.codec_unfallback", "[123,124,125]");
    AG_LOG(INFO, "Set che.audio.codec_unfallback=[123,124,125] on service level");
  }

  // Create Agora connection
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
  agora::agora_refptr<agora::rtc::IRtcConnection> connection = service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

  // 多声道私参：必须在 createRtcConnection 之后、connect 之前
  // 对 connection->getAgoraParameter() 设置（spec 场景4.2）
  // 8 声道 / payload 125 / 48000 由 4xstereo 布局决定，写死
  auto p = connection->getAgoraParameter();
  p->setInt("che.audio.custom_payload_type", CUSTOM_PAYLOAD_TYPE);
  p->setInt("che.audio.custom_channel_num", TOTAL_CHANNELS);
  p->setInt("che.audio.custom_sample_rate", AUDIO_SAMPLE_RATE);
  AG_LOG(INFO, "Set connection params: payload_type=%d channel_num=%d sample_rate=%d",
         CUSTOM_PAYLOAD_TYPE, TOTAL_CHANNELS, AUDIO_SAMPLE_RATE);

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  // Create local user observer
  auto localUserObserver = std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  // Connect to Agora channel
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                          options.userId.c_str())) {
    AG_LOG(ERROR, "Failed to connect to Agora channel!");
    return -1;
  }

  // Create media node factory
  agora::agora_refptr<agora::rtc::IMediaNodeFactory> factory = service->createMediaNodeFactory();
  if (!factory) {
    AG_LOG(ERROR, "Failed to create media node factory!");
    return -1;
  }

  // Create audio data sender
  agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender =
      factory->createAudioPcmDataSender();
  if (!audioFrameSender) {
    AG_LOG(ERROR, "Failed to create audio data sender!");
    return -1;
  }

  // Create audio track
  agora::agora_refptr<agora::rtc::ILocalAudioTrack> customAudioTrack =
      service->createCustomAudioTrack(audioFrameSender);
  if (!customAudioTrack) {
    AG_LOG(ERROR, "Failed to create audio track!");
    return -1;
  }

  // Create video frame sender
  agora::agora_refptr<agora::rtc::IVideoFrameSender> videoFrameSender =
      factory->createVideoFrameSender();
  if (!videoFrameSender) {
    AG_LOG(ERROR, "Failed to create video frame sender!");
    return -1;
  }

  // Create video track
  agora::agora_refptr<agora::rtc::ILocalVideoTrack> customVideoTrack =
      service->createCustomVideoTrack(videoFrameSender);
  if (!customVideoTrack) {
    AG_LOG(ERROR, "Failed to create video track!");
    return -1;
  }

  // Configure video encoder（分辨率跟随 MKV 视频流）
  agora::rtc::VideoEncoderConfiguration encoderConfig;
  encoderConfig.codecType = agora::rtc::VIDEO_CODEC_H264;
  encoderConfig.dimensions.width = videoInfo.width;
  encoderConfig.dimensions.height = videoInfo.height;
  encoderConfig.frameRate = (int)(videoInfo.fps + 0.5);
  encoderConfig.bitrate = agora::rtc::STANDARD_BITRATE;
  customVideoTrack->setVideoEncoderConfiguration(encoderConfig);

  // Publish audio & video track
  customAudioTrack->setEnabled(true);
  if (connection->getLocalUser()->publishAudio(customAudioTrack)) {
    AG_LOG(ERROR, "Failed to publish audio track!");
    return -1;
  }
  customVideoTrack->setEnabled(true);
  if (connection->getLocalUser()->publishVideo(customVideoTrack)) {
    AG_LOG(ERROR, "Failed to publish video track!");
    return -1;
  }

  // Wait until connected before sending media stream
  if (connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
    AG_LOG(ERROR, "Wait for connection timeout or failure!");
    return -1;
  }

  // Start sending media data（音频 10ms/帧 PacerInfo 节奏，视频按 PTS 节奏，各自独立循环）
  AG_LOG(INFO, "Start sending 8-channel audio (from %s) & video (%dx%d @ %.2f fps) ...",
         pcmPath.c_str(), videoInfo.width, videoInfo.height, videoInfo.fps);
  std::thread sendAudioThread(SampleSendAudioTask, pcmPath, audioFrameSender, std::ref(exitFlag));
  std::thread sendVideoThread(SampleSendVideoTask, options, videoInfo.width, videoInfo.height,
                              videoInfo.fps, videoFrameSender, std::ref(exitFlag));

  sendAudioThread.join();
  sendVideoThread.join();

  // Unpublish audio & video track
  connection->getLocalUser()->unpublishAudio(customAudioTrack);
  connection->getLocalUser()->unpublishVideo(customVideoTrack);

  // Unregister connection observer
  connection->unregisterObserver(connObserver.get());

  // Disconnect from Agora channel
  if (connection->disconnect()) {
    AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
    return -1;
  }
  AG_LOG(INFO, "Disconnected from Agora channel successfully");

  // Destroy Agora connection and related resources
  connObserver.reset();
  localUserObserver.reset();
  audioFrameSender = nullptr;
  customAudioTrack = nullptr;
  videoFrameSender = nullptr;
  customVideoTrack = nullptr;
  factory = nullptr;
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
