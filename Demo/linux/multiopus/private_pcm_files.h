#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <unistd.h>

// Each run owns a private directory. Destruction removes partial files as well
// as completed output, including on early returns from initialization.
class PrivatePcmFiles {
 public:
  static constexpr int kTrackCount = 4;
  PrivatePcmFiles() = default;
  PrivatePcmFiles(const PrivatePcmFiles&) = delete;
  PrivatePcmFiles& operator=(const PrivatePcmFiles&) = delete;

  ~PrivatePcmFiles() {
    if (directory_.empty()) return;
    for (int i = 0; i < kTrackCount; ++i) remove(trackPath(i).c_str());
    remove(pcmPath().c_str());
    if (rmdir(directory_.c_str()) != 0) {
      std::fprintf(stderr, "Failed to remove temporary directory %s\n", directory_.c_str());
    }
  }

  bool initialize() {
    if (!directory_.empty()) {
      errno = EALREADY;
      return false;
    }
    char path[] = "/tmp/agora-multiopus-XXXXXX";
    if (!mkdtemp(path)) return false;  // atomically creates a 0700 directory
    directory_ = path;
    return true;
  }

  std::string trackPath(int index) const {
    return directory_ + "/track" + std::to_string(index) + ".pcm";
  }

  std::string pcmPath() const { return directory_ + "/audio-8ch.pcm"; }

  static FILE* create(const std::string& path) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return nullptr;
    FILE* file = fdopen(fd, "wb");
    if (!file) {
      int savedErrno = errno;
      close(fd);
      unlink(path.c_str());
      errno = savedErrno;
    }
    return file;
  }

 private:
  std::string directory_;
};
