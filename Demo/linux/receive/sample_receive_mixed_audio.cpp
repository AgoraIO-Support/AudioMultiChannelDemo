//  Agora RTC/MEDIA SDK
//
//  Linux SDK 音频多声道（multiopus）demo: 接收远端多声道音频（混音输出，恒 2 声道）
//
//  对应需求规格需求点7（多声道混音接收——双声道输出）：
//   - 通过 setPlaybackAudioFrameParameters 设置混音回调参数（默认 48 kHz / 2 声道）
//   - onPlaybackAudioFrame 回调混音后的 PCM：多声道流混音输出恒为 2 声道，
//     奇数声道混合为一路、偶数声道混合为一路（spec 场景7.1）
//   - 不支持将 N 声道直接渲染为 N 声道播放；需逐声道数据请使用
//     sample_receive_pcm_multiopus（需求点5）或 sample_receive_encoded_audio（需求点6）
//   - 落盘文件追加写入，单文件达到 100 MB 自动切换新文件；
//     可用 ffplay / Audacity 按 48 kHz / 2 声道 / 16bit 导入核对混音内容
//
//  说明：payload 123/124/125 协商机制下，接收端无需设置 che.audio.custom_payload_type。
//

#include <csignal>
#include <cstring>
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
#define DEFAULT_NUM_OF_CHANNELS (2)  // 混音输出恒为 2 声道（spec 需求点7）
#define DEFAULT_AUDIO_FILE "received_mixed_audio.pcm"
#define DEFAULT_FILE_LIMIT (100 * 1024 * 1024)  // 100 MB 切换新文件
#define DEFAULT_CONNECT_TIMEOUT_MS (3000)

struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile = DEFAULT_AUDIO_FILE;

  struct {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
};

class PcmFrameObserver : public agora::media::IAudioFrameObserverBase {
 public:
  PcmFrameObserver(const std::string& outputFilePath)
      : outputFilePath_(outputFilePath),
        pcmFile_(nullptr),
        fileCount(0),
        fileSize_(0) {}

  ~PcmFrameObserver() {
    if (pcmFile_) fclose(pcmFile_);
  }

  bool onPlaybackAudioFrame(const char* channelId, AudioFrame& audioFrame) override;

  bool onRecordAudioFrame(const char* channelId, AudioFrame& audioFrame) override {
    return true;
  };

  bool onMixedAudioFrame(const char* channelId, AudioFrame& audioFrame) override { return true; };

  bool onPlaybackAudioFrameBeforeMixing(const char* channelId,
                                        agora::media::base::user_id_t userId,
                                        AudioFrame& audioFrame) override {
    return true;
  };

  bool onEarMonitoringAudioFrame(AudioFrame& audioFrame) override { return true; };

  AudioParams getEarMonitoringAudioParams() override { return AudioParams(); };

  int getObservedAudioFramePosition() override { return 0; };

  AudioParams getPlaybackAudioParams() override { return AudioParams(); };

  AudioParams getRecordAudioParams() override { return AudioParams(); };

  AudioParams getMixedAudioParams() override { return AudioParams(); };

 private:
  std::string outputFilePath_;
  FILE* pcmFile_;
  int fileCount;
  int fileSize_;
};

bool PcmFrameObserver::onPlaybackAudioFrame(const char* channelId, AudioFrame& audioFrame) {
  // Create new file to save received PCM samples (append mode)
  if (!pcmFile_) {
    std::string fileName =
        (++fileCount > 1) ? (outputFilePath_ + to_string(fileCount)) : outputFilePath_;
    if (!(pcmFile_ = fopen(fileName.c_str(), "ab"))) {
      AG_LOG(ERROR, "Failed to create received audio file %s", fileName.c_str());
      return false;
    }
    AG_LOG(INFO, "Created file %s to save received mixed PCM samples", fileName.c_str());
  }

  // Write PCM samples（混音后 PCM，16bit 交错，声道数恒为 2）
  size_t writeBytes = audioFrame.samplesPerChannel * audioFrame.channels * sizeof(int16_t);
  if (!audioFrame.buffer || writeBytes == 0) {
    AG_LOG(WARNING, "Empty audio frame received, skip writing");
    return true;
  }
  if (fwrite(audioFrame.buffer, 1, writeBytes, pcmFile_) != writeBytes) {
    AG_LOG(ERROR, "Error writing mixed audio data: %s", std::strerror(errno));
    return false;
  }
  fileSize_ += writeBytes;

  // Close the file if size limit is reached
  if (fileSize_ >= DEFAULT_FILE_LIMIT) {
    fclose(pcmFile_);
    pcmFile_ = nullptr;
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
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for mixed audio output "
                         "(8000/16000/32000/44100/48000, default 48000)");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for mixed audio output (1 or 2; mixed output "
                         "of multi-channel streams is always stereo)");

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

  // 与 SDK 头文件文档对齐：混音输出仅支持单/双声道与离散采样率（NGIAgoraLocalUser.h）
  if (options.audio.numOfChannels != 1 && options.audio.numOfChannels != 2) {
    AG_LOG(ERROR,
           "numOfChannels must be 1 or 2 (mixed output of multi-channel streams is always "
           "stereo; for per-channel data use sample_receive_pcm_multiopus)!");
    return -1;
  }
  if (options.audio.sampleRate != 8000 && options.audio.sampleRate != 16000 &&
      options.audio.sampleRate != 32000 && options.audio.sampleRate != 44100 &&
      options.audio.sampleRate != 48000) {
    AG_LOG(ERROR, "sampleRate must be one of 8000/16000/32000/44100/48000!");
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
  ccfg.enableAudioRecordingOrPlayout = false;  // Subscribe audio but without playback device

  agora::agora_refptr<agora::rtc::IRtcConnection> connection =
      service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }

  // Subscribe audio from all remote users
  AG_LOG(INFO, "Subscribe audio from all remote users");
  if (connection->getLocalUser()->subscribeAllAudio()) {
    AG_LOG(ERROR, "Failed to subscribe audio from all remote users!");
    return -1;
  }

  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());

  // Create local user observer
  auto localUserObserver =
      std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());

  // Register audio frame observer to receive the mixed audio stream.
  // 多声道流混音输出恒为 2 声道（奇数声道混一路、偶数声道混一路，spec 场景7.1）
  auto pcmFrameObserver = std::make_shared<PcmFrameObserver>(options.audioFile);
  if (connection->getLocalUser()->setPlaybackAudioFrameParameters(
          options.audio.numOfChannels, options.audio.sampleRate,
          agora::rtc::RAW_AUDIO_FRAME_OP_MODE_TYPE::RAW_AUDIO_FRAME_OP_MODE_READ_ONLY,
          options.audio.sampleRate / 100 * options.audio.numOfChannels)) {
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
    AG_LOG(INFO, "Start receiving mixed audio data (channels=%d, sampleRate=%d) ...",
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
