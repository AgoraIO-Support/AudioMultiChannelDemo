#include "multichannel_input_validation.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace agora_demo {
namespace {

bool fail(std::string* error, const std::string& message) {
  if (error) *error = message;
  return false;
}

bool readExact(FILE* file, void* buffer, size_t size) {
  return size == 0 || fread(buffer, 1, size, file) == size;
}

bool readU32LE(FILE* file, uint32_t& value) {
  uint8_t bytes[4];
  if (!readExact(file, bytes, sizeof(bytes))) return false;
  value = static_cast<uint32_t>(bytes[0]) |
          (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) |
          (static_cast<uint32_t>(bytes[3]) << 24);
  return true;
}

uint16_t getU16LE(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t getU32LE(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

bool getFileSize(FILE* file, uint64_t& size) {
  const off_t current = ftello(file);
  if (current < 0 || fseeko(file, 0, SEEK_END) != 0) return false;
  const off_t end = ftello(file);
  const bool ok = end >= 0 && fseeko(file, current, SEEK_SET) == 0;
  if (ok) size = static_cast<uint64_t>(end);
  return ok;
}

bool skip(FILE* file, uint64_t bytes) {
  if (bytes > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
  return fseeko(file, static_cast<off_t>(bytes), SEEK_CUR) == 0;
}

bool parseFmtChunk(const std::vector<uint8_t>& chunk, WavFormat& format, std::string* error) {
  if (chunk.size() < 16) return fail(error, "WAV fmt chunk is shorter than 16 bytes");

  const uint16_t formatType = getU16LE(chunk, 0);
  format.channels = getU16LE(chunk, 2);
  format.sampleRate = getU32LE(chunk, 4);
  const uint32_t byteRate = getU32LE(chunk, 8);
  const uint16_t blockAlign = getU16LE(chunk, 12);
  format.bitsPerSample = getU16LE(chunk, 14);

  if (formatType != 1 && formatType != 0xFFFE) {
    std::ostringstream message;
    message << "unsupported WAV format type " << formatType << "; only PCM is supported";
    return fail(error, message.str());
  }
  if (format.channels == 0 || format.sampleRate == 0 || format.bitsPerSample == 0) {
    return fail(error, "WAV fmt chunk contains zero channel count, sample rate, or bit depth");
  }

  const uint64_t bytesPerSample = (static_cast<uint64_t>(format.bitsPerSample) + 7) / 8;
  const uint64_t expectedBlockAlign = bytesPerSample * format.channels;
  const uint64_t expectedByteRate = expectedBlockAlign * format.sampleRate;
  if (expectedBlockAlign > std::numeric_limits<uint16_t>::max() ||
      expectedByteRate > std::numeric_limits<uint32_t>::max() ||
      blockAlign != expectedBlockAlign || byteRate != expectedByteRate) {
    return fail(error, "WAV fmt byte rate or block alignment is inconsistent with the format");
  }

  if (formatType == 0xFFFE) {
    if (chunk.size() < 40) {
      return fail(error, "WAVE_FORMAT_EXTENSIBLE fmt chunk is shorter than 40 bytes");
    }
    const uint16_t cbSize = getU16LE(chunk, 16);
    const uint16_t validBits = getU16LE(chunk, 18);
    if (cbSize < 22 || validBits == 0 || validBits > format.bitsPerSample) {
      return fail(error, "invalid WAVE_FORMAT_EXTENSIBLE extension");
    }
    // KSDATAFORMAT_SUBTYPE_PCM: 00000001-0000-0010-8000-00aa00389b71.
    static const uint8_t kPcmGuid[16] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
    if (memcmp(chunk.data() + 24, kPcmGuid, sizeof(kPcmGuid)) != 0) {
      return fail(error, "WAVE_FORMAT_EXTENSIBLE sub-format is not integer PCM");
    }
  }
  return true;
}

bool openRegularFile(const std::string& path, FILE** file, uint64_t& size, std::string* error) {
  *file = fopen(path.c_str(), "rb");
  if (!*file) {
    std::ostringstream message;
    message << "cannot open input file " << path << ": " << std::strerror(errno);
    return fail(error, message.str());
  }
  struct stat info;
  if (fstat(fileno(*file), &info) != 0 || !S_ISREG(info.st_mode)) {
    fclose(*file);
    *file = nullptr;
    return fail(error, "input path is not a regular file: " + path);
  }
  size = static_cast<uint64_t>(info.st_size);
  return true;
}

}  // namespace

int payloadTypeForChannels(int channels) {
  if (channels <= 4) return 123;
  if (channels <= 6) return 124;
  return 125;
}

bool validateMultiChannelConfig(int channels, int sampleRate, int customPayloadType,
                                int customChannelNum, int customSampleRate,
                                std::string* error) {
  if (channels < 3 || channels > 8) {
    return fail(error, "numOfChannels must be in [3, 8]");
  }
  if (sampleRate != 48000) {
    return fail(error, "multi-channel OPUSMC only supports 48000 Hz");
  }
  if (customPayloadType != 0 && customPayloadType != 123 && customPayloadType != 124 &&
      customPayloadType != 125) {
    return fail(error, "custom_payload_type must be 123, 124, or 125");
  }
  if (customPayloadType != 0 && customPayloadType != payloadTypeForChannels(channels)) {
    return fail(error, "custom_payload_type is incompatible with numOfChannels");
  }
  if (customChannelNum != 0 && customChannelNum != channels) {
    return fail(error, "custom_channel_num must match the input channel count");
  }
  if (customSampleRate != 0 && customSampleRate != sampleRate) {
    return fail(error, "custom_sample_rate must match the input sample rate");
  }
  return true;
}

bool parseWavHeader(FILE* file, WavFormat& format, std::string* error) {
  if (!file) return fail(error, "WAV file handle is null");
  uint64_t fileSize = 0;
  if (!getFileSize(file, fileSize) || fseeko(file, 0, SEEK_SET) != 0) {
    return fail(error, "cannot determine WAV file size");
  }
  if (fileSize < 12) return fail(error, "WAV file is shorter than the RIFF header");

  char id[4];
  uint32_t riffSize = 0;
  if (!readExact(file, id, sizeof(id)) || memcmp(id, "RIFF", 4) != 0 ||
      !readU32LE(file, riffSize) || !readExact(file, id, sizeof(id)) ||
      memcmp(id, "WAVE", 4) != 0) {
    return fail(error, "file is not a RIFF/WAVE file");
  }
  if (riffSize < 4 || static_cast<uint64_t>(riffSize) + 8 > fileSize) {
    return fail(error, "RIFF container is truncated");
  }

  const uint64_t riffEnd = static_cast<uint64_t>(riffSize) + 8;
  bool fmtFound = false;
  bool dataFound = false;
  while (static_cast<uint64_t>(ftello(file)) + 8 <= riffEnd) {
    const off_t chunkHeaderOffset = ftello(file);
    uint32_t chunkSize = 0;
    if (!readExact(file, id, sizeof(id)) || !readU32LE(file, chunkSize)) {
      return fail(error, "truncated RIFF chunk header");
    }
    const uint64_t dataOffset = static_cast<uint64_t>(chunkHeaderOffset) + 8;
    const uint64_t paddedSize = static_cast<uint64_t>(chunkSize) + (chunkSize & 1u);
    if (dataOffset > riffEnd || paddedSize > riffEnd - dataOffset ||
        dataOffset > fileSize || paddedSize > fileSize - dataOffset) {
      return fail(error, "RIFF chunk exceeds the file or RIFF container bounds");
    }

    if (memcmp(id, "fmt ", 4) == 0) {
      if (chunkSize > 1024 * 1024) return fail(error, "WAV fmt chunk is unreasonably large");
      std::vector<uint8_t> chunk(chunkSize);
      if (!readExact(file, chunk.data(), chunk.size()) ||
          !parseFmtChunk(chunk, format, error)) {
        return false;
      }
      fmtFound = true;
      if ((chunkSize & 1u) && !readExact(file, id, 1)) {
        return fail(error, "truncated RIFF chunk padding");
      }
    } else {
      if (memcmp(id, "data", 4) == 0 && !dataFound) {
        format.dataOffset = dataOffset;
        format.dataSize = chunkSize;
        dataFound = true;
      }
      if (!skip(file, paddedSize)) return fail(error, "cannot skip RIFF chunk");
    }

    if (fmtFound && dataFound) break;
  }

  if (!fmtFound || !dataFound) {
    return fail(error, "WAV file does not contain both fmt and data chunks");
  }
  if (format.dataOffset > fileSize || format.dataSize > fileSize - format.dataOffset) {
    return fail(error, "WAV data chunk exceeds the actual file size");
  }
  if (fseeko(file, static_cast<off_t>(format.dataOffset), SEEK_SET) != 0) {
    return fail(error, "cannot seek to WAV data chunk");
  }
  return true;
}

bool validateWavFile(const std::string& path, int expectedSampleRate, int expectedChannels,
                    int customPayloadType, int customChannelNum, int customSampleRate,
                    std::string* error) {
  if (!validateMultiChannelConfig(expectedChannels, expectedSampleRate, customPayloadType,
                                  customChannelNum, customSampleRate, error)) {
    return false;
  }
  FILE* file = nullptr;
  uint64_t fileSize = 0;
  if (!openRegularFile(path, &file, fileSize, error)) return false;
  WavFormat format;
  const bool parsed = parseWavHeader(file, format, error);
  fclose(file);
  if (!parsed) return false;

  if (format.bitsPerSample != 16 && format.bitsPerSample != 24) {
    std::ostringstream message;
    message << "unsupported WAV bit depth " << format.bitsPerSample
            << "; only 16-bit and 24-bit PCM are supported";
    return fail(error, message.str());
  }
  if (format.sampleRate != 48000 || format.sampleRate != static_cast<uint32_t>(expectedSampleRate)) {
    return fail(error, "WAV sample rate must be 48000 and match --sampleRate");
  }
  if (format.channels < 3 || format.channels > 8 ||
      format.channels != static_cast<uint16_t>(expectedChannels)) {
    return fail(error, "WAV channel count must be in [3, 8] and match --numOfChannels");
  }
  const uint64_t frameBytes = (format.bitsPerSample / 8u) * format.channels * (format.sampleRate / 100u);
  if (format.dataSize < frameBytes) {
    return fail(error, "WAV data chunk does not contain one complete 10 ms frame");
  }
  const uint64_t sampleBytes = (format.bitsPerSample / 8u) * format.channels;
  if (sampleBytes == 0 || format.dataSize % sampleBytes != 0) {
    return fail(error, "WAV data chunk ends in a partial interleaved sample");
  }
  return true;
}

bool validateRawPcmFile(const std::string& path, int sampleRate, int channels,
                       std::string* error) {
  if (!validateMultiChannelConfig(channels, sampleRate, 0, 0, 0, error)) return false;
  FILE* file = nullptr;
  uint64_t fileSize = 0;
  if (!openRegularFile(path, &file, fileSize, error)) return false;
  fclose(file);
  const uint64_t frameBytes = static_cast<uint64_t>(sampleRate / 100) * channels * sizeof(int16_t);
  if (fileSize < frameBytes) return fail(error, "raw PCM file does not contain one complete frame");
  const uint64_t sampleBytes = static_cast<uint64_t>(channels) * sizeof(int16_t);
  if (sampleBytes == 0 || fileSize % sampleBytes != 0) {
    return fail(error, "raw PCM file ends in a partial interleaved sample");
  }
  return true;
}

bool validateOpusRawFile(const std::string& path, size_t frameBytes, std::string* error) {
  if (frameBytes == 0) return fail(error, "Opus frame size must be non-zero");
  FILE* file = nullptr;
  uint64_t fileSize = 0;
  if (!openRegularFile(path, &file, fileSize, error)) return false;
  char magic[4] = {};
  const bool hasMagic = fread(magic, 1, sizeof(magic), file) == sizeof(magic);
  fclose(file);
  if (hasMagic && memcmp(magic, "OggS", 4) == 0) {
    return fail(error, "Ogg Opus containers are not supported; provide raw Opus frames");
  }
  if (fileSize < frameBytes) return fail(error, "Opus input does not contain one complete frame");
  if (fileSize % frameBytes != 0) {
    return fail(error, "Opus input size is not an integral number of frames");
  }
  return true;
}

FrameReadResult readFrameWithPadding(FILE* file, void* buffer, size_t frameBytes,
                                     size_t* bytesRead, std::string* error) {
  if (bytesRead) *bytesRead = 0;
  if (!file || !buffer || frameBytes == 0) {
    if (error) *error = "invalid frame read arguments";
    return FrameReadResult::kError;
  }
  const size_t count = fread(buffer, 1, frameBytes, file);
  if (bytesRead) *bytesRead = count;
  if (count == frameBytes) return FrameReadResult::kComplete;
  if (ferror(file)) {
    if (error) *error = std::strerror(errno);
    return FrameReadResult::kError;
  }
  if (count == 0 && feof(file)) return FrameReadResult::kEof;
  if (feof(file) && count > 0) {
    memset(static_cast<uint8_t*>(buffer) + count, 0, frameBytes - count);
    return FrameReadResult::kPadded;
  }
  if (error) *error = "short frame read without EOF or read error";
  return FrameReadResult::kError;
}

}  // namespace agora_demo
