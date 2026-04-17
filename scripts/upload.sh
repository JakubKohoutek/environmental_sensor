#!/usr/bin/env bash
set -euo pipefail

FQBN_COMPILE="esp8266:esp8266:d1_mini"
FQBN_UPLOAD="esp8266:esp8266:d1_mini"
BAUD="115200"
SESSION_VAR_NAME="ENV_SENSOR_UPLOAD_PORT"

parent_shell_pid="$(ps -o ppid= -p $$ | tr -d ' ')"
session_key="$(tty | tr '/ ' '__')-${parent_shell_pid}"
session_cache_file="${TMPDIR:-/tmp}/env-sensor-upload-port-${session_key}"

print_usage() {
  cat <<'EOF'
Usage:
  scripts/upload.sh [--compile|-c]

Examples:
  scripts/upload.sh
  scripts/upload.sh -c
  scripts/upload.sh --compile
EOF
}

collect_ports() {
  local detected=()
  while IFS= read -r line; do
    case "$line" in
      /dev/*)
        detected+=("$(printf '%s\n' "$line" | awk '{print $1}')")
        ;;
    esac
  done < <(arduino-cli board list)
  printf '%s\n' "${detected[@]}"
}

is_port_available() {
  local probe="$1"
  shift
  local item
  for item in "$@"; do
    if [ "$item" = "$probe" ]; then
      return 0
    fi
  done
  return 1
}

choose_port_interactively() {
  local ports=("$@")
  local i=0

  echo "Detected devices:" >&2
  while [ "$i" -lt "${#ports[@]}" ]; do
    printf '  %d) %s\n' "$((i + 1))" "${ports[$i]}" >&2
    i=$((i + 1))
  done

  while true; do
    read -r -p "Select device number: " idx
    case "$idx" in
      ''|*[!0-9]*)
        echo "Invalid selection, try again." >&2
        continue
        ;;
    esac

    if [ "$idx" -ge 1 ] && [ "$idx" -le "${#ports[@]}" ]; then
      printf '%s\n' "${ports[$((idx - 1))]}"
      return 0
    fi

    echo "Invalid selection, try again." >&2
  done
}

ports=()
while IFS= read -r port; do
  [ -n "$port" ] && ports+=("$port")
done < <(collect_ports)

do_compile=false
if [ "$#" -gt 1 ]; then
  echo "Too many arguments." >&2
  print_usage >&2
  exit 2
fi

if [ "$#" -eq 1 ]; then
  case "$1" in
    -c|--compile)
      do_compile=true
      ;;
    -h|--help|help)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
fi

if [ "$do_compile" = true ]; then
  echo "Compiling sketch..."
  arduino-cli compile --fqbn "$FQBN_COMPILE" --libraries ~/Documents/Arduino/libraries .
fi

if [ "${#ports[@]}" -eq 0 ]; then
  echo "No upload-capable serial devices detected."
  exit 1
fi

selected_port="${ENV_SENSOR_UPLOAD_PORT:-}"
if [ -z "$selected_port" ] && [ -f "$session_cache_file" ]; then
  selected_port="$(cat "$session_cache_file")"
fi

if [ -n "$selected_port" ] && is_port_available "$selected_port" "${ports[@]}"; then
  echo "Using remembered device from \$$SESSION_VAR_NAME: $selected_port"
else
  selected_port="$(choose_port_interactively "${ports[@]}")"
fi

export ENV_SENSOR_UPLOAD_PORT="$selected_port"
printf '%s\n' "$selected_port" > "$session_cache_file"
echo "Remembered device in \$$SESSION_VAR_NAME for this shell session: $selected_port"

echo "Uploading to ${selected_port}..."
arduino-cli upload --fqbn "$FQBN_UPLOAD" --port "$selected_port" .

echo "Upload complete. Starting serial monitor on ${selected_port} (baud ${BAUD})."
arduino-cli monitor --port "$selected_port" --config "baudrate=${BAUD}"
