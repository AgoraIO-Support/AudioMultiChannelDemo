#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agora_demo {

struct PcmChannelStats {
  double rms = 0.0;
  int peak = 0;
};

struct PcmChannelMatch {
  size_t sourceChannel = 0;
  size_t receivedChannel = 0;
  double correlation = 0.0;
  double receivedRmsRatio = 0.0;
};

struct PcmChannelAnalysisOptions {
  // Use only this many samples per channel. Zero uses the complete overlap.
  size_t maxSamplesPerChannel = 0;
  // The two files can be trimmed independently before comparison. This is
  // useful when the receiver starts after the sender.
  size_t sourceOffsetSamples = 0;
  size_t receivedOffsetSamples = 0;
  // A source-to-receiver match must have at least this signed correlation.
  double minimumCorrelation = 0.80;
  // An unassigned source channel with this much absolute correlation to a
  // received channel indicates likely crosstalk. Channel ID fixtures are
  // intentionally distinct, so the default catches a clear bleed while
  // allowing small codec correlation noise.
  double crosstalkCorrelation = 0.25;
  // A received channel below this fraction of its source RMS is considered
  // lost. This is deliberately permissive for Opus coding gain changes.
  double minimumRmsRatio = 0.05;
};

struct PcmChannelAnalysis {
  int sourceChannels = 0;
  int receivedChannels = 0;
  size_t comparedSamplesPerChannel = 0;
  std::vector<PcmChannelStats> sourceStats;
  std::vector<PcmChannelStats> receivedStats;
  std::vector<PcmChannelMatch> matches;
  bool channelCountMatches = false;
  bool mappingMatches = false;
  bool crosstalkDetected = false;
  bool passed = false;
  std::string failureReason;
};

// Analyze interleaved signed little-endian 16-bit samples. The source and
// received buffers may have different lengths; only their overlapping region
// after the configured offsets is compared.
bool analyzePcmChannels(const std::vector<int16_t>& source, int sourceChannels,
                        const std::vector<int16_t>& received, int receivedChannels,
                        const PcmChannelAnalysisOptions& options,
                        PcmChannelAnalysis& analysis, std::string* error);

// Load a WAV (16/24-bit integer PCM) or raw 16-bit interleaved PCM file. For a
// WAV path, channels and sample rate are read from the header. For raw PCM,
// rawChannels must be provided. maxSamplesPerChannel bounds memory use.
bool loadPcmFile(const std::string& path, int rawChannels, int expectedSampleRate,
                 size_t maxSamplesPerChannel, std::vector<int16_t>& samples, int& channels,
                 int& sampleRate, std::string* error);

}  // namespace agora_demo
