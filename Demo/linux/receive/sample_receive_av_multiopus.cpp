//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 接收远端多声道音频 + 视频（AV 同收）
//
//  以 sample_receive_pcm_multiopus 为骨架，增加视频接收：
//   - 音频：setPlaybackAudioFrameBeforeMixingParameters(numOfChannels, 48000) +
//     onPlaybackAudioFrameBeforeMixing 按远端用户回调解码后的多声道 PCM（16bit 交错），
//     按远端 UID 落盘 --audioFile（100MB 切换新文件），每 500 帧打一次日志
//   - 视频：registerVideoFrameObserver(IVideoFrameObserver2)，onFrame 回调解码后的
//     I420 帧，按远端 UID 落盘 --videoFile（100MB 切换新文件），每 100 帧打一次日志
//   - --remoteUserId 非空时 subscribeAudio + subscribeVideo 指定用户；
//     为空时 subscribeAllAudio + subscribeAllVideo 订阅全部远端用户
//
//  说明：payload 123/124/125 协商机制下，接收端无需设置 che.audio.custom_payload_type。
//

#include <csignal>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <cerrno>
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
#include "NGIAgoraVideoTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"

#define DEFAULT_SAMPLE_RATE (48000)
#define DEFAULT_NUM_OF_CHANNELS (8)
#define DEFAULT_AUDIO_FILE "received_av_multiopus.pcm"
#define DEFAULT_VIDEO_FILE "received_video.yuv"
#define DEFAULT_FILE_LIMIT (100 * 1024 * 1024)  // 100 MB 切换新文件
#define DEFAULT_CONNECT_TIMEOUT_MS (3000)

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string remoteUserId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  std::string videoFile = DEFAULT_VIDEO_FILE;

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

class YuvFrameObserver : public agora::rtc::IVideoFrameObserver2 {
 public:
  YuvFrameObserver(const std::string& outputFilePath)
      : outputFilePath_(outputFilePath) {}

  ~YuvFrameObserver() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (auto& item : files_) {
      if (item.second.file) fclose(item.second.file);
    }
  }

  void onFrame(const char* channelId, agora::user_id_t remoteUid,
               const agora::media::base::VideoFrame* frame) override;

 private:
  struct UserFileState {
    FILE* file = nullptr;
    int fileCount = 0;
    uint64_t fileSize = 0;
    uint64_t frameCount = 0;
    int64_t lastRenderTimeMs = 0;
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

  // 每 500 帧（10ms/帧，~5s）打一次进度日志
  if (state.frameCount % 500 == 0) {
    AG_LOG(INFO, "Received audio frames: %ld from user %s (%dch %dHz, %.1f MB written)",
           (long)state.frameCount, uid.c_str(), audioFrame.channels, audioFrame.samplesPerSec,
           (double)state.fileSize / 1024 / 1024);
  }

  // Close the file if size limit is reached
  if (state.fileSize >= DEFAULT_FILE_LIMIT) {
    fclose(state.file);
    state.file = nullptr;
    state.fileSize = 0;
  }
  return true;
}

void YuvFrameObserver::onFrame(const char* channelId, agora::user_id_t remoteUid,
                               const agora::media::base::VideoFrame* videoFrame) {
  const std::string uid = remoteUid ? remoteUid : "unknown";
  std::lock_guard<std::mutex> lock(file_mutex_);
  if (!videoFrame || !videoFrame->yBuffer || !videoFrame->uBuffer || !videoFrame->vBuffer) {
    AG_LOG(WARNING, "Empty video frame received, skip writing");
    return;
  }
  if (videoFrame->type != agora::media::base::VIDEO_PIXEL_I420) {
    // demo 只落盘 I420；其他格式（如 NV12）直接跳过并提示
    if (files_[uid].frameCount == 0) {
      AG_LOG(WARNING, "Unexpected video pixel format %d, only I420 is dumped", videoFrame->type);
    }
    return;
  }

  UserFileState& state = files_[uid];
  if (!state.file) {
    ++state.fileCount;
    std::string fileName = makePerUserOutputPath(outputFilePath_, uid.c_str());
    if (state.fileCount > 1) fileName += "." + to_string(state.fileCount);
    if (!(state.file = fopen(fileName.c_str(), "wb"))) {
      AG_LOG(ERROR, "Failed to create received video file %s", fileName.c_str());
      return;
    }
    AG_LOG(INFO, "Created file %s for remote user %s", fileName.c_str(), uid.c_str());
  }

  // 逐行写 Y/U/V 平面（stride 可能大于 width）
  const int width = videoFrame->width;
  const int height = videoFrame->height;
  for (int r = 0; r < height; r++) {
    fwrite(videoFrame->yBuffer + r * videoFrame->yStride, 1, width, state.file);
  }
  for (int r = 0; r < height / 2; r++) {
    fwrite(videoFrame->uBuffer + r * videoFrame->uStride, 1, width / 2, state.file);
  }
  for (int r = 0; r < height / 2; r++) {
    fwrite(videoFrame->vBuffer + r * videoFrame->vStride, 1, width / 2, state.file);
  }
  state.fileSize += (uint64_t)width * height * 3 / 2;
  state.frameCount++;
  state.lastRenderTimeMs = videoFrame->renderTimeMs;

  // 每 100 帧打一次进度日志
  if (state.frameCount % 100 == 0) {
    AG_LOG(INFO, "Received video frames: %ld from user %s (%dx%d, %.1f MB written)",
           (long)state.frameCount, uid.c_str(), width, height,
           (double)state.fileSize / 1024 / 1024);
  }

  // Close the file if size limit is reached
  if (state.fileSize >= DEFAULT_FILE_LIMIT) {
    fclose(state.file);
    state.file = nullptr;
    state.fileSize = 0;
  }
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
  optParser.add_long_opt("videoFile", &options.videoFile, "Output video file (I420)");
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

  // Subscribe audio & video from all remote users or a specific remote user
  agora::rtc::VideoSubscriptionOptions subscriptionOptions;  // 默认大流、解码后帧
  if (options.remoteUserId.empty()) {
    AG_LOG(INFO, "Subscribe audio & video from all remote users");
    if (connection->getLocalUser()->subscribeAllAudio()) {
      AG_LOG(ERROR, "Failed to subscribe audio from all remote users!");
      return -1;
    }
    if (connection->getLocalUser()->subscribeAllVideo(subscriptionOptions)) {
      AG_LOG(ERROR, "Failed to subscribe video from all remote users!");
      return -1;
    }
  } else {
    if (connection->getLocalUser()->subscribeAudio(options.remoteUserId.c_str())) {
      AG_LOG(ERROR, "Failed to subscribe audio from user %s!", options.remoteUserId.c_str());
      return -1;
    }
    if (connection->getLocalUser()->subscribeVideo(options.remoteUserId.c_str(),
                                                   subscriptionOptions)) {
      AG_LOG(ERROR, "Failed to subscribe video from user %s!", options.remoteUserId.c_str());
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

  // Register video frame observer to receive video stream（须在订阅回调触发前设置，
  // onUserVideoTrackSubscribed 里才会向 SDK 注册）
  auto yuvFrameObserver = std::make_shared<YuvFrameObserver>(options.videoFile);
  localUserObserver->setVideoFrameObserver(yuvFrameObserver.get());

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
    AG_LOG(INFO,
           "Start receiving multi-channel audio (channels=%d, sampleRate=%d) & video data ...",
           options.audio.numOfChannels, options.audio.sampleRate);

    // Periodically check exit flag
    while (!exitFlag) {
      usleep(10000);
    }
  }

  // Unregister audio & video frame observers
  localUserObserver->unsetAudioFrameObserver();
  localUserObserver->unsetVideoFrameObserver();

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
  yuvFrameObserver.reset();
  connection = nullptr;

  // Destroy Agora Service
  service->release();
  service = nullptr;

  return 0;
}
