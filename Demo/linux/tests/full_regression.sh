#!/usr/bin/env bash
# Full Linux C/C++ media regression. Run from any directory after building.
set -eu

sample_dir=""
if [ "$#" -gt 0 ] && [[ $1 != -* ]]; then
  sample_dir=$1
  shift
fi
sample_dir=${sample_dir:-"$(cd "$(dirname "$0")/../out" && pwd)"}
app_id=${APP_ID:-${APP_TOKEN:-}}
if [ -z "$app_id" ]; then
  echo "Set APP_ID (or APP_TOKEN for compatibility) before running full regression" >&2
  exit 2
fi
exec python3 "$(cd "$(dirname "$0")" && pwd)/full_regression.py" \
  --sample-dir "$sample_dir" --app-id "$app_id" "$@"
