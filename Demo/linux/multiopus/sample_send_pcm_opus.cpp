// Ordinary Opus PCM sender used by the fallback regression. It deliberately
// does not set multi-channel private parameters or codec_unfallback, so it is
// a real non-OPUSMC peer for negotiation tests.

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"
#include "common/log.h"
#include "common/helper.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"

namespace {

constexpr int kConnectTimeoutMs = 3000;

struct Options {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile;
  int sampleRate = 48000;
  int channels = 2;
};

std::atomic<bool> exitFlag(false);
void signalHandler(int /*signal*/) { exitFlag.store(true, std::memory_order_relaxed); }

void sendAudio(const Options& options,
               agora::agora_refptr<agora::rtc::IAudioPcmDataSender> sender) {
  static FILE* file = nullptr;
  const int samplesPerFrame = options.sampleRate / 100;
  const size_t frameBytes = static_cast<size_t>(samplesPerFrame) * options.channels *
                            sizeof(int16_t);
  std::vector<uint8_t> frame(frameBytes, 0);
  if (!file) {
    file = std::fopen(options.audioFile.c_str(), "rb");
    if (!file) {
      AG_LOG(ERROR, "Failed to open ordinary Opus PCM input %s: %s", options.audioFile.c_str(),
             std::strerror(errno));
      exitFlag.store(true, std::memory_order_relaxed);
      return;
    }
    AG_LOG(INFO, "Opened ordinary Opus PCM input %s (%dch %dHz)", options.audioFile.c_str(),
           options.channels, options.sampleRate);
  }
  size_t bytesRead = std::fread(frame.data(), 1, frame.size(), file);
  if (bytesRead != frame.size()) {
    if (std::feof(file)) {
      std::clearerr(file);
      std::rewind(file);
      bytesRead = std::fread(frame.data(), 1, frame.size(), file);
    }
    if (bytesRead != frame.size()) {
      AG_LOG(ERROR, "Ordinary Opus PCM input does not contain a complete frame");
      exitFlag.store(true, std::memory_order_relaxed);
      return;
    }
  }
  if (sender->sendAudioPcmData(frame.data(), 0, 0, samplesPerFrame,
                               agora::rtc::TWO_BYTES_PER_SAMPLE, options.channels,
                               options.sampleRate) < 0) {
    AG_LOG(ERROR, "Failed to send ordinary Opus PCM frame");
    exitFlag.store(true, std::memory_order_relaxed);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  Options options;
  opt_parser parser;
  parser.add_long_opt("token", &options.appId, "App ID or token");
  parser.add_long_opt("channelId", &options.channelId, "Channel ID");
  parser.add_long_opt("userId", &options.userId, "User ID");
  parser.add_long_opt("audioFile", &options.audioFile, "Interleaved signed 16-bit PCM");
  parser.add_long_opt("sampleRate", &options.sampleRate, "PCM sample rate");
  parser.add_long_opt("numOfChannels", &options.channels, "Ordinary Opus channel count (1 or 2)");
  if (argc <= 1 || !parser.parse_opts(argc, argv)) {
    std::ostringstream usage;
    parser.print_usage(argv[0], usage);
    std::cout << usage.str() << std::endl;
    return -1;
  }
  if (options.appId.empty() || options.channelId.empty() || options.audioFile.empty()) {
    AG_LOG(ERROR, "token, channelId and audioFile are required");
    return -1;
  }
  if (options.channels < 1 || options.channels > 2 || options.sampleRate <= 0 ||
      options.sampleRate % 100 != 0) {
    AG_LOG(ERROR, "ordinary Opus supports 1/2 channels and a sample rate divisible by 100");
    return -1;
  }

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
  std::signal(SIGQUIT, signalHandler);

  auto service = createAndInitAgoraService(false, true, true);
  if (!service) return -1;
  agora::rtc::RtcConnectionConfiguration config;
  config.autoSubscribeAudio = false;
  config.autoSubscribeVideo = false;
  config.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
  auto connection = service->createRtcConnection(config);
  if (!connection) {
    service->release();
    return -1;
  }
  auto observer = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(observer.get());
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(), options.userId.c_str())) {
    AG_LOG(ERROR, "Failed to connect ordinary Opus peer");
    connection->unregisterObserver(observer.get());
    connection = nullptr;
    service->release();
    return -1;
  }
  auto factory = service->createMediaNodeFactory();
  auto sender = factory ? factory->createAudioPcmDataSender() : nullptr;
  auto track = sender ? service->createCustomAudioTrack(sender) : nullptr;
  if (!sender || !track || connection->getLocalUser()->publishAudio(track)) {
    AG_LOG(ERROR, "Failed to create or publish ordinary Opus track");
    connection->disconnect();
    connection->unregisterObserver(observer.get());
    connection = nullptr;
    service->release();
    return -1;
  }
  if (observer->waitUntilConnected(kConnectTimeoutMs) != 0) {
    AG_LOG(ERROR, "Wait for ordinary Opus connection timeout or failure!");
    connection->getLocalUser()->unpublishAudio(track);
    connection->disconnect();
    connection->unregisterObserver(observer.get());
    connection = nullptr;
    service->release();
    return -1;
  }
  AG_LOG(INFO, "Start sending ordinary Opus audio data ...");
  PacerInfo pacer = {0, 10, 0, std::chrono::steady_clock::now()};
  while (!exitFlag.load(std::memory_order_relaxed)) {
    sendAudio(options, sender);
    waitBeforeNextSend(pacer);
  }
  connection->getLocalUser()->unpublishAudio(track);
  connection->unregisterObserver(observer.get());
  connection->disconnect();
  sender = nullptr;
  track = nullptr;
  factory = nullptr;
  connection = nullptr;
  service->release();
  return 0;
}
