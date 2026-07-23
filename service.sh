#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
STATE_FILE="$RUNDIR/state.json"
PID_FILE="$RUNDIR/collector.pid"
LOG_FILE="$RUNDIR/collector.log"
NATIVE_BIN="$MODDIR/bin/chg_daemon"
SHELL_BIN="$MODDIR/collector.sh"

is_running() {
  [ -n "$1" ] && kill -0 "$1" 2>/dev/null
}

write_stub() {
  mode="$1"
  reason="$2"
  timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null)
  [ -n "$timestamp" ] || timestamp=$(date)
  cat > "$STATE_FILE" <<EOF
{"timestamp":"$timestamp","source":{"mode":"$mode","csv_path":""},"device":{"market_name":"N/A","build_id":"N/A","battery_sn":"N/A","manufacture_date":"N/A"},"battery":{"level":null,"voltage_mv":null,"current_ma":null,"temperature_c":null,"cell_power_w":null,"current_mah":null,"remaining_to_full_mah":null,"full_charge_mah":null,"design_capacity_mah":null,"soh_pct":null,"health_pct":null,"locked_mah":null,"locked_pct":null,"chip_soc":null,"gauge_soc":null,"cycle_count":null,"status":"$reason"},"charging":{"usb_online":false,"notify_code":"N/A","charge_type":"N/A","fast_charge_type":"N/A","svooc_flag":"N/A","mmi_status":"N/A","usb_voltage_mv":null,"usb_voltage_max_mv":null,"usb_current_ma":null,"usb_current_source":"N/A","power_w":null,"power_source":"$mode","pps_power_w":null,"eta":"$reason","bdd_voltdiff_trend":"N/A","vbat_voltdiff_mv":null},"thermals":{"battery_c":null,"usb_c":null,"vooc_c":null,"cpu_c":null,"gpu_c":null,"shell_c":null},"statistics":{"samples":0,"current_max_a":null,"current_min_a":null,"current_avg_a":null,"power_max_w":null,"power_min_w":null,"power_avg_w":null},"watchdog":{"offline_samples":0,"capacity_stalled":false,"remaining_seconds":0},"raw":{"service.mode":"$mode","service.reason":"$reason","service.log_file":"$LOG_FILE","service.state_file":"$STATE_FILE","service.module_dir":"$MODDIR"},"csv":{}}
EOF
}

while [ "$(getprop sys.boot_completed 2>/dev/null)" != "1" ]; do
  sleep 3
done

mkdir -p "$RUNDIR"
chmod 0755 "$MODDIR/webroot" "$MODDIR/webroot/assets" 2>/dev/null

if [ -f "$PID_FILE" ] && is_running "$(cat "$PID_FILE" 2>/dev/null)"; then
  exit 0
fi

rm -f "$PID_FILE"
printf '%s\n' "O-Pulse service started" > "$LOG_FILE"
printf '%s\n' "module_dir=$MODDIR" >> "$LOG_FILE"
printf '%s\n' "state_file=$STATE_FILE" >> "$LOG_FILE"

if [ -x "$NATIVE_BIN" ]; then
  printf '%s\n' "collector=native" >> "$LOG_FILE"
  write_stub native-starting "native collector starting"
  (
    "$NATIVE_BIN" --state-file "$STATE_FILE" --interval 3 --offline-interval 5 >> "$LOG_FILE" 2>&1 &
    child_pid=$!
    trap 'kill "$child_pid" 2>/dev/null' TERM INT
    wait "$child_pid"
    exit_code=$?
    printf '%s\n' "native collector exited: $exit_code" >> "$LOG_FILE"
    write_stub native-exited "native collector exited ($exit_code)"
  ) &
  echo $! > "$PID_FILE"
elif [ -f "$SHELL_BIN" ]; then
  printf '%s\n' "collector=shell-fallback" >> "$LOG_FILE"
  write_stub shell-starting "shell collector starting"
  (
    /system/bin/sh "$SHELL_BIN" --state-file "$STATE_FILE" --interval 3 >> "$LOG_FILE" 2>&1 &
    child_pid=$!
    trap 'kill "$child_pid" 2>/dev/null' TERM INT
    wait "$child_pid"
    exit_code=$?
    printf '%s\n' "shell collector exited: $exit_code" >> "$LOG_FILE"
    write_stub shell-exited "shell collector exited ($exit_code)"
  ) &
  echo $! > "$PID_FILE"
else
  printf '%s\n' "collector=missing" >> "$LOG_FILE"
  write_stub collector-missing "collector files are missing"
fi
