#include "common/pcm_channel_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* program) {
  std::cerr
      << "Usage: " << program
      << " --source FILE --received FILE --channels N [options]\n"
      << "  --source FILE              source WAV or raw PCM\n"
      << "  --received FILE            received raw 16-bit interleaved PCM\n"
      << "  --channels N               expected source/received channels (3..8)\n"
      << "  --received-channels N      override received channel count (for mismatch checks)\n"
      << "  --source-channels N        channels when --source is raw PCM\n"
      << "  --sample-rate HZ           expected sample rate (default 48000)\n"
      << "  --max-seconds SEC          bound memory and comparison window (default 5)\n"
      << "  --source-offset-ms MS      trim source before comparison (default 0)\n"
      << "  --received-offset-ms MS    trim received before comparison (default 0)\n"
      << "  --min-correlation VALUE    signed correlation threshold (default 0.80)\n"
      << "  --help                     show this help\n";
}

bool parseInt(const std::string& text, int& value) {
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (!end || *end != '\0' || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool parseDouble(const std::string& text, double& value) {
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end && *end == '\0' && std::isfinite(value);
}

bool nextValue(int argc, char** argv, int& index, std::string& value) {
  if (index + 1 >= argc) return false;
  value = argv[++index];
  return true;
}

bool requireNonNegative(int value, const char* name) {
  if (value >= 0) return true;
  std::cerr << name << " must be non-negative\n";
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  std::string sourcePath;
  std::string receivedPath;
  int channels = 0;
  int receivedChannels = 0;
  int sourceChannels = 0;
  int sampleRate = 48000;
  int maxSeconds = 5;
  int sourceOffsetMs = 0;
  int receivedOffsetMs = 0;
  double minimumCorrelation = 0.80;

  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help") {
      printUsage(argv[0]);
      return 0;
    }
    std::string value;
    if (option == "--source" || option == "--received" || option == "--channels" ||
        option == "--received-channels" || option == "--source-channels" ||
        option == "--sample-rate" || option == "--max-seconds" ||
        option == "--source-offset-ms" || option == "--received-offset-ms" ||
        option == "--min-correlation") {
      if (!nextValue(argc, argv, i, value)) {
        std::cerr << option << " requires a value\n";
        return 2;
      }
    }
    if (option == "--source") {
      sourcePath = value;
    } else if (option == "--received") {
      receivedPath = value;
    } else if (option == "--channels") {
      if (!parseInt(value, channels)) {
        std::cerr << "invalid --channels\n";
        return 2;
      }
    } else if (option == "--received-channels") {
      if (!parseInt(value, receivedChannels)) {
        std::cerr << "invalid --received-channels\n";
        return 2;
      }
    } else if (option == "--source-channels") {
      if (!parseInt(value, sourceChannels)) {
        std::cerr << "invalid --source-channels\n";
        return 2;
      }
    } else if (option == "--sample-rate") {
      if (!parseInt(value, sampleRate)) {
        std::cerr << "invalid --sample-rate\n";
        return 2;
      }
    } else if (option == "--max-seconds") {
      if (!parseInt(value, maxSeconds) || !requireNonNegative(maxSeconds, "--max-seconds")) {
        return 2;
      }
    } else if (option == "--source-offset-ms") {
      if (!parseInt(value, sourceOffsetMs) ||
          !requireNonNegative(sourceOffsetMs, "--source-offset-ms")) {
        return 2;
      }
    } else if (option == "--received-offset-ms") {
      if (!parseInt(value, receivedOffsetMs) ||
          !requireNonNegative(receivedOffsetMs, "--received-offset-ms")) {
        return 2;
      }
    } else if (option == "--min-correlation") {
      if (!parseDouble(value, minimumCorrelation) || minimumCorrelation < -1.0 ||
          minimumCorrelation > 1.0) {
        std::cerr << "--min-correlation must be in [-1, 1]\n";
        return 2;
      }
    } else {
      std::cerr << "unknown option: " << option << "\n";
      printUsage(argv[0]);
      return 2;
    }
  }

  if (sourcePath.empty() || receivedPath.empty() || channels < 3 || channels > 8) {
    printUsage(argv[0]);
    return 2;
  }
  if (receivedChannels == 0) receivedChannels = channels;
  if (sourceChannels == 0) sourceChannels = channels;
  if (sampleRate <= 0) {
    std::cerr << "--sample-rate must be positive\n";
    return 2;
  }
  const size_t comparisonSamples = maxSeconds == 0
                                       ? 0
                                       : static_cast<size_t>(sampleRate) * maxSeconds;
  const size_t sourceOffsetSamples =
      static_cast<size_t>(sourceOffsetMs) * static_cast<size_t>(sampleRate) / 1000;
  const size_t receivedOffsetSamples =
      static_cast<size_t>(receivedOffsetMs) * static_cast<size_t>(sampleRate) / 1000;
  const size_t largestOffset = std::max(sourceOffsetSamples, receivedOffsetSamples);
  const size_t loadSamples = comparisonSamples == 0 ? 0 : comparisonSamples + largestOffset;
  std::vector<int16_t> source;
  std::vector<int16_t> received;
  int sourceFileChannels = 0;
  int sourceFileRate = 0;
  int receivedFileChannels = 0;
  int receivedFileRate = 0;
  std::string error;
  if (!agora_demo::loadPcmFile(sourcePath, sourceChannels, sampleRate, loadSamples, source,
                               sourceFileChannels, sourceFileRate, &error)) {
    std::cerr << "source: " << error << "\n";
    return 1;
  }
  if (sourceFileChannels != channels) {
    std::cerr << "source: expected " << channels << " channels, got " << sourceFileChannels
              << "\n";
    return 1;
  }
  if (!agora_demo::loadPcmFile(receivedPath, receivedChannels, sampleRate, loadSamples, received,
                               receivedFileChannels, receivedFileRate, &error)) {
    std::cerr << "received: " << error << "\n";
    return 1;
  }
  if (receivedFileChannels != receivedChannels) {
    std::cerr << "received: expected " << receivedChannels << " channels, got "
              << receivedFileChannels << "\n";
    return 1;
  }
  agora_demo::PcmChannelAnalysisOptions options;
  options.maxSamplesPerChannel = comparisonSamples;
  options.sourceOffsetSamples = sourceOffsetSamples;
  options.receivedOffsetSamples = receivedOffsetSamples;
  options.minimumCorrelation = minimumCorrelation;
  agora_demo::PcmChannelAnalysis analysis;
  if (!agora_demo::analyzePcmChannels(source, channels, received, receivedFileChannels, options,
                                      analysis, &error)) {
    std::cerr << "analysis: " << error << "\n";
    return 1;
  }

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "source_channels=" << analysis.sourceChannels
            << " received_channels=" << analysis.receivedChannels
            << " compared_samples_per_channel=" << analysis.comparedSamplesPerChannel << "\n";
  for (size_t channel = 0; channel < analysis.sourceStats.size(); ++channel) {
    std::cout << "source_channel=" << channel << " rms=" << analysis.sourceStats[channel].rms
              << " peak=" << analysis.sourceStats[channel].peak << "\n";
  }
  for (size_t channel = 0; channel < analysis.receivedStats.size(); ++channel) {
    std::cout << "received_channel=" << channel << " rms=" << analysis.receivedStats[channel].rms
              << " peak=" << analysis.receivedStats[channel].peak << "\n";
  }
  for (const auto& match : analysis.matches) {
    std::cout << "mapping source=" << match.sourceChannel
              << " received=" << match.receivedChannel << " correlation=" << match.correlation
              << " rms_ratio=" << match.receivedRmsRatio << "\n";
  }
  if (!analysis.passed) {
    std::cout << "result=FAIL reason=" << analysis.failureReason << "\n";
    return 1;
  }
  std::cout << "result=PASS\n";
  return 0;
}
