#include "pcm_channel_analyzer.h"

#include "multichannel_input_validation.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <sys/stat.h>

namespace agora_demo {
namespace {

bool fail(std::string* error, const std::string& message) {
  if (error) *error = message;
  return false;
}

bool validChannels(int channels) { return channels >= 3 && channels <= 8; }

int16_t readS16(const uint8_t* bytes) {
  const uint16_t value = static_cast<uint16_t>(bytes[0]) |
                         (static_cast<uint16_t>(bytes[1]) << 8);
  return static_cast<int16_t>(value);
}

bool isRiffFile(const std::string& path, bool& isRiff, std::string* error) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    return fail(error, "cannot open input file " + path + ": " + std::strerror(errno));
  }
  char id[4] = {};
  const bool ok = std::fread(id, 1, sizeof(id), file) == sizeof(id);
  std::fclose(file);
  if (!ok) return fail(error, "input file is shorter than four bytes: " + path);
  isRiff = std::memcmp(id, "RIFF", sizeof(id)) == 0;
  return true;
}

bool getRegularFileSize(FILE* file, uint64_t& size) {
  struct stat info;
  if (fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
    return false;
  }
  size = static_cast<uint64_t>(info.st_size);
  return true;
}

bool readPcmBytes(FILE* file, uint64_t bytes, int bytesPerSample, int channels,
                  size_t maxSamplesPerChannel, std::vector<int16_t>& samples,
                  std::string* error) {
  const uint64_t frameBytes = static_cast<uint64_t>(bytesPerSample) * channels;
  if (frameBytes == 0 || bytes % frameBytes != 0) {
    return fail(error, "PCM data does not contain complete interleaved frames");
  }
  uint64_t frames = bytes / frameBytes;
  if (maxSamplesPerChannel != 0) {
    frames = std::min<uint64_t>(frames, maxSamplesPerChannel);
  }
  if (frames > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) / channels) {
    return fail(error, "PCM input is too large");
  }
  const size_t sampleCount = static_cast<size_t>(frames) * channels;
  samples.assign(sampleCount, 0);
  const size_t readBytes = static_cast<size_t>(frames * frameBytes);
  std::vector<uint8_t> bytesBuffer(readBytes);
  if (readBytes != 0 && std::fread(bytesBuffer.data(), 1, readBytes, file) != readBytes) {
    return fail(error, "failed to read PCM data");
  }
  for (size_t i = 0; i < sampleCount; ++i) {
    const uint8_t* source = bytesBuffer.data() + i * bytesPerSample;
    samples[i] = bytesPerSample == 2 ? readS16(source) : decodePcm24LeToS16(source);
  }
  return true;
}

PcmChannelStats statsForChannel(const std::vector<int16_t>& samples, int channels,
                                size_t firstFrame, size_t frames, int channel) {
  PcmChannelStats stats;
  if (frames == 0) return stats;
  long double sumSquares = 0.0;
  for (size_t frame = 0; frame < frames; ++frame) {
    const int value = samples[(firstFrame + frame) * channels + channel];
    stats.peak = std::max(stats.peak, std::abs(value));
    sumSquares += static_cast<long double>(value) * value;
  }
  stats.rms = std::sqrt(static_cast<double>(sumSquares / frames));
  return stats;
}

double correlationAtOffsets(const std::vector<int16_t>& source, int sourceChannels,
                            int sourceChannel, size_t sourceOffset,
                            const std::vector<int16_t>& received, int receivedChannels,
                            int receivedChannel, size_t receivedOffset, size_t frames) {
  if (frames == 0) return 0.0;
  long double sourceMean = 0.0;
  long double receivedMean = 0.0;
  for (size_t frame = 0; frame < frames; ++frame) {
    sourceMean += source[(sourceOffset + frame) * sourceChannels + sourceChannel];
    receivedMean += received[(receivedOffset + frame) * receivedChannels + receivedChannel];
  }
  sourceMean /= frames;
  receivedMean /= frames;
  long double covariance = 0.0;
  long double sourceVariance = 0.0;
  long double receivedVariance = 0.0;
  for (size_t frame = 0; frame < frames; ++frame) {
    const long double sourceDelta =
        source[(sourceOffset + frame) * sourceChannels + sourceChannel] - sourceMean;
    const long double receivedDelta =
        received[(receivedOffset + frame) * receivedChannels + receivedChannel] - receivedMean;
    covariance += sourceDelta * receivedDelta;
    sourceVariance += sourceDelta * sourceDelta;
    receivedVariance += receivedDelta * receivedDelta;
  }
  if (sourceVariance <= 0.0 || receivedVariance <= 0.0) return 0.0;
  return static_cast<double>(covariance / std::sqrt(sourceVariance * receivedVariance));
}

struct Assignment {
  double score = -std::numeric_limits<double>::infinity();
  std::vector<size_t> receivedForSource;
};

void findAssignment(const std::vector<std::vector<double>>& correlations, size_t sourceChannel,
                    std::vector<bool>& used, std::vector<size_t>& current, double score,
                    Assignment& best) {
  if (sourceChannel == correlations.size()) {
    if (score > best.score) {
      best.score = score;
      best.receivedForSource = current;
    }
    return;
  }
  for (size_t receivedChannel = 0; receivedChannel < used.size(); ++receivedChannel) {
    if (used[receivedChannel]) continue;
    used[receivedChannel] = true;
    current[sourceChannel] = receivedChannel;
    findAssignment(correlations, sourceChannel + 1, used, current,
                   score + correlations[sourceChannel][receivedChannel], best);
    used[receivedChannel] = false;
  }
}

}  // namespace

bool loadPcmFile(const std::string& path, int rawChannels, int expectedSampleRate,
                 size_t maxSamplesPerChannel, std::vector<int16_t>& samples, int& channels,
                 int& sampleRate, std::string* error) {
  bool riff = false;
  if (!isRiffFile(path, riff, error)) return false;
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return fail(error, "cannot open input file " + path + ": " + std::strerror(errno));
  uint64_t fileSize = 0;
  if (!getRegularFileSize(file, fileSize)) {
    std::fclose(file);
    return fail(error, "input path is not a regular file: " + path);
  }

  int bitsPerSample = 16;
  uint64_t dataBytes = fileSize;
  WavFormat wavFormat;
  if (riff) {
    if (!parseWavHeader(file, wavFormat, error)) {
      std::fclose(file);
      return false;
    }
    if (wavFormat.bitsPerSample != 16 && wavFormat.bitsPerSample != 24) {
      std::fclose(file);
      return fail(error, "only 16-bit and 24-bit integer PCM WAV files are supported");
    }
    channels = wavFormat.channels;
    sampleRate = static_cast<int>(wavFormat.sampleRate);
    bitsPerSample = wavFormat.bitsPerSample;
    dataBytes = wavFormat.dataSize;
  } else {
    channels = rawChannels;
    sampleRate = expectedSampleRate;
    if (!validChannels(channels)) {
      std::fclose(file);
      return fail(error, "raw PCM channel count must be in [3, 8]");
    }
    if (expectedSampleRate <= 0) {
      std::fclose(file);
      return fail(error, "raw PCM sample rate must be positive");
    }
  }
  if (!validChannels(channels)) {
    std::fclose(file);
    return fail(error, "PCM channel count must be in [3, 8]");
  }
  if (expectedSampleRate > 0 && sampleRate != expectedSampleRate) {
    std::fclose(file);
    std::ostringstream message;
    message << "sample rate mismatch: expected " << expectedSampleRate << ", got " << sampleRate;
    return fail(error, message.str());
  }
  if (riff) {
    if (fseeko(file, static_cast<off_t>(wavFormat.dataOffset), SEEK_SET) != 0) {
      std::fclose(file);
      return fail(error, "cannot seek to WAV data chunk");
    }
  } else if (fseeko(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return fail(error, "cannot seek to raw PCM data");
  }
  const bool ok = readPcmBytes(file, dataBytes, bitsPerSample / 8, channels,
                               maxSamplesPerChannel, samples, error);
  std::fclose(file);
  return ok;
}

bool analyzePcmChannels(const std::vector<int16_t>& source, int sourceChannels,
                        const std::vector<int16_t>& received, int receivedChannels,
                        const PcmChannelAnalysisOptions& options,
                        PcmChannelAnalysis& analysis, std::string* error) {
  analysis = PcmChannelAnalysis();
  analysis.sourceChannels = sourceChannels;
  analysis.receivedChannels = receivedChannels;
  if (!validChannels(sourceChannels) || !validChannels(receivedChannels)) {
    return fail(error, "source and received channel counts must be in [3, 8]");
  }
  if (source.size() % sourceChannels != 0 || received.size() % receivedChannels != 0) {
    return fail(error, "source or received PCM does not contain complete frames");
  }
  analysis.channelCountMatches = sourceChannels == receivedChannels;
  if (!analysis.channelCountMatches) {
    analysis.failureReason = "received channel count differs from source";
    return true;
  }
  const size_t sourceFrames = source.size() / sourceChannels;
  const size_t receivedFrames = received.size() / receivedChannels;
  if (options.sourceOffsetSamples >= sourceFrames || options.receivedOffsetSamples >= receivedFrames) {
    return fail(error, "comparison offset is beyond the available PCM samples");
  }
  size_t frames = std::min(sourceFrames - options.sourceOffsetSamples,
                           receivedFrames - options.receivedOffsetSamples);
  if (options.maxSamplesPerChannel != 0) {
    frames = std::min(frames, options.maxSamplesPerChannel);
  }
  if (frames < 2) return fail(error, "not enough overlapping PCM samples to compare");
  analysis.comparedSamplesPerChannel = frames;
  analysis.sourceStats.resize(sourceChannels);
  analysis.receivedStats.resize(receivedChannels);
  for (int channel = 0; channel < sourceChannels; ++channel) {
    analysis.sourceStats[channel] =
        statsForChannel(source, sourceChannels, options.sourceOffsetSamples, frames, channel);
    analysis.receivedStats[channel] =
        statsForChannel(received, receivedChannels, options.receivedOffsetSamples, frames, channel);
  }

  std::vector<std::vector<double>> correlations(
      sourceChannels, std::vector<double>(receivedChannels, 0.0));
  for (int sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
    for (int receivedChannel = 0; receivedChannel < receivedChannels; ++receivedChannel) {
      correlations[sourceChannel][receivedChannel] =
          correlationAtOffsets(source, sourceChannels, sourceChannel, options.sourceOffsetSamples,
                               received, receivedChannels, receivedChannel,
                               options.receivedOffsetSamples, frames);
    }
  }

  std::vector<bool> used(receivedChannels, false);
  std::vector<size_t> current(sourceChannels, 0);
  Assignment assignment;
  findAssignment(correlations, 0, used, current, 0.0, assignment);
  if (assignment.receivedForSource.size() != static_cast<size_t>(sourceChannels)) {
    return fail(error, "failed to find a one-to-one channel assignment");
  }
  analysis.matches.reserve(sourceChannels);
  bool mappingPass = true;
  for (int sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
    const size_t receivedChannel = assignment.receivedForSource[sourceChannel];
    const double sourceRms = analysis.sourceStats[sourceChannel].rms;
    const double receivedRms = analysis.receivedStats[receivedChannel].rms;
    const double rmsRatio = sourceRms > 0.0 ? receivedRms / sourceRms : 0.0;
    PcmChannelMatch match;
    match.sourceChannel = sourceChannel;
    match.receivedChannel = receivedChannel;
    match.correlation = correlations[sourceChannel][receivedChannel];
    match.receivedRmsRatio = rmsRatio;
    analysis.matches.push_back(match);
    if (match.correlation < options.minimumCorrelation || rmsRatio < options.minimumRmsRatio) {
      mappingPass = false;
    }
  }

  for (int sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
    const size_t receivedChannel = assignment.receivedForSource[sourceChannel];
    const double ownCorrelation =
        std::fabs(correlations[sourceChannel][receivedChannel]);
    if (ownCorrelation < options.minimumCorrelation) continue;
    for (int otherSource = 0; otherSource < sourceChannels; ++otherSource) {
      if (otherSource == sourceChannel) continue;
      if (std::fabs(correlations[otherSource][receivedChannel]) >=
          options.crosstalkCorrelation) {
        analysis.crosstalkDetected = true;
        break;
      }
    }
    if (analysis.crosstalkDetected) break;
  }
  analysis.mappingMatches = mappingPass;
  analysis.passed = analysis.channelCountMatches && analysis.mappingMatches &&
                    !analysis.crosstalkDetected;
  if (!analysis.passed) {
    if (!analysis.channelCountMatches) {
      analysis.failureReason = "received channel count differs from source";
    } else if (analysis.crosstalkDetected) {
      analysis.failureReason = "ambiguous source correlation indicates crosstalk or duplication";
    } else {
      analysis.failureReason = "one or more channels failed correlation or RMS checks";
    }
  }
  return true;
}

}  // namespace agora_demo
