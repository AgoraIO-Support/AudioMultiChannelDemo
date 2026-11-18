//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 接收远端多声道音频（解码 PCM 回调）
//
//  对应需求规格需求点5（接收多声道音频——解码 PCM 回调）：
//   - 通过 setPlaybackAudioFrameBeforeMixingParameters(numOfChannels, 48000) 设置回调参数
//   - onPlaybackAudioFrameBeforeMixing 按远端用户回调解码后的多声道 PCM（16bit 交错）
//   - 回调声道数须与发送端声道数一致，否则无法还原完整多声道（spec 场景4.5/5.1）
//   - 按远端 UID 分文件落盘，单文件达到 100 MB 自动切换新文件；
//     可用 ffplay / Audacity 按 48 kHz / N 声道 / 16bit 导入逐声道核对
//
//  说明：payload 123/124/125 协商机制下，接收端无需设置 che.audio.custom_payload_type。
//       本 demo 只做音频，去掉了参考 sample_receive_yuv_pcm 中的 YUV 视频接收逻辑。
//

#include <csignal>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "AgoraRefCountedObject.h"
#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"

#define DEFAULT_SAMPLE_RATE (48000)
#define DEFAULT_NUM_OF_CHANNELS (8)
#define DEFAULT_AUDIO_FILE "received_audio_multiopus.pcm"
#define DEFAULT_FILE_LIMIT (100 * 1024 * 1024)  // 100 MB 切换新文件
#define DEFAULT_CONNECT_TIMEOUT_MS (3000)

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string remoteUserId;
  std::string audioFile = DEFAULT_AUDIO_FILE;

  struct {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
};

class PcmFrameObserver : public agora::media::IAudioFrameObserverBase {
 public:
  PcmFrameObserver(const std::string& outputFilePath)
      : outputFilePath_(outputFilePath) {}

  ~PcmFrameObserver() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (auto& item : files_) {
      if (item.second.file) fclose(item.second.file);
    }
  }

  bool onPlaybackAudioFrame(const char* channelId, AudioFrame& audioFrame) override {
    return true;
  };

  bool onRecordAudioFrame(const char* channelId, AudioFrame& audioFrame) override {
    return true;
  };

  bool onMixedAudioFrame(const char* channelId, AudioFrame& audioFrame) override { return true; };

  bool onPlaybackAudioFrameBeforeMixing(const char* channelId,
                                        agora::media::base::user_id_t userId,
                                        AudioFrame& audioFrame) override;

  bool onEarMonitoringAudioFrame(AudioFrame& audioFrame) override { return true; };

  AudioParams getEarMonitoringAudioParams() override { return AudioParams(); };

  int getObservedAudioFramePosition() override { return 0; };

  AudioParams getPlaybackAudioParams() override { return AudioParams(); };

  AudioParams getRecordAudioParams() override { return AudioParams(); };

  AudioParams getMixedAudioParams() override { return AudioParams(); };

 private:
  struct UserFileState {
    FILE* file = nullptr;
    int fileCount = 0;
    uint64_t fileSize = 0;
    uint64_t frameCount = 0;
    int64_t lastRenderTimeMs = 0;
    uint32_t lastRtpTimestamp = 0;
    int channels = 0;
    int sampleRate = 0;
  };

  std::string outputFilePath_;
  std::map<std::string, UserFileState> files_;
  std::mutex file_mutex_;
};

bool PcmFrameObserver::onPlaybackAudioFrameBeforeMixing(const char* channelId,
                                                        agora::media::base::user_id_t userId,
                                                        AudioFrame& audioFrame) {
  const std::string uid = userId ? userId : "unknown";
  std::lock_guard<std::mutex> lock(file_mutex_);
  UserFileState& state = files_[uid];

  // Keep one file sequence per remote UID. Use truncation so a new run cannot
  // contaminate validation with bytes left by an older run.
  if (!state.file) {
    ++state.fileCount;
    std::string fileName = makePerUserOutputPath(outputFilePath_, uid.c_str());
    if (state.fileCount > 1) fileName += "." + to_string(state.fileCount);
    if (!(state.file = fopen(fileName.c_str(), "wb"))) {
      AG_LOG(ERROR, "Failed to create received audio file %s", fileName.c_str());
      return false;
    }
    AG_LOG(INFO, "Created file %s for remote user %s", fileName.c_str(), uid.c_str());
  }

  // Write PCM samples（16bit 交错，声道数与 setPlaybackAudioFrameBeforeMixingParameters 一致）
  size_t writeBytes = audioFrame.samplesPerChannel * audioFrame.channels * sizeof(int16_t);
  if (!audioFrame.buffer || writeBytes == 0) {
    AG_LOG(WARNING, "Empty audio frame received, skip writing");
    return true;
  }
  if (fwrite(audioFrame.buffer, 1, writeBytes, state.file) != writeBytes) {
    AG_LOG(ERROR, "Error writing decoded audio data: %s", std::strerror(errno));
    return false;
  }
  state.fileSize += writeBytes;
  state.frameCount++;
  state.lastRenderTimeMs = audioFrame.renderTimeMs;
  state.lastRtpTimestamp = audioFrame.rtpTimestamp;
  state.channels = audioFrame.channels;
  state.sampleRate = audioFrame.samplesPerSec;

  // Close the file if size limit is reached
  if (state.fileSize >= DEFAULT_FILE_LIMIT) {
    fclose(state.file);
    state.file = nullptr;
    state.fileSize = 0;
  }
  return true;
}

static bool exitFlag = false;
static void SignalHandler(int sigNo) { exitFlag = true; }

int main(int argc, char* argv[]) {
  SampleOptions options;
  opt_parser optParser;

  optParser.add_long_opt("token", &options.appId, "The token for authentication");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("remoteUserId", &options.remoteUserId,
                         "The remote user to receive stream from (default: subscribe all)");
  optParser.add_long_opt("audioFile", &options.audioFile, "Output audio file");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for received audio (48000)");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for received audio, must match the sender (3~8)");

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

  if (options.audio.numOfChannels < 3 || options.audio.numOfChannels > 8) {
    AG_LOG(ERROR, "numOfChannels must be in [3, 8] and match the sender's channel count!");
    return -1;
  }
  if (options.audio.sampleRate != 48000) {
    AG_LOG(ERROR, "Multi-channel OPUSMC receive only supports a 48000 Hz sample rate!");
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

  // Create Agora connection
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_AUDIENCE;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.enableAudioRecordingOrPlayout = false;  // Subscribe audio but without playback

  agora::agora_refptr<agora::rtc::IRtcConnection> connection =
      service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

  // Subscribe audio from all remote users or a specific remote user
  if (options.remoteUserId.empty()) {
    AG_LOG(INFO, "Subscribe audio from all remote users");
    if (connection->getLocalUser()->subscribeAllAudio()) {
      AG_LOG(ERROR, "Failed to subscribe audio from all remote users!");
      return -1;
    }
  } else {
    if (connection->getLocalUser()->subscribeAudio(options.remoteUserId.c_str())) {
      AG_LOG(ERROR, "Failed to subscribe audio from user %s!", options.remoteUserId.c_str());
      return -1;
    }
  }

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  // Create local user observer
  auto localUserObserver =
      std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  // Register audio frame observer to receive audio stream.
  // 回调声道数必须与发送端一致才能拿到完整多声道（spec 场景5.1）
  auto pcmFrameObserver = std::make_shared<PcmFrameObserver>(options.audioFile);
  if (connection->getLocalUser()->setPlaybackAudioFrameBeforeMixingParameters(
          options.audio.numOfChannels, options.audio.sampleRate)) {
    AG_LOG(ERROR, "Failed to set audio frame parameters!");
    return -1;
  }
  localUserObserver->setAudioFrameObserver(pcmFrameObserver.get());

  // Connect to Agora channel
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                          options.userId.c_str())) {
    AG_LOG(ERROR, "Failed to connect to Agora channel!");
    return -1;
  }

  // Wait until connected before entering the receive loop；
  // 连接超时/失败则不进入接收循环，走下方清理后退出
  if (connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
    AG_LOG(ERROR, "Wait for connection timeout or failure!");
  } else {
    // Start receiving incoming media data
    AG_LOG(INFO, "Start receiving multi-channel audio data (channels=%d, sampleRate=%d) ...",
           options.audio.numOfChannels, options.audio.sampleRate);

    // Periodically check exit flag
    while (!exitFlag) {
      usleep(10000);
    }
  }

  // Unregister audio frame observer
  localUserObserver->unsetAudioFrameObserver();

  // Unregister connection observer
  connection->unregisterObserver(connObserver.get());

  // Disconnect from Agora channel
  if (connection->disconnect()) {
    AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
    return -1;
  }
  AG_LOG(INFO, "Disconnected from Agora channel successfully");

  // Destroy Agora connection and related resources
  localUserObserver.reset();
  pcmFrameObserver.reset();
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
