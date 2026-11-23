# Agora Linux SDK 音频多声道 demo 镜像
#
# 构建上下文必须是仓库根目录（需要同时访问 Demo/linux 与 LinuxSDKFolder）：
#   docker build -f Demo/linux/Dockerfile -t agora-multichannel-linux .
# ARM64（需 buildx + QEMU，或在 ARM 机器上原生构建）：
#   docker buildx build --platform linux/arm64 -f Demo/linux/Dockerfile -t agora-multichannel-linux:arm64 .

FROM debian:bookworm-slim AS builder

ARG TARGETARCH
# demo 镜像不锁定 apt 版本：bookworm 源滚动更新，锁死版本反而易构建失败
# ffmpeg 开发包仅 sample_send_mkv_multiopus 需要（pkg-config + libav*）
# hadolint ignore=DL3008
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake unzip \
    pkg-config libavformat-dev libavcodec-dev libavutil-dev \
    libswresample-dev libswscale-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY LinuxSDKFolder ./LinuxSDKFolder
COPY Demo/linux ./Demo/linux

# 按目标架构选择 SDK 目录，若尚未解压则自动提取对应 ZIP/TGZ 并归一化布局，
# 再把 SDK 动态库收集到固定目录 /sdk-libs，供运行阶段拷贝
RUN set -eux; \
    arch="${TARGETARCH:-$(dpkg --print-architecture)}"; \
    case "$arch" in \
      amd64) SDK_PARENT=/src/LinuxSDKFolder/X86_64 ;; \
      arm64) SDK_PARENT=/src/LinuxSDKFolder/Aarch64 ;; \
      *) echo "unsupported arch: $arch"; exit 1 ;; \
    esac; \
    SDK_DIR="$SDK_PARENT/agora_rtc_sdk"; \
    if [ ! -f "$SDK_DIR/agora_sdk/libagora_rtc_sdk.so" ] || \
       [ ! -f "$SDK_DIR/agora_sdk/include/IAgoraService.h" ]; then \
      SDK_PACKAGE=""; \
      for candidate in "$SDK_PARENT"/Agora_Native_SDK_for_Linux_*.zip; do \
        if [ -f "$candidate" ]; then SDK_PACKAGE="$candidate"; break; fi; \
      done; \
      if [ -z "$SDK_PACKAGE" ]; then \
        for candidate in "$SDK_PARENT"/Agora-RTC-*-linux-gnu-*.tgz; do \
          if [ -f "$candidate" ]; then SDK_PACKAGE="$candidate"; break; fi; \
        done; \
      fi; \
      if [ -z "$SDK_PACKAGE" ]; then echo "SDK package not found in $SDK_PARENT"; exit 1; fi; \
      echo "extracting SDK package: $SDK_PACKAGE"; \
      case "$SDK_PACKAGE" in \
        *.zip) \
          rm -rf "$SDK_DIR" "$SDK_PARENT/Agora_Native_SDK_for_Linux_SERVER"; \
          mkdir -p "$SDK_DIR"; \
          unzip -q "$SDK_PACKAGE" -d "$SDK_PARENT"; \
          SDK_SOURCE_DIR="$SDK_PARENT/Agora_Native_SDK_for_Linux_SERVER/sdk"; \
          if [ ! -f "$SDK_SOURCE_DIR/libagora_rtc_sdk.so" ]; then \
            SDK_SOURCE_DIR="$SDK_PARENT/Agora_Native_SDK_for_Linux_SERVER/rtc/sdk"; \
          fi; \
          if [ ! -f "$SDK_SOURCE_DIR/libagora_rtc_sdk.so" ]; then \
            echo "SDK library not found in $SDK_PACKAGE"; exit 1; \
          fi; \
          SDK_INCLUDE_DIR="$SDK_SOURCE_DIR/include"; \
          if [ ! -f "$SDK_INCLUDE_DIR/IAgoraService.h" ]; then \
            SDK_INCLUDE_DIR="$SDK_SOURCE_DIR/low_level_api/include"; \
          fi; \
          if [ ! -f "$SDK_INCLUDE_DIR/IAgoraService.h" ]; then \
            echo "SDK headers not found in $SDK_PACKAGE"; exit 1; \
          fi; \
          ln -s "$SDK_SOURCE_DIR" "$SDK_DIR/agora_sdk"; \
          ln -s "$SDK_INCLUDE_DIR" "$SDK_DIR/agora_sdk/include" \
          ;; \
        *.tgz) \
          rm -rf "$SDK_DIR"; \
          tar -xzf "$SDK_PACKAGE" -C "$SDK_PARENT" \
          ;; \
      esac; \
    fi; \
    test -f "$SDK_DIR/agora_sdk/libagora_rtc_sdk.so"; \
    cmake -S /src/Demo/linux -B /build \
      -DAGORA_SDK_DIR="$SDK_DIR" -DCMAKE_BUILD_TYPE=Release -Wno-dev; \
    cmake --build /build -j"$(nproc)"; \
    mkdir -p /sdk-libs; \
    cp "$SDK_DIR"/agora_sdk/*.so /sdk-libs/; \
    mkdir -p /src/Demo/linux/out; \
    cp -R /src/Demo/linux/public /src/Demo/linux/out/

FROM debian:bookworm-slim

# sample_send_mkv_multiopus 的运行期 ffmpeg 库（bookworm 59/57/4/6 对应 FFmpeg 5.1）
# hadolint ignore=DL3008
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    libavformat59 libavcodec59 libavutil57 libswresample4 libswscale6 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/Demo/linux/out/ /app/
COPY --from=builder /sdk-libs/ /opt/agora/lib/
ENV LD_LIBRARY_PATH=/opt/agora/lib
RUN useradd --system --uid 10001 --no-create-home --home-dir /app agora \
 && chown agora /app
USER agora

# 默认使用容器网络；仅在网络环境确需 host 模式时再显式启用。
#   docker run --rm -it agora-multichannel-linux \
#     ./sample_send_opus8 --channelId test --token <token> --audioFile public/8ch_2.opus
CMD ["/bin/bash"]
