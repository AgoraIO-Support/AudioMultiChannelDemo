#!/usr/bin/env bash
# Requires built samples, a valid Agora token, and a channel that permits one
# broadcaster and one audience user. Run from any directory.
set -u

sample_dir=${1:-"$(cd "$(dirname "$0")/../out" && pwd)"}
app_token=${APP_TOKEN:-}
channel_prefix=${CHANNEL_ID:-"mkv-exit-regression-$$"}
duration=${DURATION_SEC:-5}

if [ -z "$app_token" ]; then
  echo "Set APP_TOKEN to run MKV exit regression" >&2
  exit 2
fi
if [ ! -x "$sample_dir/sample_send_mkv_multiopus" ] ||
   [ ! -x "$sample_dir/sample_receive_av_multiopus" ]; then
  echo "Built MKV sender and AV receiver are required in $sample_dir" >&2
  exit 2
fi

wait_for_exit() {
  local pid=$1
  local tries=0
  while kill -0 "$pid" 2>/dev/null; do
    tries=$((tries + 1))
    if [ "$tries" -ge 80 ]; then return 1; fi
    sleep 0.25
  done
  wait "$pid" 2>/dev/null
  return 0
}

run_case() {
  local name=$1
  local signal_target=$2
  local output_mode=$3
  local work_dir
  work_dir=$(mktemp -d "${TMPDIR:-/tmp}/agora-mkv-exit.${name}.XXXXXX") || return 1
  local channel="${channel_prefix}-${name}"
  local audio_out video_out
  if [ "$output_mode" = "null" ]; then
    audio_out=/dev/null
    video_out=/dev/null
  else
    audio_out="$work_dir/audio"
    video_out="$work_dir/video"
  fi

  (cd "$sample_dir" && ./sample_receive_av_multiopus \
      --token "$app_token" --channelId "$channel" --userId "recv-$name" \
      --numOfChannels 8 --sampleRate 48000 --audioFile "$audio_out" \
      --videoFile "$video_out" >"$work_dir/receiver.log" 2>&1) &
  local receiver_pid=$!
  sleep 2
  (cd "$sample_dir" && ./sample_send_mkv_multiopus \
      --token "$app_token" --channelId "$channel" --userId "send-$name" \
      --videoFile public/VictoriaMulti.mkv >"$work_dir/sender.log" 2>&1) &
  local sender_pid=$!
  sleep "$duration"

  case "$signal_target" in
    sender-int) kill -INT "$sender_pid" 2>/dev/null || true ;;
    sender-term) kill -TERM "$sender_pid" 2>/dev/null || true ;;
    receiver-first)
      kill -TERM "$receiver_pid" 2>/dev/null || true
      wait_for_exit "$receiver_pid" || { echo "FAIL $name: receiver hung" >&2; return 1; }
      kill -TERM "$sender_pid" 2>/dev/null || true
      ;;
    normal) kill -INT "$sender_pid" 2>/dev/null || true ;;
  esac

  if ! wait_for_exit "$sender_pid"; then
    echo "FAIL $name: sender did not exit within 20s" >&2
    kill -KILL "$sender_pid" 2>/dev/null || true
    return 1
  fi
  if kill -0 "$receiver_pid" 2>/dev/null; then
    kill -INT "$receiver_pid" 2>/dev/null || true
  fi
  if ! wait_for_exit "$receiver_pid"; then
    echo "FAIL $name: receiver did not exit within 20s" >&2
    kill -KILL "$receiver_pid" 2>/dev/null || true
    return 1
  fi

  echo "PASS $name ($output_mode) logs=$work_dir"
  return 0
}

failed=0
run_case normal normal file || failed=1
run_case sender-int sender-int file || failed=1
run_case sender-term sender-term null || failed=1
run_case receiver-first receiver-first null || failed=1
exit "$failed"
