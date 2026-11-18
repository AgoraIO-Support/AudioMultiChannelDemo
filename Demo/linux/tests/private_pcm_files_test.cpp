#include "../multiopus/private_pcm_files.h"

#include <cassert>
#include <cstring>
#include <sys/stat.h>

int main() {
  std::string firstDirectory;
  {
    PrivatePcmFiles first;
    PrivatePcmFiles second;
    assert(first.initialize());
    assert(second.initialize());
    assert(first.pcmPath() != second.pcmPath());
    firstDirectory = first.pcmPath().substr(0, first.pcmPath().find_last_of('/'));

    struct stat info;
    assert(stat(firstDirectory.c_str(), &info) == 0);
    assert(S_ISDIR(info.st_mode) && (info.st_mode & 0777) == 0700);

    FILE* pcm = PrivatePcmFiles::create(first.pcmPath());
    assert(pcm);
    assert(fputs("original", pcm) >= 0);
    assert(fclose(pcm) == 0);
    assert(stat(first.pcmPath().c_str(), &info) == 0);
    assert(S_ISREG(info.st_mode) && (info.st_mode & 0077) == 0);

    // Existing files and symlinks must never be truncated or followed.
    assert(!PrivatePcmFiles::create(first.pcmPath()));
    assert(symlink(first.pcmPath().c_str(), first.trackPath(0).c_str()) == 0);
    assert(!PrivatePcmFiles::create(first.trackPath(0)));
    char content[16] = {};
    pcm = fopen(first.pcmPath().c_str(), "rb");
    assert(pcm && fread(content, 1, 8, pcm) == 8);
    assert(fclose(pcm) == 0);
    assert(std::strcmp(content, "original") == 0);

    FILE* track = PrivatePcmFiles::create(first.trackPath(1));
    assert(track && fclose(track) == 0);
  }
  struct stat info;
  assert(lstat(firstDirectory.c_str(), &info) != 0 && errno == ENOENT);
}
