//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: raw PCM 多声道发送
//
//  对应需求规格需求点1（多声道 PCM 音频发送）与需求点4（编码配置与能力协商）：
//   - 3~8 声道、48 kHz、16bit 有符号小端、交错布局 PCM，经 OPUSMC 编码发布到频道
//   - 发送节奏 10 ms/帧，samplesPerChannel = sampleRate/100
//   - 多声道私参 che.audio.custom_payload_type / che.audio.custom_channel_num /
//     che.audio.custom_sample_rate 在 createRtcConnection 之后、connect 之前
//     对 connection->getAgoraParameter() 设置
//   - che.audio.codec_unfallback（--codecUnfallback 开启）在
//     service->getAgoraParameter() 上、createRtcConnection 之前设置
//
//  API 调用流程：
//  service = createAndInitAgoraService()
//      connection = service->createRtcConnection()
//          connection->getAgoraParameter()->setInt(...)   // connect 前设置多声道私参
//          connection->registerObserver()
//          connection->connect()
//          factory = service->createMediaNodeFactory()
//              AudioSender = factory->createAudioPcmDataSender()
//                  AudioTrack = service->createCustomAudioTrack()
//      connection->getLocalUser()->publishAudio(audioTrack)
//                  Sender->sendAudioPcmData()
//
//  说明：本 demo 只做音频多声道，去掉了参考 sample 中的 H264 视频发送逻辑。
//

#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/stat.h>
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
#define DEFAULT_AUDIO_FILE "public/8_Channel_ID_signed_16_bit_pcm.raw.pcm"

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  int customChannelNum = 0;    // che.audio.custom_channel_num，3/5/7 声道必须显式设置
  int customPayloadType = 0;   // che.audio.custom_payload_type，0 表示按声道数自动推导
  int customSampleRate = 0;    // che.audio.custom_sample_rate，0 表示跟随 sampleRate
  bool codecUnfallback = false;  // che.audio.codec_unfallback，service 级、createRtcConnection 前设置

  struct {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
};

// payload type 与声道数映射（spec 场景4.1）：
//   123 -> 3/4 声道（默认 4），124 -> 5/6 声道（默认 6），125 -> 7/8 声道（默认 8）
static int payloadTypeForChannels(int numOfChannels) {
  if (numOfChannels <= 4) return 123;
  if (numOfChannels <= 6) return 124;
  return 125;
}

// 需求点9（4×双声道音轨 -> 8 声道流布局映射）说明：
// sendAudioPcmData 要求的输入是 N 声道逐样本交错（interleaved）PCM。
// 源端 8 声道以 4 条 stereo 音轨、按 track 块序排列（[T1L,T1R]xN [T2L,T2R]xN ...），
// 不能直接喂给本接口；需先重排为逐样本交错布局，声道顺序为
// CH1=T1L, CH2=T1R, CH3=T2L, CH4=T2R, CH5=T3L, CH6=T3R, CH7=T4L, CH8=T4R，
// 即缓冲区按 [T1L,T1R,T2L,T2R,T3L,T3R,T4L,T4R] 每样本重复排列。
// 声道对 (0,1)(2,3)(4,5)(6,7) 分别对应轨 1~4，与 8 声道 OPUSMC 4 个 coupled stream 配对一致。
// 44.1 kHz 源须先重采样到 48 kHz。详见 README「4×stereo 布局映射」一节。

static void sendOnePcmFrame(const SampleOptions& options,
                            agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender) {
  static FILE* file = nullptr;
  const char* fileName = options.audioFile.c_str();

  // Calculate byte size for 10ms audio samples
  int sampleSize = sizeof(int16_t) * options.audio.numOfChannels;
  int samplesPer10ms = options.audio.sampleRate / 100;
  int sendBytes = sampleSize * samplesPer10ms;

  if (!file) {
    if (!(file = fopen(fileName, "rb"))) {
      AG_LOG(ERROR, "Failed to open audio file %s", fileName);
      return;
    }
    AG_LOG(INFO, "Open audio file %s successfully", fileName);
  }

  uint8_t frameBuf[sendBytes];

  if (fread(frameBuf, 1, sizeof(frameBuf), file) != sizeof(frameBuf)) {
    if (feof(file)) {
      AG_LOG(INFO, "End of audio file");
    } else {
      // 读错误时同样关闭文件，让下一帧走重开路径（否则 error indicator 置位后永久刷错）
      AG_LOG(ERROR, "Error reading audio data: %s", std::strerror(errno));
    }
    fclose(file);
    file = nullptr;
    return;
  }

  if (audioFrameSender->sendAudioPcmData(frameBuf, 0, 0, samplesPer10ms,
                                         agora::rtc::TWO_BYTES_PER_SAMPLE,
                                         options.audio.numOfChannels,
                                         options.audio.sampleRate) < 0) {
    AG_LOG(ERROR, "Failed to send audio frame!");
  }
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
                         "The audio file in raw PCM format to be sent");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for the PCM file to be sent (multi-channel only supports 48000)");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for the PCM file to be sent (3~8)");
  optParser.add_long_opt("custom_channel_num", &options.customChannelNum,
                         "che.audio.custom_channel_num, required for 3/5/7 channels");
  optParser.add_long_opt("custom_payload_type", &options.customPayloadType,
                         "che.audio.custom_payload_type: 123->3/4ch, 124->5/6ch, 125->7/8ch");
  optParser.add_long_opt("custom_sample_rate", &options.customSampleRate,
                         "che.audio.custom_sample_rate, default follows --sampleRate");
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

  if (options.audioFile.empty()) {
    AG_LOG(ERROR, "Must provide audio file path!");
    return -1;
  }
  // 连接前预检素材：可读、为常规文件且至少包含一帧，避免启动后发送线程空转刷错误日志
  {
    FILE* f = fopen(options.audioFile.c_str(), "rb");
    if (!f) {
      AG_LOG(ERROR, "Cannot open audio file %s: %s", options.audioFile.c_str(),
             std::strerror(errno));
      return -1;
    }
    struct stat st;
    int frameBytes =
        options.audio.sampleRate / 100 * options.audio.numOfChannels * (int)sizeof(int16_t);
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < frameBytes) {
      AG_LOG(ERROR,
             "Audio file %s must be a regular file containing at least one %d-byte frame",
             options.audioFile.c_str(), frameBytes);
      fclose(f);
      return -1;
    }
    fclose(f);
  }
  AG_LOG(INFO, "input audio file location: %s", options.audioFile.c_str());

  if (options.audio.numOfChannels < 3 || options.audio.numOfChannels > 8) {
    AG_LOG(ERROR, "numOfChannels must be in [3, 8] for multi-channel (OPUSMC) sending!");
    return -1;
  }
  if (options.audio.sampleRate != 48000) {
    AG_LOG(ERROR, "Multi-channel (OPUSMC) codec only supports 48000 Hz sample rate!");
    return -1;
  }

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
    AG_LOG(ERROR, "custom channel count and sample rate must match the PCM input.");
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
  if (connection->getLocalUser()->publishAudio(customAudioTrack)) {
    AG_LOG(ERROR, "Failed to publish audio track!");
    return -1;
  }

  // Wait until connected before sending media stream
  if (connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
    AG_LOG(ERROR, "Wait for connection timeout or failure!");
    return -1;
  }

  // Start sending media data
  AG_LOG(INFO, "Start sending multi-channel audio data (channels=%d, sampleRate=%d) ...",
         options.audio.numOfChannels, options.audio.sampleRate);
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
