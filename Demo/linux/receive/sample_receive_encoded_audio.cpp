//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 接收远端多声道音频（编码帧回调）
//
//  对应需求规格需求点6（接收多声道音频——编码帧回调）：
//   - 连接配置 audioRecvEncodedFrame=true 且 enableAudioRecordingOrPlayout=false
//   - 观众角色 subscribeAllAudio（或 subscribeAudio 指定流）
//   - IAudioEncodedFrameReceiver::onEncodedAudioFrameReceived 回调 OPUSMC 编码帧，
//     info 仅携带 codec 与 sendTs（v4.4.30.152 不提供声道数/采样率，业务侧不得依赖）
//   - 该路径不解码、不播放，与播放路径互斥；业务侧可自行解码或转发
//   - 按远端 UID 分文件落盘，单文件达到 100 MB 自动切换新文件
//

#include <csignal>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
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

#define DEFAULT_AUDIO_FILE "received_audio_multiopus.opus"
#define DEFAULT_FILE_LIMIT (100 * 1024 * 1024)  // 100 MB 切换新文件
#define DEFAULT_CONNECT_TIMEOUT_MS (3000)

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  bool strictTimestampContinuity = false;
};

class EncodedAudioReceiver : public agora::rtc::IAudioEncodedFrameReceiver {
 public:
  EncodedAudioReceiver(const std::string& outputFilePath, bool strictTimestampContinuity)
      : outputFilePath_(outputFilePath),
        strictTimestampContinuity_(strictTimestampContinuity) {}

  ~EncodedAudioReceiver() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (auto& item : files_) {
      if (item.second.file) fclose(item.second.file);
    }
  }

  bool hasTimestampIssue() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (const auto& item : files_) {
      if (item.second.timestampIssue) return true;
    }
    return false;
  }

  bool onEncodedAudioFrameReceived(
      const uint8_t* packet, size_t length,
      const agora::media::base::AudioEncodedFrameInfo& info) override;

  bool onEncodedAudioFrameReceivedForUser(
      const std::string& userId, const uint8_t* packet, size_t length,
      const agora::media::base::AudioEncodedFrameInfo& info);

 private:
  struct UserFileState {
    FILE* file = nullptr;
    int fileCount = 0;
    uint64_t fileSize = 0;
    uint64_t frameCount = 0;
    uint64_t lastSendTs = 0;
    bool timestampIssue = false;
    uint8_t codec = 0;
    size_t lastFrameLength = 0;
  };

  std::string outputFilePath_;
  bool strictTimestampContinuity_ = false;
  std::map<std::string, UserFileState> files_;
  std::mutex file_mutex_;
};

bool EncodedAudioReceiver::onEncodedAudioFrameReceived(
    const uint8_t* packet, size_t length, const agora::media::base::AudioEncodedFrameInfo& info) {
  return onEncodedAudioFrameReceivedForUser("unknown", packet, length, info);
}

bool EncodedAudioReceiver::onEncodedAudioFrameReceivedForUser(
    const std::string& userId, const uint8_t* packet, size_t length,
    const agora::media::base::AudioEncodedFrameInfo& info) {
  std::lock_guard<std::mutex> lock(file_mutex_);
  UserFileState& state = files_[userId];
  if (!state.file) {
    ++state.fileCount;
    std::string fileName = makePerUserOutputPath(outputFilePath_, userId.c_str());
    if (state.fileCount > 1) fileName += "." + to_string(state.fileCount);
    if (!(state.file = fopen(fileName.c_str(), "wb"))) {
      AG_LOG(ERROR, "Failed to create received audio file %s", fileName.c_str());
      return false;
    }
    AG_LOG(INFO, "Created file %s for remote user %s", fileName.c_str(), userId.c_str());
  }

  const bool hasPreviousTimestamp = state.frameCount != 0;
  const uint64_t timestampDelta = hasPreviousTimestamp && info.sendTs >= state.lastSendTs
                                      ? info.sendTs - state.lastSendTs
                                      : 0;
  // The first callback after subscription may cover packets accumulated while
  // the remote track was being subscribed (for example, a 2880-tick gap).
  const bool afterSubscriptionWarmup = state.frameCount > 1;
  if (hasPreviousTimestamp && info.sendTs <= state.lastSendTs) {
    state.timestampIssue = true;
    AG_LOG(ERROR,
           "ENCODED_TIMESTAMP_ORDER user=%s previous=%llu current=%llu delta=%llu",
           userId.c_str(), static_cast<unsigned long long>(state.lastSendTs),
           static_cast<unsigned long long>(info.sendTs),
           static_cast<unsigned long long>(timestampDelta));
  } else if (hasPreviousTimestamp && afterSubscriptionWarmup && timestampDelta > 1920) {
    state.timestampIssue = state.timestampIssue || strictTimestampContinuity_;
    if (strictTimestampContinuity_) {
      AG_LOG(ERROR,
             "ENCODED_TIMESTAMP_GAP user=%s previous=%llu current=%llu delta=%llu",
             userId.c_str(), static_cast<unsigned long long>(state.lastSendTs),
             static_cast<unsigned long long>(info.sendTs),
             static_cast<unsigned long long>(timestampDelta));
    } else {
      AG_LOG(WARNING,
             "ENCODED_TIMESTAMP_GAP user=%s previous=%llu current=%llu delta=%llu",
             userId.c_str(), static_cast<unsigned long long>(state.lastSendTs),
             static_cast<unsigned long long>(info.sendTs),
             static_cast<unsigned long long>(timestampDelta));
    }
  }
  ++state.frameCount;

  // Keep the first five frames for interval assertions, then sample every
  // 250 frames to avoid flooding the application log.
  if (state.frameCount <= 5 || state.frameCount % 250 == 0) {
    AG_LOG(INFO,
           "Received encoded audio frame: user=%s length=%zu codec=%d sendTs=%llu delta=%llu "
           "(frame #%llu)",
           userId.c_str(), length, info.codec, (unsigned long long)info.sendTs,
           static_cast<unsigned long long>(timestampDelta),
           (unsigned long long)state.frameCount);
  }

  if (fwrite(packet, 1, length, state.file) != length) {
    AG_LOG(ERROR, "Error writing encoded audio data: %s", std::strerror(errno));
    return false;
  }
  state.fileSize += length;
  state.lastSendTs = info.sendTs;
  state.codec = info.codec;
  state.lastFrameLength = length;
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
  optParser.add_long_opt("audioFile", &options.audioFile, "Output audio file");
  optParser.add_long_opt("strictTimestampContinuity", &options.strictTimestampContinuity,
                         "Pass 1 to fail on encoded timestamp gaps after subscription warmup; "
                         "default monitors gaps as warnings");

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
  AG_LOG(INFO, "Encoded timestamp continuity mode: %s",
         options.strictTimestampContinuity ? "strict" : "monitor");

  std::signal(SIGQUIT, SignalHandler);
  std::signal(SIGABRT, SignalHandler);
  std::signal(SIGINT, SignalHandler);

  // Create Agora service
  auto service = createAndInitAgoraService(false, true, true);
  if (!service) {
    AG_LOG(ERROR, "Failed to creating Agora service!");
    return -1;
  }

  // 编码帧接收路径：audioRecvEncodedFrame=true 且 enableAudioRecordingOrPlayout=false
  // （不解码、不播放，与播放路径互斥，spec 场景6.1）
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_AUDIENCE;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.enableAudioRecordingOrPlayout = false;
  ccfg.audioRecvEncodedFrame = true;

  agora::agora_refptr<agora::rtc::IRtcConnection> connection =
      service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

  if (connection->getLocalUser()->subscribeAllAudio()) {
    AG_LOG(ERROR, "Failed to subscribe audio from all remote users!");
    return -1;
  }

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  auto localUserObserver =
      std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  auto audioReceiver =
      std::make_shared<EncodedAudioReceiver>(options.audioFile, options.strictTimestampContinuity);
  localUserObserver->setEncodedAudioFrameCallback(
      [audioReceiver](const std::string& userId, const uint8_t* packet, size_t length,
                      const agora::media::base::AudioEncodedFrameInfo& info) {
        return audioReceiver->onEncodedAudioFrameReceivedForUser(userId, packet, length, info);
      });

  // Register the observer before connecting so the first subscribed frames are not missed.
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
    AG_LOG(INFO, "Start receiving encoded audio frames ...");

    // Periodically check exit flag
    while (!exitFlag) {
      usleep(10000);
    }
  }

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
  const bool timestampIssue = audioReceiver->hasTimestampIssue();
  audioReceiver.reset();
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return timestampIssue ? 1 : 0;
}
