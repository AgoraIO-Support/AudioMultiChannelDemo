#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/.." && pwd)
out_bin=${TMPDIR:-/tmp}/pcm_channel_analyzer_test

${CXX:-c++} -std=c++11 -Wall -Wextra -Werror \
  "$root_dir/common/multichannel_input_validation.cpp" \
  "$root_dir/common/pcm_channel_analyzer.cpp" \
  "$root_dir/tests/pcm_channel_analyzer_test.cpp" \
  -o "$out_bin"
"$out_bin"
rm -f "$out_bin"

# Exercise both WAV loading and raw PCM loading when the checked-in assets are
# available. Keep this bounded to one second so the test stays suitable for CI.
if [ -f "$root_dir/public/8_Channel_ID_signed_16_bit_pcm.wav" ] &&
   [ -f "$root_dir/public/8_Channel_ID_signed_16_bit_pcm.raw.pcm" ]; then
  tool_bin=${TMPDIR:-/tmp}/pcm_channel_analyzer_tool
  ${CXX:-c++} -std=c++11 -Wall -Wextra -Werror -I"$root_dir" \
    "$root_dir/common/multichannel_input_validation.cpp" \
    "$root_dir/common/pcm_channel_analyzer.cpp" \
    "$root_dir/tools/pcm_channel_analyzer.cpp" \
    -o "$tool_bin"
  "$tool_bin" --source "$root_dir/public/8_Channel_ID_signed_16_bit_pcm.wav" \
    --received "$root_dir/public/8_Channel_ID_signed_16_bit_pcm.raw.pcm" \
    --channels 8 --sample-rate 48000 --max-seconds 1 >/dev/null
  rm -f "$tool_bin"
fi
echo "PCM channel analysis tests passed"
