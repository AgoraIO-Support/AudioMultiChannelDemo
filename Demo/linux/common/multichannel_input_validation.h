#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace agora_demo {

struct WavFormat {
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  uint64_t dataOffset = 0;
  uint64_t dataSize = 0;
};

enum class FrameReadResult {
  kComplete,
  kPadded,
  kEof,
  kError,
};

int payloadTypeForChannels(int channels);

bool validateMultiChannelConfig(int channels, int sampleRate, int customPayloadType,
                                int customChannelNum, int customSampleRate,
                                std::string* error);

// Parses a RIFF/WAVE file and leaves the stream positioned at the data chunk.
// The parser validates chunk bounds against both the RIFF container and the actual file.
bool parseWavHeader(FILE* file, WavFormat& format, std::string* error);

bool validateWavFile(const std::string& path, int expectedSampleRate, int expectedChannels,
                    int customPayloadType, int customChannelNum, int customSampleRate,
                    std::string* error);

// Raw PCM has no container metadata. A file must contain at least one complete frame;
// a final partial frame is accepted because senders pad it with zeros before sending.
bool validateRawPcmFile(const std::string& path, int sampleRate, int channels,
                       std::string* error);

// Encoded Opus frames cannot be padded. The file must contain whole 640-byte frames.
bool validateOpusRawFile(const std::string& path, size_t frameBytes, std::string* error);

// Reads one frame. A partial final frame is zero-padded and reported as kPadded.
FrameReadResult readFrameWithPadding(FILE* file, void* buffer, size_t frameBytes,
                                     size_t* bytesRead, std::string* error);

}  // namespace agora_demo
