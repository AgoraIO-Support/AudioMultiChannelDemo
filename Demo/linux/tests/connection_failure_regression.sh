#!/usr/bin/env bash
# Run from any directory after building the Linux samples and copying public/ to out/.
# This deliberately uses an invalid App ID and checks the connection-timeout path.
set -u

sample_dir=${1:-"$(cd "$(dirname "$0")/../out" && pwd)"}
if ! cd "$sample_dir"; then
  echo "Cannot access sample directory: $sample_dir" >&2
  exit 1
fi
result_dir=$(mktemp -d "${TMPDIR:-/tmp}/agora-connect-failure.XXXXXX") || exit 1
failed=0

for sample in sample_send_pcm_multiopus sample_send_wav_multiopus sample_send_opus8 sample_send_mkv_multiopus; do
  log="$result_dir/$sample.log"
  timeout 120 "./$sample" --token 00000000000000000000000000000000 \
    --channelId "invalid_cleanup_$$" --userId 4101 >"$log" 2>&1
  status=$?
  if [ "$status" -eq 255 ] && grep -q 'Wait for connection timeout or failure!' "$log"; then
    echo "PASS $sample: timeout reported, exit $status"
  else
    echo "FAIL $sample: exit $status; see $log" >&2
    failed=1
  fi
done

echo "Logs: $result_dir"
exit "$failed"
