#!/usr/bin/env bash
# Run from any directory on Linux after building the PCM sender and receiver.
# APP_TOKEN is the value accepted by the samples' --token argument (an App ID
# for projects without certificate authentication). Logs and PCM are retained.
set -eu

sample_dir=${1:-"$(cd "$(dirname "$0")/../out" && pwd)"}
app_token=${APP_TOKEN:-}
channel=${CHANNEL_ID:-"lifecycle-$$-$(date +%s)"}
sender_uid=${SENDER_UID:-71001}
receiver_uid=${RECEIVER_UID:-71002}
cycles=${CYCLES:-3}
timeout_sec=${TIMEOUT_SEC:-20}
settle_sec=${SETTLE_SEC:-4}
observe_sec=${OBSERVE_SEC:-2}
recovery_media_sec=${RECOVERY_MEDIA_SEC:-2}
min_growth_bytes=${MIN_GROWTH_BYTES:-38400}
pcm_input=${PCM_INPUT:-"$sample_dir/public/8_Channel_ID_signed_16_bit_pcm.raw.pcm"}

if [ -z "$app_token" ]; then echo 'Set APP_TOKEN before running the regression' >&2; exit 2; fi
for n in "$sender_uid" "$receiver_uid" "$cycles" "$timeout_sec" "$settle_sec" "$observe_sec" "$recovery_media_sec" "$min_growth_bytes"; do
  case "$n" in ''|*[!0-9]*) echo 'UIDs and timing/cycle options must be positive integers' >&2; exit 2;; esac
  if [ "$n" -eq 0 ]; then echo 'UIDs and timing/cycle options must be positive integers' >&2; exit 2; fi
done
if [ "$sender_uid" = "$receiver_uid" ]; then echo 'Sender and receiver UIDs must differ' >&2; exit 2; fi
for f in "$sample_dir/sample_send_pcm_multiopus" "$sample_dir/sample_receive_pcm_multiopus"; do
  if [ ! -x "$f" ]; then echo "Missing executable: $f" >&2; exit 2; fi
done
if [ ! -r "$pcm_input" ]; then echo "Missing PCM input: $pcm_input" >&2; exit 2; fi
sample_dir=$(cd "$sample_dir" && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/agora-lifecycle.XXXXXX")
recovery_log=${RECOVERY_LOG:-"$work_dir/recovery-offsets.txt"}
: >"$recovery_log"
sender_pid='' receiver_pid=''
echo "Artifacts: $work_dir"

cleanup() {
  local pid
  trap - EXIT INT TERM
  for pid in "$sender_pid" "$receiver_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid" 2>/dev/null || :; fi
  done
  for pid in "$sender_pid" "$receiver_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      (sleep 5; kill -KILL "$pid" 2>/dev/null || :) &
      local watchdog=$!
      wait "$pid" 2>/dev/null || :
      kill "$watchdog" 2>/dev/null || :
      wait "$watchdog" 2>/dev/null || :
    fi
  done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
fail() { echo "FAIL: $* (artifacts: $work_dir)" >&2; exit 1; }
count() { grep -F -c -- "$2" "$1" 2>/dev/null || :; }
wait_log() {
  local file=$1 pattern=$2 expected=$3 pid=$4 start=$SECONDS found
  while :; do
    found=$(count "$file" "$pattern"); found=${found:-0}
    if [ "$found" -ge "$expected" ]; then return; fi
    kill -0 "$pid" 2>/dev/null || fail "process exited waiting for $pattern in $file"
    [ $((SECONDS - start)) -lt "$timeout_sec" ] || fail "timeout waiting for $pattern in $file"
    sleep 0.2
  done
}
size() { if [ -f "$1" ]; then wc -c < "$1" | tr -d ' '; else printf '0'; fi; }
mark_recovery() { printf '%s %s\n' "$1" "$2" >>"$recovery_log"; }
wait_growth() {
  local file=$1 before=$2 start=$SECONDS
  while [ "$(size "$file")" -lt $((before + min_growth_bytes)) ]; do
    if ! kill -0 "$sender_pid" 2>/dev/null || ! kill -0 "$receiver_pid" 2>/dev/null; then
      fail 'a participant exited while waiting for PCM'
    fi
    [ $((SECONDS - start)) -lt "$timeout_sec" ] || fail "PCM did not grow: $file"
    sleep 0.2
  done
}
assert_stopped() {
  local file=$1 first second
  sleep "$settle_sec"
  first=$(size "$file")
  sleep "$observe_sec"
  second=$(size "$file")
  [ "$first" = "$second" ] || fail "PCM kept growing while stopped: $first -> $second"
}
stop_process() {
  local pid=$1 name=$2 start=$SECONDS status=0
  kill -TERM "$pid" 2>/dev/null || fail "$name already exited"
  while kill -0 "$pid" 2>/dev/null; do
    [ $((SECONDS - start)) -lt "$timeout_sec" ] || fail "$name did not exit"
    sleep 0.2
  done
  wait "$pid" || status=$?
  [ "$status" -eq 0 ] || fail "$name exit status $status"
}
start_receiver() {
  local run=$1
  receiver_log="$work_dir/receiver-$run.log"
  pcm="$work_dir/receiver-$run.pcm.uid-$sender_uid"
  mkdir -p "$work_dir/sdk-receiver-$run"
  (cd "$sample_dir" && AGORA_LOG_PATH="$work_dir/sdk-receiver-$run/agorasdk.log" exec ./sample_receive_pcm_multiopus --token "$app_token" --channelId "$channel" --userId "$receiver_uid" --remoteUserId "$sender_uid" --audioFile "$work_dir/receiver-$run.pcm" --numOfChannels 8 --sampleRate 48000 --lifecycleControl 1) >"$receiver_log" 2>&1 &
  receiver_pid=$!
  wait_log "$receiver_log" 'Start receiving multi-channel audio data' 1 "$receiver_pid"
}
start_sender() {
  local run=$1
  sender_log="$work_dir/sender-$run.log"
  mkdir -p "$work_dir/sdk-sender-$run"
  (cd "$sample_dir" && AGORA_LOG_PATH="$work_dir/sdk-sender-$run/agorasdk.log" exec ./sample_send_pcm_multiopus --token "$app_token" --channelId "$channel" --userId "$sender_uid" --audioFile "$pcm_input" --numOfChannels 8 --sampleRate 48000 --lifecycleControl 1) >"$sender_log" 2>&1 &
  sender_pid=$!
  wait_log "$sender_log" 'Start sending multi-channel audio data' 1 "$sender_pid"
}
toggle() {
  local pid=$1 log=$2 pattern=$3 expected=$4
  kill -USR1 "$pid" 2>/dev/null || fail "cannot signal $pid"
  wait_log "$log" "$pattern" "$expected" "$pid"
}

start_receiver 1
start_sender 1
wait_log "$receiver_log" "onUserAudioTrackSubscribed: userId $sender_uid" 1 "$receiver_pid"
before=0
wait_growth "$pcm" "$before"
sleep "$recovery_media_sec"
mark_recovery initial "$before"
for ((i=1; i<=cycles; i++)); do
  toggle "$sender_pid" "$sender_log" 'LIFECYCLE publish=0 ret=0' "$i"
  assert_stopped "$pcm"
  before=$(size "$pcm")
  toggle "$sender_pid" "$sender_log" 'LIFECYCLE publish=1 ret=0' "$i"
  wait_growth "$pcm" "$before"
  sleep "$recovery_media_sec"
  mark_recovery "republish-$i" "$before"
  toggle "$receiver_pid" "$receiver_log" 'LIFECYCLE subscribe=0 ret=0' "$i"
  assert_stopped "$pcm"
  before=$(size "$pcm")
  toggle "$receiver_pid" "$receiver_log" 'LIFECYCLE subscribe=1 ret=0' "$i"
  wait_growth "$pcm" "$before"
  sleep "$recovery_media_sec"
  mark_recovery "resubscribe-$i" "$before"
  echo "PASS publish/subscribe cycle $i"
done

stop_process "$sender_pid" sender
sender_pid=
wait_log "$receiver_log" "onUserLeft: userId $sender_uid" 1 "$receiver_pid"
assert_stopped "$pcm"
before=$(size "$pcm")
start_sender 2
wait_growth "$pcm" "$before"
sleep "$recovery_media_sec"
mark_recovery sender-rejoin "$before"
echo 'PASS sender leave/rejoin with same UID'

stop_process "$receiver_pid" receiver
receiver_pid=
start_receiver 2
wait_log "$receiver_log" "onUserAudioTrackSubscribed: userId $sender_uid" 1 "$receiver_pid"
before=0
wait_growth "$pcm" "$before"
sleep "$recovery_media_sec"
mark_recovery receiver-rejoin "$before"
echo 'PASS receiver leave/rejoin with same UID'

# Exit immediately after a control signal to exercise concurrent teardown.
kill -USR1 "$receiver_pid" 2>/dev/null || fail 'cannot signal receiver'
stop_process "$receiver_pid" receiver
receiver_pid=
kill -USR1 "$sender_pid" 2>/dev/null || fail 'cannot signal sender'
stop_process "$sender_pid" sender
sender_pid=
echo 'PASS rapid control/signal exit'
if grep -E '\[ APP_LOG_ERROR \]|spdlog fatal error' "$work_dir"/sender-*.log "$work_dir"/receiver-*.log; then
  fail 'participant logs contain an application or SDK logging error'
fi
echo "PASS lifecycle regression; artifacts: $work_dir"
