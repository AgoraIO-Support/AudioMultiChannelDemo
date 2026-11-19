#include "../common/multichannel_input_validation.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

std::string makeTempPath() {
  char path[] = "/tmp/agora-multichannel-test-XXXXXX";
  const int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);
  return path;
}

void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void appendChunk(std::vector<uint8_t>& bytes, const char id[4],
                 const std::vector<uint8_t>& payload) {
  bytes.insert(bytes.end(), id, id + 4);
  const size_t oldSize = bytes.size();
  bytes.resize(oldSize + 4);
  put32(bytes, oldSize, static_cast<uint32_t>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  if (payload.size() & 1u) bytes.push_back(0);
}

std::vector<uint8_t> makeFmt(uint16_t formatType, uint16_t channels, uint32_t sampleRate,
                             uint16_t bitsPerSample, bool extensibleFloat) {
  const size_t fmtSize = formatType == 0xFFFE ? 40 : 16;
  std::vector<uint8_t> fmt(fmtSize, 0);
  const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
  put16(fmt, 0, formatType);
  put16(fmt, 2, channels);
  put32(fmt, 4, sampleRate);
  put32(fmt, 8, sampleRate * blockAlign);
  put16(fmt, 12, blockAlign);
  put16(fmt, 14, bitsPerSample);
  if (formatType == 0xFFFE) {
    put16(fmt, 16, 22);
    put16(fmt, 18, bitsPerSample);
    // KSDATAFORMAT_SUBTYPE_PCM or IEEE_FLOAT GUID.
    fmt[24] = extensibleFloat ? 0x03 : 0x01;
    fmt[30] = 0x10;
    fmt[32] = 0x80;
    fmt[33] = 0x00;
    fmt[34] = 0x00;
    fmt[35] = 0xaa;
    fmt[36] = 0x00;
    fmt[37] = 0x38;
    fmt[38] = 0x9b;
    fmt[39] = 0x71;
  }
  return fmt;
}

std::vector<uint8_t> makeWav(const std::vector<uint8_t>& fmt,
                             const std::vector<uint8_t>& data,
                             bool oddJunk = false) {
  std::vector<uint8_t> bytes(12, 0);
  std::memcpy(bytes.data(), "RIFF", 4);
  std::memcpy(bytes.data() + 8, "WAVE", 4);
  if (oddJunk) appendChunk(bytes, "JUNK", std::vector<uint8_t>(3, 0x7f));
  appendChunk(bytes, "fmt ", fmt);
  appendChunk(bytes, "data", data);
  put32(bytes, 4, static_cast<uint32_t>(bytes.size() - 8));
  return bytes;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  FILE* file = std::fopen(path.c_str(), "wb");
  assert(file);
  assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
  assert(std::fclose(file) == 0);
}

bool parseFile(const std::string& path, agora_demo::WavFormat& format, std::string& error) {
  FILE* file = std::fopen(path.c_str(), "rb");
  assert(file);
  const bool ok = agora_demo::parseWavHeader(file, format, &error);
  std::fclose(file);
  return ok;
}

void expectWavRejected(const std::vector<uint8_t>& bytes) {
  const std::string path = makeTempPath();
  writeFile(path, bytes);
  agora_demo::WavFormat format;
  std::string error;
  assert(!parseFile(path, format, error));
  std::remove(path.c_str());
}

void expectWavValidationRejected(const std::vector<uint8_t>& bytes) {
  const std::string path = makeTempPath();
  writeFile(path, bytes);
  std::string error;
  assert(!agora_demo::validateWavFile(path, 48000, 8, 125, 8, 48000, &error));
  std::remove(path.c_str());
}

}  // namespace

int main() {
  const uint8_t negativeOne[3] = {0xff, 0xff, 0xff};
  const uint8_t negativeMin[3] = {0x00, 0x00, 0x80};
  const uint8_t positiveMax[3] = {0xff, 0xff, 0x7f};
  const uint8_t positiveOne[3] = {0x01, 0x00, 0x00};
  assert(agora_demo::decodePcm24LeToS16(negativeOne) == -1);
  assert(agora_demo::decodePcm24LeToS16(negativeMin) == -32768);
  assert(agora_demo::decodePcm24LeToS16(positiveMax) == 32767);
  assert(agora_demo::decodePcm24LeToS16(positiveOne) == 0);

  const uint16_t channels = 8;
  const uint32_t sampleRate = 48000;
  const size_t frameBytes = channels * 2 * (sampleRate / 100);
  const std::vector<uint8_t> data(frameBytes, 0x12);

  // A legal odd-sized JUNK chunk must consume one alignment byte.
  const std::string validPath = makeTempPath();
  writeFile(validPath, makeWav(makeFmt(1, channels, sampleRate, 16, false), data, true));
  agora_demo::WavFormat format;
  std::string error;
  assert(parseFile(validPath, format, error));
  assert(format.channels == channels && format.sampleRate == sampleRate);
  assert(format.dataSize == frameBytes);
  assert(agora_demo::validateWavFile(validPath, sampleRate, channels, 125, channels,
                                     sampleRate, &error));
  std::remove(validPath.c_str());

  // RIFF and chunk declarations must not extend beyond the actual file.
  std::vector<uint8_t> truncated = makeWav(makeFmt(1, channels, sampleRate, 16, false), data);
  truncated.resize(truncated.size() - 1);
  expectWavRejected(truncated);
  std::vector<uint8_t> oversizedChunk = makeWav(makeFmt(1, channels, sampleRate, 16, false), data);
  put32(oversizedChunk, 16, 0x7fffffff);
  expectWavRejected(oversizedChunk);

  // Extensible integer PCM is accepted; Extensible float and unsupported bit
  // depths/formats are rejected.
  const std::string extensiblePath = makeTempPath();
  writeFile(extensiblePath, makeWav(makeFmt(0xFFFE, channels, sampleRate, 16, false), data));
  assert(agora_demo::validateWavFile(extensiblePath, sampleRate, channels, 125, channels,
                                     sampleRate, &error));
  std::remove(extensiblePath.c_str());
  expectWavValidationRejected(makeWav(makeFmt(0xFFFE, channels, sampleRate, 32, false),
                                      std::vector<uint8_t>(frameBytes * 2, 0)));
  expectWavRejected(makeWav(makeFmt(0xFFFE, channels, sampleRate, 32, true), data));
  expectWavRejected(makeWav(makeFmt(3, channels, sampleRate, 16, false), data));

  // CLI/header mismatch and a file shorter than one 10 ms frame are rejected before connect.
  const std::string shortPath = makeTempPath();
  writeFile(shortPath, makeWav(makeFmt(1, channels, 44100, 16, false), data));
  assert(!agora_demo::validateWavFile(shortPath, sampleRate, channels, 125, channels,
                                      sampleRate, &error));
  std::remove(shortPath.c_str());
  const std::string tinyPath = makeTempPath();
  writeFile(tinyPath, std::vector<uint8_t>(frameBytes - 1, 0));
  assert(!agora_demo::validateRawPcmFile(tinyPath, sampleRate, channels, &error));
  std::remove(tinyPath.c_str());

  // Raw PCM final partial frames are padded; an empty file is a real EOF.
  const std::string pcmPath = makeTempPath();
  std::vector<uint8_t> pcm(frameBytes + channels * 2, 0x44);
  writeFile(pcmPath, pcm);
  assert(agora_demo::validateRawPcmFile(pcmPath, sampleRate, channels, &error));
  FILE* pcmFile = std::fopen(pcmPath.c_str(), "rb");
  assert(pcmFile);
  std::vector<uint8_t> frame(frameBytes, 0);
  size_t bytesRead = 0;
  assert(agora_demo::readFrameWithPadding(pcmFile, frame.data(), frame.size(), &bytesRead, &error) ==
         agora_demo::FrameReadResult::kComplete);
  assert(agora_demo::readFrameWithPadding(pcmFile, frame.data(), frame.size(), &bytesRead, &error) ==
         agora_demo::FrameReadResult::kPadded);
  assert(bytesRead == channels * 2 && frame[frame.size() - 1] == 0);
  assert(agora_demo::readFrameWithPadding(pcmFile, frame.data(), frame.size(), &bytesRead, &error) ==
         agora_demo::FrameReadResult::kEof);
  std::fclose(pcmFile);
  std::remove(pcmPath.c_str());

  const std::string opusPath = makeTempPath();
  writeFile(opusPath, std::vector<uint8_t>(1280, 0x55));
  assert(agora_demo::validateOpusRawFile(opusPath, 640, &error));
  writeFile(opusPath, std::vector<uint8_t>(641, 0x55));
  assert(!agora_demo::validateOpusRawFile(opusPath, 640, &error));
  writeFile(opusPath, std::vector<uint8_t>{'O', 'g', 'g', 'S'});
  assert(!agora_demo::validateOpusRawFile(opusPath, 640, &error));
  std::remove(opusPath.c_str());

  assert(agora_demo::payloadTypeForChannels(3) == 123);
  assert(agora_demo::payloadTypeForChannels(6) == 124);
  assert(agora_demo::payloadTypeForChannels(8) == 125);
  return 0;
}
