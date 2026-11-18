//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 预编码 8 声道 Opus 裸流发送
//
//  对应需求规格需求点3（预编码多声道 Opus 音频发送）：
//   - 输入为约定格式的 Opus 裸帧流（非 ogg 封装）：每帧 640 字节 = 20 ms @ 48 kHz 8 声道
//     （参考素材 public/8ch_2.opus）
//   - 20 ms pacer 调用 sendEncodedAudioFrame，EncodedAudioFrameInfo 为
//     codec=AUDIO_CODEC_OPUSMC、numberOfChannels=8、sampleRateHz=48000、samplesPerChannel=960
//     （注：spec 写作 AUDIO_CODEC_OPUS8C，SDK v4.4.30.149 头文件中对应枚举为
//      AgoraBase.h 中的 AUDIO_CODEC_OPUSMC = 13，本 demo 以头文件为准）
//   - custom audio track 以 MIX_DISABLED 创建（不经 3A / 混音）
//   - 读到 EOF 后关闭并重新打开文件，循环发送直至退出
//   - 已设置 che.audio.custom_payload_type=125、che.audio.custom_channel_num=8
//     （connection 级、connect 之前）
//
//  说明：sample 不具备 ogg 封装解析能力，输入必须是 640 字节/帧的裸 Opus 流。
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

#include "AgoraBase.h"
#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"

#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_AUDIO_FILE "public/8ch_2.opus"
#define DEFAULT_FRAME_SIZE_DURATION (20)  // ms
#define OPUS8_FRAME_BYTES (640)           // 20 ms @ 48 kHz 8 ch Opus 裸帧
#define OPUS8_NUM_OF_CHANNELS (8)
#define OPUS8_SAMPLE_RATE (48000)
#define OPUS8_SAMPLES_PER_CHANNEL (960)  // 20 ms * 48000 Hz / 1000
#define OPUS8_PAYLOAD_TYPE (125)         // 125 -> 7/8 声道（spec 场景4.1）

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  bool codecUnfallback = false;  // che.audio.codec_unfallback，service 级、createRtcConnection 前设置
  struct {
    int frameSizeDuration = DEFAULT_FRAME_SIZE_DURATION;
  } audio;
};

static void SampleSendAudioTask(
    const SampleOptions& options,
    agora::agora_refptr<agora::rtc::IAudioEncodedFrameSender> audioFrameSender, bool& exitFlag) {
  static FILE* file = nullptr;
  uint8_t frameBuf[OPUS8_FRAME_BYTES];

  // Opus frames are sent at 20 ms interval
  PacerInfo pacer = {0, options.audio.frameSizeDuration, 0, std::chrono::steady_clock::now()};

  while (!exitFlag) {
    if (!file) {
      if (!(file = fopen(options.audioFile.c_str(), "rb"))) {
        AG_LOG(ERROR, "Failed to open audio file %s, stop sending thread", options.audioFile.c_str());
        return;
      }
      AG_LOG(INFO, "Open audio file %s successfully", options.audioFile.c_str());
    }

    if (fread(frameBuf, 1, sizeof(frameBuf), file) != sizeof(frameBuf)) {
      if (feof(file)) {
        AG_LOG(INFO, "End of audio file, loop from the beginning");
      } else {
        // 读错误时同样关闭文件，让下一帧走重开路径（否则 error indicator 置位后永久刷错）
        AG_LOG(ERROR, "Error reading audio data: %s", std::strerror(errno));
      }
      fclose(file);
      file = nullptr;
      continue;
    }

    agora::rtc::EncodedAudioFrameInfo info;
    info.codec = agora::rtc::AUDIO_CODEC_TYPE::AUDIO_CODEC_OPUSMC;
    info.numberOfChannels = OPUS8_NUM_OF_CHANNELS;
    info.sampleRateHz = OPUS8_SAMPLE_RATE;
    info.samplesPerChannel = OPUS8_SAMPLES_PER_CHANNEL;

    if (!audioFrameSender->sendEncodedAudioFrame(frameBuf, sizeof(frameBuf), info)) {
      AG_LOG(ERROR, "Failed to send encoded audio frame!");
    }

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
                         "The 8-channel Opus raw frame stream file to be sent "
                         "(640 bytes per 20ms frame, NOT ogg container)");
  optParser.add_long_opt("frameSizeDuration", &options.audio.frameSizeDuration,
                         "The audio frame size duration to be sent (ms); only 20 is "
                         "supported (frames are hardcoded to 640 bytes / 960 samples)");
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

  // 帧格式硬编码为 640 字节/帧（20ms @ 48kHz 8ch，spec 场景3.1/3.2），
  // frameSizeDuration 只改发送节奏，其他值会与帧格式/samplesPerChannel 脱钩导致破音
  if (options.audio.frameSizeDuration != DEFAULT_FRAME_SIZE_DURATION) {
    AG_LOG(ERROR, "Only --frameSizeDuration %d is supported (frames are hardcoded to %d "
                  "bytes / %d samples per channel), got %d",
           DEFAULT_FRAME_SIZE_DURATION, OPUS8_FRAME_BYTES, OPUS8_SAMPLES_PER_CHANNEL,
           options.audio.frameSizeDuration);
    return -1;
  }

  // 连接前预检素材：可读、为常规文件且至少包含一帧，避免启动后发送线程静默退出或空转
  {
    FILE* f = fopen(options.audioFile.c_str(), "rb");
    if (!f) {
      AG_LOG(ERROR, "Cannot open audio file %s: %s", options.audioFile.c_str(),
             std::strerror(errno));
      return -1;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < OPUS8_FRAME_BYTES) {
      AG_LOG(ERROR,
             "Audio file %s must be a regular file containing at least one %d-byte frame",
             options.audioFile.c_str(), OPUS8_FRAME_BYTES);
      fclose(f);
      return -1;
    }
    fclose(f);
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

  // 多声道私参：connection 级、connect 之前设置（spec 场景4.2）
  auto p = connection->getAgoraParameter();
  p->setInt("che.audio.custom_payload_type", OPUS8_PAYLOAD_TYPE);
  p->setInt("che.audio.custom_channel_num", OPUS8_NUM_OF_CHANNELS);
  p->setInt("che.audio.custom_sample_rate", OPUS8_SAMPLE_RATE);

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

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

  // Create audio encoded frame sender
  agora::agora_refptr<agora::rtc::IAudioEncodedFrameSender> audioFrameSender =
      factory->createAudioEncodedFrameSender();
  if (!audioFrameSender) {
    AG_LOG(ERROR, "Failed to create audio encoded frame sender!");
    return -1;
  }

  // Create audio track with MIX_DISABLED（预编码发送不经 3A / 混音，spec 6.3 技术维度问题1）
  agora::agora_refptr<agora::rtc::ILocalAudioTrack> customAudioTrack =
      service->createCustomAudioTrack(audioFrameSender, agora::base::MIX_DISABLED);
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
    // The custom track was published before waiting. Clean up the observer,
    // connection, and service explicitly so a failed connect cannot abort at
    // process exit while SDK worker threads are still running.
    connection->getLocalUser()->unpublishAudio(customAudioTrack);
    connection->unregisterObserver(connObserver.get());
    if (connection->disconnect()) {
      AG_LOG(ERROR, "Failed to disconnect from Agora channel after connection failure!");
    }
    connObserver.reset();
    audioFrameSender = nullptr;
    customAudioTrack = nullptr;
    factory = nullptr;
    connection = nullptr;
    service->release();
    service = nullptr;
    return -1;
  }

  // Start sending media data
  AG_LOG(INFO, "Start sending 8-channel pre-encoded Opus data ...");
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
  audioFrameSender = nullptr;
  customAudioTrack = nullptr;
  factory = nullptr;
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
