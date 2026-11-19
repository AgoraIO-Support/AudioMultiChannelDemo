#include "../common/pcm_channel_analyzer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<int16_t> makeSource(int channels, size_t frames) {
  std::vector<int16_t> samples(frames * channels);
  for (size_t frame = 0; frame < frames; ++frame) {
    for (int channel = 0; channel < channels; ++channel) {
      // Distinct periodic IDs make channel swaps and copies observable while
      // keeping the test deterministic and independent of an audio library.
      const double phase = 2.0 * 3.14159265358979323846 *
                           (static_cast<double>(frame) * (channel + 2) / frames);
      samples[frame * channels + channel] = static_cast<int16_t>(
          5000.0 * std::sin(phase) + 1000.0 * (channel + 1));
    }
  }
  return samples;
}

std::vector<int16_t> permute(const std::vector<int16_t>& source, int channels,
                             const std::vector<int>& sourceForReceived) {
  const size_t frames = source.size() / channels;
  std::vector<int16_t> output(source.size());
  for (size_t frame = 0; frame < frames; ++frame) {
    for (int received = 0; received < channels; ++received) {
      output[frame * channels + received] =
          source[frame * channels + sourceForReceived[received]];
    }
  }
  return output;
}

void expectPass(const std::vector<int16_t>& source, int channels,
                const std::vector<int16_t>& received) {
  agora_demo::PcmChannelAnalysis analysis;
  agora_demo::PcmChannelAnalysisOptions options;
  options.minimumCorrelation = 0.98;
  std::string error;
  assert(agora_demo::analyzePcmChannels(source, channels, received, channels, options, analysis,
                                        &error));
  assert(analysis.passed);
  assert(analysis.matches.size() == static_cast<size_t>(channels));
}

void expectFail(const std::vector<int16_t>& source, int sourceChannels,
                const std::vector<int16_t>& received, int receivedChannels) {
  agora_demo::PcmChannelAnalysis analysis;
  agora_demo::PcmChannelAnalysisOptions options;
  options.minimumCorrelation = 0.98;
  std::string error;
  assert(agora_demo::analyzePcmChannels(source, sourceChannels, received, receivedChannels,
                                        options, analysis, &error));
  assert(!analysis.passed);
}

void expectCrosstalkFail(const std::vector<int16_t>& source, int channels,
                         const std::vector<int16_t>& received) {
  agora_demo::PcmChannelAnalysis analysis;
  agora_demo::PcmChannelAnalysisOptions options;
  options.minimumCorrelation = 0.80;
  options.crosstalkCorrelation = 0.25;
  std::string error;
  assert(agora_demo::analyzePcmChannels(source, channels, received, channels, options, analysis,
                                        &error));
  assert(analysis.crosstalkDetected);
  assert(!analysis.passed);
}

}  // namespace

int main() {
  for (int channels = 3; channels <= 8; ++channels) {
    const std::vector<int16_t> source = makeSource(channels, 4800);
    expectPass(source, channels, source);

    std::vector<int> reversed(channels);
    for (int channel = 0; channel < channels; ++channel) reversed[channel] = channels - channel - 1;
    expectPass(source, channels, permute(source, channels, reversed));

    std::vector<int> duplicated = reversed;
    duplicated[channels - 1] = duplicated[channels - 2];
    expectFail(source, channels, permute(source, channels, duplicated), channels);

    std::vector<int16_t> missing = source;
    for (size_t frame = 0; frame < missing.size() / channels; ++frame) {
      missing[frame * channels + 1] = 0;
    }
    expectFail(source, channels, missing, channels);
    const int mismatchChannels = channels < 8 ? channels + 1 : channels - 1;
    std::vector<int16_t> mismatched((source.size() / channels) * mismatchChannels, 0);
    for (size_t frame = 0; frame < source.size() / channels; ++frame) {
      for (int channel = 0; channel < std::min(channels, mismatchChannels); ++channel) {
        mismatched[frame * mismatchChannels + channel] = source[frame * channels + channel];
      }
    }
    expectFail(source, channels, mismatched, mismatchChannels);

    std::vector<int16_t> crosstalk = source;
    for (size_t frame = 0; frame < source.size() / channels; ++frame) {
      const int mixed = static_cast<int>(source[frame * channels]) +
                        static_cast<int>(source[frame * channels + 1]) / 3;
      crosstalk[frame * channels] = static_cast<int16_t>(mixed);
    }
    expectCrosstalkFail(source, channels, crosstalk);
  }

  // A capture can contain an initial offset after the sender starts. The
  // caller can trim it explicitly instead of silently comparing different
  // portions of the source and received streams.
  const std::vector<int16_t> source = makeSource(3, 4800);
  const size_t offset = 240;
  std::vector<int16_t> received(source.begin() + offset * 3, source.end());
  agora_demo::PcmChannelAnalysis analysis;
  agora_demo::PcmChannelAnalysisOptions options;
  options.sourceOffsetSamples = offset;
  options.minimumCorrelation = 0.98;
  std::string error;
  assert(agora_demo::analyzePcmChannels(source, 3, received, 3, options, analysis, &error));
  assert(analysis.passed);
  return 0;
}
