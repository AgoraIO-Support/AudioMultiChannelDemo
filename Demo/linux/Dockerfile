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
 && apt-get install -y --no-install-recommends build-essential cmake \
    pkg-config libavformat-dev libavcodec-dev libavutil-dev \
    libswresample-dev libswscale-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY LinuxSDKFolder ./LinuxSDKFolder
COPY Demo/linux ./Demo/linux

# 按目标架构选择 SDK（amd64→X86_64，arm64→Aarch64），编译 sample，
# 并把 SDK 动态库收集到固定目录 /sdk-libs，供运行阶段拷贝
RUN set -eux; \
    arch="${TARGETARCH:-$(dpkg --print-architecture)}"; \
    case "$arch" in \
      amd64) SDK_DIR=/src/LinuxSDKFolder/X86_64/agora_rtc_sdk ;; \
      arm64) SDK_DIR=/src/LinuxSDKFolder/Aarch64/agora_rtc_sdk ;; \
      *) echo "unsupported arch: $arch"; exit 1 ;; \
    esac; \
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

# RTC 媒体走 UDP，建议运行时加 --network host，例如：
#   docker run --rm -it --network host agora-multichannel-linux \
#     ./sample_send_opus8 --channelId test --token <token> --audioFile public/8ch_2.opus
CMD ["/bin/bash"]
