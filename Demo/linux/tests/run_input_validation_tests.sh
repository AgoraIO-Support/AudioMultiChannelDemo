#!/usr/bin/env bash
set -euo pipefail

root_dir=$(cd "$(dirname "$0")/.." && pwd)
out_bin=${TMPDIR:-/tmp}/multichannel_input_validation_test

${CXX:-c++} -std=c++11 -Wall -Wextra -Werror \
  "$root_dir/common/multichannel_input_validation.cpp" \
  "$root_dir/tests/multichannel_input_validation_test.cpp" \
  -o "$out_bin"
"$out_bin"
rm -f "$out_bin"
echo "multichannel input validation tests passed"
