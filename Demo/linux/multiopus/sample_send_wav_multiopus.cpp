//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 多声道 WAV 文件发送
//
//  对应需求规格需求点2（多声道 WAV 文件发送）：
//   - 解析标准 RIFF/WAVE 头，打印声道数 / 采样率 / 位深日志
//   - 16bit PCM 直接发送；24bit PCM 逐样本转换为 16bit 后发送（小端读取、符号扩展、右移 8bit）
//   - 其他位深报错并停止读取该文件，不发送
//   - 采样率非 48000 打印警告（不做重采样；多声道编解码仅支持 48 kHz，
//     非 48 kHz 素材直接发送会导致音调/时长异常，需业务侧先重采样）
//   - 发送节奏 10 ms/帧，samplesPerChannel = sampleRate/100
//
//  多声道私参设置时机与 sample_send_pcm_multiopus 一致（spec 需求点4）。
//  说明：本 demo 只做音频多声道，去掉了参考 sample 中的 H264 视频发送逻辑。
//

#include <csignal>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>

#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/helper.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"

#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_SAMPLE_RATE (48000)
#define DEFAULT_NUM_OF_CHANNELS (8)
#define DEFAULT_AUDIO_FILE "public/8_Channel_ID_signed_16_bit_pcm.wav"

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  int customChannelNum = 0;    // che.audio.custom_channel_num，3/5/7 声道必须显式设置
  int customPayloadType = 0;   // che.audio.custom_payload_type，0 表示按声道数自动推导
  int customSampleRate = 0;    // che.audio.custom_sample_rate，0 表示跟随 WAV 头采样率
  bool codecUnfallback = false;  // che.audio.codec_unfallback，service 级、createRtcConnection 前设置

  struct {
    int sampleRate = DEFAULT_SAMPLE_RATE;      // 仅用于与 WAV 头核对，实际以 WAV 头为准
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;  // 仅用于与 WAV 头核对，实际以 WAV 头为准
  } audio;
};

// WAV 文件格式信息（由 RIFF chunk 解析得到）
struct WavFormat {
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  long dataOffset = 0;     // "data" chunk 数据区在文件中的偏移
  uint32_t dataSize = 0;   // "data" chunk 数据区字节数
};

// payload type 与声道数映射（spec 场景4.1）：
//   123 -> 3/4 声道（默认 4），124 -> 5/6 声道（默认 6），125 -> 7/8 声道（默认 8）
static int payloadTypeForChannels(int numOfChannels) {
  if (numOfChannels <= 4) return 123;
  if (numOfChannels <= 6) return 124;
  return 125;
}

static bool readChunkId(FILE* file, char id[4]) { return fread(id, 1, 4, file) == 4; }

static bool readU32LE(FILE* file, uint32_t& value) {
  uint8_t buf[4];
  if (fread(buf, 1, 4, file) != 4) return false;
  value = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
          ((uint32_t)buf[3] << 24);
  return true;
}

static bool readU16LE(FILE* file, uint16_t& value) {
  uint8_t buf[2];
  if (fread(buf, 1, 2, file) != 2) return false;
  value = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  return true;
}

// 按 RIFF chunk 逐个扫描解析 WAV 头（兼容 fmt chunk 长度 >16 的 WAVE_FORMAT_EXTENSIBLE）
static bool parseWavHeader(FILE* file, WavFormat& format) {
  char chunkId[4];
  uint32_t chunkSize = 0;

  if (!readChunkId(file, chunkId) || memcmp(chunkId, "RIFF", 4) != 0) {
    AG_LOG(ERROR, "Not a RIFF file");
    return false;
  }
  if (!readU32LE(file, chunkSize)) return false;  // RIFF size，不使用
  if (!readChunkId(file, chunkId) || memcmp(chunkId, "WAVE", 4) != 0) {
    AG_LOG(ERROR, "Not a WAVE file");
    return false;
  }

  bool fmtFound = false;
  bool dataFound = false;
  while (!dataFound && readChunkId(file, chunkId) && readU32LE(file, chunkSize)) {
    if (memcmp(chunkId, "fmt ", 4) == 0) {
      if (chunkSize < 16) {
        AG_LOG(ERROR, "Invalid fmt chunk size: %u", chunkSize);
        return false;
      }
      uint16_t formatType = 0;
      uint32_t byteRate = 0;
      uint16_t blockAlign = 0;
      if (!readU16LE(file, formatType) || !readU16LE(file, format.channels) ||
          !readU32LE(file, format.sampleRate) || !readU32LE(file, byteRate) ||
          !readU16LE(file, blockAlign) || !readU16LE(file, format.bitsPerSample)) {
        AG_LOG(ERROR, "Failed to parse fmt chunk");
        return false;
      }
      if (formatType != 1 && formatType != 0xFFFE /* WAVE_FORMAT_EXTENSIBLE */) {
        AG_LOG(ERROR, "Unsupported WAV format type: %d (only PCM is supported)", formatType);
        return false;
      }
      // fmt chunk may be larger than 16 bytes; RIFF chunks are word-aligned.
      if (chunkSize > 16 && fseek(file, chunkSize - 16, SEEK_CUR) != 0) return false;
      if ((chunkSize & 1) && fseek(file, 1, SEEK_CUR) != 0) return false;
      fmtFound = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      format.dataOffset = ftell(file);
      format.dataSize = chunkSize;
      dataFound = true;
    } else {
      // 跳过其他 chunk（LIST、fact 等），先校验 chunkSize 不超过文件剩余长度
      long currentPos = ftell(file);
      fseek(file, 0, SEEK_END);
      long fileSize = ftell(file);
      fseek(file, currentPos, SEEK_SET);
      if (chunkSize > static_cast<uint32_t>(fileSize - currentPos)) {
        AG_LOG(ERROR, "Invalid chunk size %u at offset %ld (exceeds file remaining %ld)",
               chunkSize, currentPos, fileSize - currentPos);
        return false;
      }
      if (fseek(file, chunkSize + (chunkSize & 1), SEEK_CUR) != 0) return false;
    }
  }

  if (!fmtFound || !dataFound) {
    AG_LOG(ERROR, "Invalid WAV file: fmt or data chunk not found");
    return false;
  }
  return true;
}

static bool validateWavOptions(const SampleOptions& options) {
  FILE* file = fopen(options.audioFile.c_str(), "rb");
  if (!file) {
    AG_LOG(ERROR, "Failed to open audio file %s", options.audioFile.c_str());
    return false;
  }

  WavFormat wav;
  bool parsed = parseWavHeader(file, wav);
  fclose(file);
  if (!parsed) return false;

  if (wav.bitsPerSample != 16 && wav.bitsPerSample != 24) {
    AG_LOG(ERROR, "Unsupported bit depth: %d. Only 16-bit and 24-bit PCM are supported.",
           wav.bitsPerSample);
    return false;
  }
  if (wav.sampleRate != 48000) {
    AG_LOG(ERROR, "WAV sample rate must be 48000 for multi-channel OPUSMC, got %d.",
           wav.sampleRate);
    return false;
  }
  if (wav.channels < 3 || wav.channels > 8) {
    AG_LOG(ERROR, "WAV channel count must be in [3, 8], got %d.", wav.channels);
    return false;
  }
  if (wav.sampleRate != options.audio.sampleRate || wav.channels != options.audio.numOfChannels) {
    AG_LOG(ERROR,
           "WAV header (%d Hz, %d channels) must match --sampleRate (%d) and --numOfChannels (%d).",
           wav.sampleRate, wav.channels, options.audio.sampleRate, options.audio.numOfChannels);
    return false;
  }
  return true;
}

static void sendOnePcmFrame(const SampleOptions& options,
                            agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender) {
  static FILE* file = nullptr;
  static WavFormat wav;
  const char* fileName = options.audioFile.c_str();

  // 打开文件并解析 WAV 头（每个文件只解析一次）
  if (!file) {
    if (!(file = fopen(fileName, "rb"))) {
      AG_LOG(ERROR, "Failed to open audio file %s", fileName);
      return;
    }
    if (!parseWavHeader(file, wav)) {
      fclose(file);
      file = nullptr;
      return;
    }

    AG_LOG(INFO, "Opened WAV file: %s", fileName);
    AG_LOG(INFO, "Sample Rate: %d, Channels: %d, Bits Per Sample: %d", wav.sampleRate,
           wav.channels, wav.bitsPerSample);

  }

  // 每 10ms 帧的样本数与字节数
  int samplesPer10ms = wav.sampleRate / 100;
  int originalSampleBytes = wav.bitsPerSample / 8;  // 24bit=3 字节，16bit=2 字节
  int originalFrameBytes = originalSampleBytes * wav.channels * samplesPer10ms;
  int outputFrameBytes = sizeof(int16_t) * wav.channels * samplesPer10ms;  // 输出恒为 16bit

  uint8_t* inputBuf = new uint8_t[originalFrameBytes];
  uint8_t* outputBuf = new uint8_t[outputFrameBytes];

  if (fread(inputBuf, 1, originalFrameBytes, file) != (size_t)originalFrameBytes) {
    if (feof(file)) {
      fclose(file);
      file = nullptr;
      AG_LOG(INFO, "End of audio file");
    } else {
      AG_LOG(ERROR, "Error reading audio data: %s", std::strerror(errno));
    }
    delete[] inputBuf;
    delete[] outputBuf;
    return;
  }

  if (wav.bitsPerSample == 24) {
    // 24bit -> 16bit：小端读取、符号扩展、右移 8bit（spec 场景2.2）
    int totalSamples = wav.channels * samplesPer10ms;
    for (int i = 0; i < totalSamples; i++) {
      const uint8_t* sample24 = &inputBuf[i * 3];
      int32_t sample32 = sample24[0] | (sample24[1] << 8) | (sample24[2] << 16);
      if (sample32 & 0x800000) {
        sample32 |= (int32_t)0xFF000000;  // 符号扩展
      }
      int16_t sample16 = static_cast<int16_t>(sample32 >> 8);
      outputBuf[i * 2] = static_cast<uint8_t>(sample16 & 0xFF);
      outputBuf[i * 2 + 1] = static_cast<uint8_t>((sample16 >> 8) & 0xFF);
    }
  } else {
    // 16bit 直接发送（spec 场景2.1）
    memcpy(outputBuf, inputBuf, outputFrameBytes);
  }

  // 输出给 SDK 的位深恒为 16bit（TWO_BYTES_PER_SAMPLE）
  if (audioFrameSender->sendAudioPcmData(outputBuf, 0, 0, samplesPer10ms,
                                         agora::rtc::TWO_BYTES_PER_SAMPLE, wav.channels,
                                         wav.sampleRate) < 0) {
    AG_LOG(ERROR, "Failed to send audio frame!");
  }

  delete[] inputBuf;
  delete[] outputBuf;
}

static void SampleSendAudioTask(
    const SampleOptions& options,
    agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender, bool& exitFlag) {
  // Currently only 10 ms PCM frame is supported. So PCM frames are sent at 10 ms interval
  PacerInfo pacer = {0, 10, 0, std::chrono::steady_clock::now()};

  while (!exitFlag) {
    sendOnePcmFrame(options, audioFrameSender);
    waitBeforeNextSend(pacer);  // sleep for a while before sending next frame
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
  optParser.add_long_opt("audioFile", &options.audioFile,
                         "The multi-channel WAV file to be sent (16/24bit PCM)");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Expected sample rate, checked against the WAV header (48000)");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Expected number of channels, checked against the WAV header (3~8)");
  optParser.add_long_opt("custom_channel_num", &options.customChannelNum,
                         "che.audio.custom_channel_num, required for 3/5/7 channels");
  optParser.add_long_opt("custom_payload_type", &options.customPayloadType,
                         "che.audio.custom_payload_type: 123->3/4ch, 124->5/6ch, 125->7/8ch");
  optParser.add_long_opt("custom_sample_rate", &options.customSampleRate,
                         "che.audio.custom_sample_rate, default follows the WAV header");
  optParser.add_long_opt("codecUnfallback", &options.codecUnfallback,
                         "Set che.audio.codec_unfallback=[123,124,125] on service level "
                         "before createRtcConnection to force multi-channel codec");

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

  if (options.audioFile.empty()) {
    AG_LOG(ERROR, "Must provide audio file path!");
    return -1;
  }
  AG_LOG(INFO, "input audio file location: %s", options.audioFile.c_str());

  if (options.audio.numOfChannels < 3 || options.audio.numOfChannels > 8) {
    AG_LOG(ERROR, "numOfChannels must be in [3, 8] for multi-channel (OPUSMC) sending!");
    return -1;
  }
  if (!validateWavOptions(options)) return -1;

  // 未显式指定时按声道数推导 payload type（123->3/4, 124->5/6, 125->7/8）
  if (options.customPayloadType == 0) {
    options.customPayloadType = payloadTypeForChannels(options.audio.numOfChannels);
    AG_LOG(INFO, "custom_payload_type not set, derived %d from numOfChannels=%d",
           options.customPayloadType, options.audio.numOfChannels);
  }
  // 校验 payload type 合法性（OPUSMC 仅支持 123/124/125）
  if (options.customPayloadType != 123 && options.customPayloadType != 124 &&
      options.customPayloadType != 125) {
    AG_LOG(ERROR, "custom_payload_type must be 123, 124 or 125, got %d",
           options.customPayloadType);
    return -1;
  }
  if (options.customPayloadType != payloadTypeForChannels(options.audio.numOfChannels)) {
    AG_LOG(ERROR, "custom_payload_type %d is incompatible with numOfChannels=%d",
           options.customPayloadType, options.audio.numOfChannels);
    return -1;
  }

  // 未显式指定 custom_channel_num 时跟随 numOfChannels（4/6/8 为默认值，3/5/7 必须显式设置）
  if (options.customChannelNum == 0) {
    options.customChannelNum = options.audio.numOfChannels;
  }
  if (options.customSampleRate == 0) {
    options.customSampleRate = options.audio.sampleRate;
  }
  if (options.customChannelNum != options.audio.numOfChannels ||
      options.customSampleRate != options.audio.sampleRate) {
    AG_LOG(ERROR, "custom channel count and sample rate must match the WAV header.");
    return -1;
  }

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
  auto p = connection->getAgoraParameter();
  p->setInt("che.audio.custom_payload_type", options.customPayloadType);
  p->setInt("che.audio.custom_channel_num", options.customChannelNum);
  p->setInt("che.audio.custom_sample_rate", options.customSampleRate);
  AG_LOG(INFO, "Set connection params: payload_type=%d channel_num=%d sample_rate=%d",
         options.customPayloadType, options.customChannelNum, options.customSampleRate);

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

  // Publish audio track
  connection->getLocalUser()->publishAudio(customAudioTrack);

  // Wait until connected before sending media stream
  connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS);

  // Start sending media data
  AG_LOG(INFO, "Start sending multi-channel WAV audio data ...");
  std::thread sendAudioThread(SampleSendAudioTask, options, audioFrameSender, std::ref(exitFlag));

  sendAudioThread.join();

  // Unpublish audio track
  connection->getLocalUser()->unpublishAudio(customAudioTrack);

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
  factory = nullptr;
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
