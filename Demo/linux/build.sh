#!/bin/bash
# Agora Linux SDK 音频多声道 demo 便捷构建脚本
# 用法: ./build.sh [x86_64|aarch64] [clean]
#   不传架构时按 uname -m 自动选择 LinuxSDKFolder 下的 X86_64 或 Aarch64 SDK。

set -e

CURRENT=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$CURRENT/../.." && pwd)

# clean 可作为第一个或第二个参数：./build.sh clean | ./build.sh x86_64 clean
if [ "$1" = "clean" ] || [ "$2" = "clean" ]; then
  rm -rf "$CURRENT/build" "$CURRENT/out"
  echo "clean done"
  exit 0
fi

arch="$1"
if [ -z "$arch" ]; then
  case "$(uname -m)" in
    x86_64)  arch="x86_64" ;;
    aarch64|arm64) arch="aarch64" ;;
    *) echo "unsupported host arch: $(uname -m)"; exit 1 ;;
  esac
fi

case "$arch" in
  x86_64)  SDK_DIR="$PROJECT_ROOT/LinuxSDKFolder/X86_64/agora_rtc_sdk" ;;
  aarch64) SDK_DIR="$PROJECT_ROOT/LinuxSDKFolder/Aarch64/agora_rtc_sdk" ;;
  *) echo "usage: $0 [x86_64|aarch64] [clean]"; exit 1 ;;
esac

SDK_PACKAGE_DIR=$(dirname "$SDK_DIR")
if [ ! -d "$SDK_DIR/agora_sdk" ] || [ ! -f "$SDK_DIR/agora_sdk/libagora_rtc_sdk.so" ]; then
  SDK_TGZ=""
  for candidate in "$SDK_PACKAGE_DIR"/Agora-RTC-*-linux-gnu-*.tgz; do
    if [ -f "$candidate" ]; then
      SDK_TGZ="$candidate"
      break
    fi
  done
  if [ -z "$SDK_TGZ" ]; then
    echo "error: SDK package not found in $SDK_PACKAGE_DIR"
    exit 1
  fi
  echo "extracting SDK package: $SDK_TGZ"
  tar -xzf "$SDK_TGZ" -C "$SDK_PACKAGE_DIR"
fi

if [ ! -f "$SDK_DIR/agora_sdk/libagora_rtc_sdk.so" ]; then
  echo "error: SDK not found at $SDK_DIR after extraction"
  exit 1
fi

echo "target arch : $arch"
echo "sdk dir     : $SDK_DIR"

mkdir -p "$CURRENT/build"
cd "$CURRENT/build"
cmake "$CURRENT" -DAGORA_SDK_DIR="$SDK_DIR" -DCMAKE_BUILD_TYPE=Release -Wno-dev
make -j8

# 将素材拷贝到 out/ 旁边，方便直接运行（sample 默认从 public/ 读取素材）
mkdir -p "$CURRENT/out"
cp -R "$CURRENT/public" "$CURRENT/out/" 2>/dev/null || true

echo "build done, binaries are in $CURRENT/out"
