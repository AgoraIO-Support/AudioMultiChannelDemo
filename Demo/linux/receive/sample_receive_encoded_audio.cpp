//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 接收远端多声道音频（编码帧回调）
//
//  对应需求规格需求点6（接收多声道音频——编码帧回调）：
//   - 连接配置 audioRecvEncodedFrame=true 且 enableAudioRecordingOrPlayout=false
//   - 观众角色 subscribeAllAudio（或 subscribeAudio 指定流）
//   - IAudioEncodedFrameReceiver::onEncodedAudioFrameReceived 回调 OPUSMC 编码帧，
//     info 仅携带 codec 与 sendTs（v4.4.30.149 不提供声道数/采样率，业务侧不得依赖）
//   - 该路径不解码、不播放，与播放路径互斥；业务侧可自行解码或转发
//   - 落盘文件追加写入，单文件达到 100 MB 自动切换新文件
//

#include <csignal>
#include <cstring>
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
};

class EncodedAudioReceiver : public agora::rtc::IAudioEncodedFrameReceiver {
 public:
  EncodedAudioReceiver(const std::string& outputFilePath)
      : outputFilePath_(outputFilePath), file_(nullptr), fileCount(0), fileSize_(0),
        frameCount_(0) {}

  ~EncodedAudioReceiver() {
    if (file_) fclose(file_);
  }

  bool onEncodedAudioFrameReceived(
      const uint8_t* packet, size_t length,
      const agora::media::base::AudioEncodedFrameInfo& info) override;

 private:
  std::string outputFilePath_;
  FILE* file_;
  int fileCount;
  int fileSize_;
  int frameCount_;  // 回调约 20ms/帧，用于日志节流
};

bool EncodedAudioReceiver::onEncodedAudioFrameReceived(
    const uint8_t* packet, size_t length, const agora::media::base::AudioEncodedFrameInfo& info) {
  if (!file_) {
    std::string fileName =
        (++fileCount > 1) ? (outputFilePath_ + to_string(fileCount)) : outputFilePath_;
    if (!(file_ = fopen(fileName.c_str(), "ab"))) {
      AG_LOG(ERROR, "Failed to create received audio file %s", fileName.c_str());
      return false;
    }
    AG_LOG(INFO, "Created file %s to save received encoded audio frames", fileName.c_str());
  }

  // 回调约 20ms/帧，逐帧打日志会刷屏：保留首帧，之后每 250 帧（约 5s）打一次
  if (frameCount_++ % 250 == 0) {
    AG_LOG(INFO, "Received encoded audio frame: length=%zu codec=%d sendTs=%llu (frame #%d)",
           length, info.codec, (unsigned long long)info.sendTs, frameCount_);
  }

  if (fwrite(packet, 1, length, file_) != length) {
    AG_LOG(ERROR, "Error writing encoded audio data: %s", std::strerror(errno));
    return false;
  }
  fileSize_ += length;
  if (fileSize_ >= DEFAULT_FILE_LIMIT) {
    fclose(file_);
    file_ = nullptr;
    fileSize_ = 0;
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

  auto audioReceiver = std::make_shared<EncodedAudioReceiver>(options.audioFile);
  localUserObserver->setEncodedAudioFrameObserver(audioReceiver.get());

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
  audioReceiver.reset();
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
