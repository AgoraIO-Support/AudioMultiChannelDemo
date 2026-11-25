#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "AgoraRefCountedObject.h"
#include "IAgoraService.h"
#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNode.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraRtcConnection.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"

namespace {

struct Options {
  std::string appId;
  std::string channelId;
  std::string userId = "86002";
  std::string remoteUserId = "86001";
  int iterations = 1;
  int numOfChannels = 4;
  int connectTimeoutMs = 3000;
  int holdMs = 0;
  int releaseDelayUs = 0;
  int noConnect = 0;
  int expectedBuild = 0;
};

class NoopAudioFrameObserver final : public agora::media::IAudioFrameObserverBase {
 public:
  bool onPlaybackAudioFrame(const char*, AudioFrame&) override { return true; }
  bool onRecordAudioFrame(const char*, AudioFrame&) override { return true; }
  bool onMixedAudioFrame(const char*, AudioFrame&) override { return true; }
  bool onPlaybackAudioFrameBeforeMixing(const char*, agora::media::base::user_id_t,
                                        AudioFrame&) override { return true; }
  bool onEarMonitoringAudioFrame(AudioFrame&) override { return true; }
  AudioParams getEarMonitoringAudioParams() override { return AudioParams(); }
  int getObservedAudioFramePosition() override { return 0; }
  AudioParams getPlaybackAudioParams() override { return AudioParams(); }
  AudioParams getRecordAudioParams() override { return AudioParams(); }
  AudioParams getMixedAudioParams() override { return AudioParams(); }
};

static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "signal control requires lock-free atomic bool");
std::atomic<bool> exitFlag(false);
void SignalHandler(int) { exitFlag.store(true, std::memory_order_relaxed); }

void trace(const std::string& message) {
  std::cout << message << std::endl;
}

int runConnection(agora::base::IAgoraService* service, const Options& options, int iteration) {
  const std::string channel = options.channelId + "-" + std::to_string(iteration);
  trace("RACE_CASE_BEGIN iteration=" + std::to_string(iteration) + " channel=" + channel);

  agora::rtc::RtcConnectionConfiguration config;
  config.clientRoleType = agora::rtc::CLIENT_ROLE_AUDIENCE;
  config.autoSubscribeAudio = false;
  config.autoSubscribeVideo = false;
  config.enableAudioRecordingOrPlayout = false;

  agora::agora_refptr<agora::rtc::IRtcConnection> connection =
      service->createRtcConnection(config);
  if (!connection) {
    trace("RACE_CASE_ERROR stage=create_connection");
    return 1;
  }

  auto connectionObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connectionObserver.get());
  auto localUserObserver =
      std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());
  NoopAudioFrameObserver audioFrameObserver;
  int result = 0;

  if (connection->getLocalUser()->subscribeAudio(options.remoteUserId.c_str()) != 0) {
    trace("RACE_CASE_ERROR stage=subscribe");
    result = 1;
  } else if (connection->getLocalUser()->setPlaybackAudioFrameBeforeMixingParameters(
                 options.numOfChannels, 48000) != 0) {
    trace("RACE_CASE_ERROR stage=set_playback_parameters");
    result = 1;
  }
  localUserObserver->setAudioFrameObserver(&audioFrameObserver);

  bool connected = false;
  if (result == 0 && !options.noConnect) {
    if (connection->connect(options.appId.c_str(), channel.c_str(), options.userId.c_str()) != 0) {
      trace("RACE_CASE_ERROR stage=connect");
      result = 1;
    } else if (connectionObserver->waitUntilConnected(options.connectTimeoutMs) == 0) {
      connected = true;
      trace("RACE_CASE_CONNECTED iteration=" + std::to_string(iteration));
    } else {
      trace("RACE_CASE_CONNECT_TIMEOUT iteration=" + std::to_string(iteration));
      result = 1;
    }
  }

  if (options.holdMs > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(options.holdMs));
  }

  localUserObserver->unsetAudioFrameObserver();
  if (!options.noConnect) {
    if (connection->disconnect() != 0) {
      trace("RACE_CASE_ERROR stage=disconnect");
      result = 1;
    } else if (connectionObserver->waitUntilDisconnected(options.connectTimeoutMs) == 0) {
      trace("RACE_CASE_DISCONNECTED iteration=" + std::to_string(iteration));
    } else {
      trace("RACE_CASE_DISCONNECT_TIMEOUT iteration=" + std::to_string(iteration));
      result = 1;
    }
  }

  connection->unregisterObserver(connectionObserver.get());
  localUserObserver.reset();
  if (options.releaseDelayUs > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(options.releaseDelayUs));
  }
  connection = nullptr;
  trace("RACE_CASE_DESTROYED iteration=" + std::to_string(iteration) +
        " connected=" + (connected ? "1" : "0"));
  return result;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options options;
  opt_parser parser;
  parser.add_long_opt("token", &options.appId, "The token for authentication");
  parser.add_long_opt("channelId", &options.channelId, "Channel prefix; iteration is appended");
  parser.add_long_opt("userId", &options.userId, "Receiver UID");
  parser.add_long_opt("remoteUserId", &options.remoteUserId, "Subscription target UID");
  parser.add_long_opt("iterations", &options.iterations, "Connections per process");
  parser.add_long_opt("numOfChannels", &options.numOfChannels, "Playback channel count");
  parser.add_long_opt("connectTimeoutMs", &options.connectTimeoutMs,
                      "Connect and disconnect timeout in milliseconds");
  parser.add_long_opt("holdMs", &options.holdMs, "Hold time before disconnect");
  parser.add_long_opt("releaseDelayUs", &options.releaseDelayUs,
                      "Delay before connection destruction in microseconds");
  parser.add_long_opt("noConnect", &options.noConnect,
                      "Set to 1 to test creation/destruction without RTC join");
  parser.add_long_opt("expectedBuild", &options.expectedBuild,
                      "Required SDK build; zero disables the check");

  if (argc <= 1 || !parser.parse_opts(argc, argv)) {
    parser.print_usage(argv[0], std::cout);
    return 2;
  }
  if (options.appId.empty() || options.channelId.empty() || options.iterations <= 0 ||
      options.numOfChannels < 3 || options.numOfChannels > 8 ||
      options.connectTimeoutMs <= 0 || options.holdMs < 0 || options.releaseDelayUs < 0) {
    std::cerr << "Invalid teardown-race options" << std::endl;
    return 2;
  }

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  int32_t build = 0;
  getAgoraSdkVersion(&build);
  trace("RACE_SDK_BUILD " + std::to_string(build));
  if (options.expectedBuild && build != options.expectedBuild) {
    std::cerr << "Unexpected SDK build" << std::endl;
    return 4;
  }

  auto service = createAndInitAgoraService(false, true, true);
  if (!service) {
    std::cerr << "Failed to create Agora service" << std::endl;
    return 3;
  }

  int completed = 0;
  int failures = 0;
  for (int iteration = 1; iteration <= options.iterations; ++iteration) {
    if (exitFlag.load(std::memory_order_relaxed)) break;
    const int result = runConnection(service, options, iteration);
    ++completed;
    if (result != 0) ++failures;
  }

  trace("RACE_SERVICE_RELEASE");
  service->release();
  service = nullptr;
  trace("RACE_SERVICE_RELEASED");
  trace("RACE_SUMMARY completed=" + std::to_string(completed) +
        " failures=" + std::to_string(failures));
  return failures == 0 ? 0 : 1;
}
