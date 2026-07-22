#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
STATE_FILE="$RUNDIR/state.json"
INTERVAL=10
OFFLINE_INTERVAL=10
CAPACITY_TIMEOUT=123
CELL_TYPE=1

read_node() {
  if [ -n "$1" ] && [ -r "$1" ]; then
    value=$(cat "$1" 2>/dev/null)
    if [ -n "$value" ]; then
      printf '%s' "$value"
      return
    fi
  fi
  printf '%s' "$2"
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

json_string() {
  printf '"%s"' "$(json_escape "$1")"
}

json_number() {
  if [ -z "$1" ] || [ "$1" = "null" ]; then
    printf 'null'
  else
    printf '%s' "$1"
  fi
}

json_bool() {
  if [ "$1" = "1" ]; then
    printf 'true'
  else
    printf 'false'
  fi
}

append_string_pair() {
  target="$1"
  first_name="$2"
  key="$3"
  value="$4"
  eval "current=\${$target}"
  eval "first=\${$first_name}"
  if [ "$first" = "1" ]; then
    current=""
  else
    current="$current,"
  fi
  current="$current$(json_string "$key"):$(json_string "$value")"
  eval "$target=\$current"
  eval "$first_name=0"
}

scaled_num() {
  awk -v v="$1" -v d="$2" 'BEGIN { if (v == "" || v == "N/A" || v == "--") print "null"; else printf "%.2f", v / d }'
}

abs_scaled_num() {
  awk -v v="$1" -v m="$2" -v d="$3" 'BEGIN { if (v == "" || v == "N/A" || v == "--") print "null"; else { if (v < 0) v = -v; printf "%.2f", v * m / d } }'
}

safe_percent() {
  awk -v a="$1" -v b="$2" 'BEGIN { if (a == "" || b == "" || b == 0) print "null"; else printf "%.2f", (a * 100) / b }'
}

floor_percent() {
  awk -v a="$1" -v b="$2" 'BEGIN { if (a == "" || b == "" || b == 0) print "null"; else printf "%.0f", (a * 100) / b }'
}

safe_diff() {
  awk -v a="$1" -v b="$2" 'BEGIN { if (a == "" || b == "") print "null"; else { v = a - b; if (v < 0) v = 0; printf "%.2f", v } }'
}

product_or_null() {
  awk -v a="$1" -v b="$2" 'BEGIN { if (a == "null" || b == "null" || a == "" || b == "") print "null"; else printf "%.2f", a * b }'
}

ge_value() {
  awk -v v="$1" -v c="$2" 'BEGIN { exit !(v != "null" && v + 0 >= c) }'
}

gt_value() {
  awk -v v="$1" -v c="$2" 'BEGIN { exit !(v != "null" && v + 0 > c) }'
}

is_active_signal() {
  [ -n "$1" ] && [ "$1" != "0" ] && [ "$1" != "--" ] && [ "$1" != "N/A" ]
}

discover_thermal() {
  keyword="$1"
  for dir in /sys/devices/virtual/thermal/thermal_zone*; do
    [ -d "$dir" ] || continue
    type=$(read_node "$dir/type" "")
    case "$type" in
      *"$keyword"*)
        printf '%s/temp' "$dir"
        return
        ;;
    esac
  done
}

thermal_celsius() {
  awk -v v="$1" 'BEGIN { if (v == "" || v == "N/A" || v == "--") print "null"; else if (v > 1000 || v < -1000) printf "%.2f", v / 1000; else printf "%.2f", v / 100 }'
}

input_current_amperes() {
  awk -v v="$1" 'BEGIN {
    if (v == "" || v == "N/A" || v == "--") print "null";
    else {
      if (v < 0) v = -v;
      if (v >= 10000) printf "%.2f", v / 1000000;
      else printf "%.2f", v / 1000;
    }
  }'
}

input_current_unit() {
  awk -v v="$1" 'BEGIN {
    if (v == "" || v == "N/A" || v == "--") print "unavailable";
    else {
      if (v < 0) v = -v;
      if (v >= 10000) print "ua(auto)";
      else print "ma(auto)";
    }
  }'
}

pps_power_w() {
  awk -v v="$1" 'BEGIN {
    if (v == "" || v == "N/A" || v == "--" || v + 0 <= 0) print "null";
    else if (v > 200000) printf "%.2f", v / 1000000;
    else if (v > 1000) printf "%.2f", v / 1000;
    else printf "%.2f", v + 0;
  }'
}

pps_power_unit() {
  awk -v v="$1" 'BEGIN {
    if (v == "" || v == "N/A" || v == "--" || v + 0 <= 0) print "unavailable";
    else if (v > 200000) print "uw(auto)";
    else if (v > 1000) print "mw(auto)";
    else print "w(auto)";
  }'
}

find_latest_csv() {
  latest=""
  for file in /data/vendor/battery/battery-log-*.csv; do
    [ -f "$file" ] || continue
    latest="$file"
    break
  done
  if [ -n "$latest" ]; then
    ls -t /data/vendor/battery/battery-log-*.csv 2>/dev/null | head -n 1
  fi
}

parse_csv_pairs() {
  awk -F, '
    NR == 1 {
      for (i = 1; i <= NF; ++i) {
        key = $i
        gsub(/[^A-Za-z0-9_]/, "", key)
        headers[i] = key
      }
      next
    }
    NF > 0 {
      last_count = NF
      for (i = 1; i <= NF; ++i) values[i] = $i
    }
    END {
      for (i = 1; i <= last_count; ++i) {
        if (headers[i] != "") printf "%s\t%s\n", headers[i], values[i]
      }
    }
  ' "$1" 2>/dev/null
}

while [ $# -gt 0 ]; do
  case "$1" in
    --state-file)
      STATE_FILE="$2"
      shift 2
      ;;
    --interval)
      INTERVAL="$2"
      shift 2
      ;;
    --offline-interval)
      OFFLINE_INTERVAL="$2"
      shift 2
      ;;
    --capacity-timeout)
      CAPACITY_TIMEOUT="$2"
      shift 2
      ;;
    --cell-type)
      CELL_TYPE="$2"
      shift 2
      ;;
    *)
      shift
      ;;
  esac
done

mkdir -p "$RUNDIR"

USB_THERMAL=$(discover_thermal "usb")
VOOC_THERMAL=$(discover_thermal "svooc_mos_btb_usr")
CPU_THERMAL=$(discover_thermal "cpu-1-0-usr")
[ -n "$CPU_THERMAL" ] || CPU_THERMAL=$(discover_thermal "cpu-0-0-usr")
GPU_THERMAL=$(discover_thermal "gpu-usr")
SHELL_THERMAL=$(discover_thermal "shell_front")
[ -n "$SHELL_THERMAL" ] || SHELL_THERMAL=$(discover_thermal "quiet_therm")

offline_samples=0
sample_count=0
sum_current=0
sum_power=0
max_current=
min_current=
max_power=
min_power=
last_rm=
changed_at=$(date +%s)

echo "O-Pulse shell collector: started; interval=${INTERVAL}s, offline_interval=${OFFLINE_INTERVAL}s"

while true; do
  timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null)
  [ -n "$timestamp" ] || timestamp=$(date)
  now_epoch=$(date +%s)

  market_name=$(getprop ro.vendor.oplus.market.name 2>/dev/null)
  build_id=$(getprop ro.build.display.id 2>/dev/null)
  battery_sn=$(read_node /sys/class/oplus_chg/battery/battery_sn "N/A")
  manufacture_date=$(read_node /sys/class/oplus_chg/battery/battery_manu_date "N/A")
  usb_online_raw=$(read_node /sys/class/power_supply/usb/online "0")
  battery_notify_code=$(read_node /sys/class/oplus_chg/battery/battery_notify_code "0")
  charge_type=$(read_node /sys/class/power_supply/battery/charge_type "N/A")
  fast_chg_type=$(read_node /sys/class/oplus_chg/battery/fast_chg_type "0")
  svooc_flag=$(read_node /sys/class/oplus_chg/battery/svooc_flag "0")
  chg_mmi_status=$(read_node /sys/class/oplus_chg/battery/chg_mmi_status "0")
  capacity=$(read_node /sys/class/power_supply/battery/capacity "")
  chip_soc=$(read_node /sys/class/oplus_chg/battery/chip_soc "--")
  gauge_soc=$(read_node /sys/class/oplus_chg/battery/gauge_soc "--")
  battery_rm=$(read_node /sys/class/oplus_chg/battery/battery_rm "0")
  battery_fcc=$(read_node /sys/class/oplus_chg/battery/battery_fcc "0")
  design_capacity=$(read_node /sys/class/oplus_chg/battery/design_capacity "0")
  battery_soh=$(read_node /sys/class/oplus_chg/battery/battery_soh "0")
  battery_cc=$(read_node /sys/class/oplus_chg/battery/battery_cc "--")
  usb_voltage_max_raw=$(read_node /sys/class/power_supply/usb/voltage_max "0")
  battery_voltage_raw=$(read_node /sys/class/power_supply/battery/voltage_now "0")
  usb_voltage_raw=$(read_node /sys/class/power_supply/usb/voltage_now "0")
  battery_current_raw=$(read_node /sys/class/power_supply/battery/current_now "0")
  usb_current_raw=$(read_node /sys/class/power_supply/usb/current_now "0")
  ppschg_power_raw=$(read_node /sys/devices/virtual/oplus_chg/battery/ppschg_power "0")
  bdd_voltdiff_trend=$(read_node /sys/class/oplus_chg/battery/bdd_voltdiff_trend "")
  vbat_voltdiff=$(read_node /sys/class/oplus_chg/battery/vbat_voltdiff "0")
  battery_temp_raw=$(read_node /sys/class/power_supply/battery/temp "0")
  usb_thermal_raw=$(read_node "$USB_THERMAL" "0")
  vooc_thermal_raw=$(read_node "$VOOC_THERMAL" "0")
  cpu_thermal_raw=$(read_node "$CPU_THERMAL" "0")
  gpu_thermal_raw=$(read_node "$GPU_THERMAL" "0")
  shell_thermal_raw=$(read_node "$SHELL_THERMAL" "0")

  if [ -z "$capacity" ] && [ "$battery_fcc" != "0" ] && [ "$battery_rm" != "0" ]; then
    capacity=$(floor_percent "$battery_rm" "$battery_fcc")
  fi

  usb_voltage_v=$(scaled_num "$usb_voltage_raw" 1000000)
  usb_voltage_max_v=$(scaled_num "$usb_voltage_max_raw" 1000000)
  battery_voltage_v=$(scaled_num "$battery_voltage_raw" 1000000)
  battery_current_a=$(abs_scaled_num "$battery_current_raw" "$([ "$CELL_TYPE" = "1" ] && echo 2 || echo 1)" 1000)
  input_current_a=$(input_current_amperes "$usb_current_raw")
  input_current_unit_value=$(input_current_unit "$usb_current_raw")
  input_current_source="/sys/class/power_supply/usb/current_now"
  cell_power_w=$(product_or_null "$battery_voltage_v" "$battery_current_a")
  pps_power_value=$(pps_power_w "$ppschg_power_raw")
  pps_power_unit_value=$(pps_power_unit "$ppschg_power_raw")

  usb_voltage_present=0
  if ge_value "$usb_voltage_v" 4; then
    usb_voltage_present=1
  fi

  usb_protocol_active=0
  if is_active_signal "$fast_chg_type" || is_active_signal "$svooc_flag" || is_active_signal "$chg_mmi_status"; then
    usb_protocol_active=1
  fi

  usb_online=0
  if [ "$usb_online_raw" = "1" ] && { [ "$usb_voltage_present" = "1" ] || [ "$usb_protocol_active" = "1" ]; }; then
    usb_online=1
  fi

  power_w="null"
  power_source="unavailable"
  if ge_value "$pps_power_value" 0.5 && awk -v v="$pps_power_value" 'BEGIN { exit !(v <= 300) }'; then
    power_w="$pps_power_value"
    power_source="ppschg_power"
  elif gt_value "$usb_voltage_v" 0 && gt_value "$input_current_a" 0; then
    power_w=$(product_or_null "$usb_voltage_v" "$input_current_a")
    power_source="usb_voltage_current"
  elif [ "$cell_power_w" != "null" ]; then
    power_w="$cell_power_w"
    power_source="battery_cell_estimate"
  fi

  battery_temp_c=$(scaled_num "$battery_temp_raw" 10)
  usb_temp_c=$(thermal_celsius "$usb_thermal_raw")
  vooc_temp_c=$(thermal_celsius "$vooc_thermal_raw")
  cpu_temp_c=$(thermal_celsius "$cpu_thermal_raw")
  gpu_temp_c=$(thermal_celsius "$gpu_thermal_raw")
  shell_temp_c=$(thermal_celsius "$shell_thermal_raw")

  remaining_to_full_mah=$(safe_diff "$battery_fcc" "$battery_rm")
  locked_mah="$remaining_to_full_mah"
  locked_pct=$(safe_percent "$locked_mah" "$battery_fcc")
  health_pct=$(floor_percent "$battery_fcc" "$design_capacity")

  full=0
  [ "$battery_notify_code" != "0" ] && full=1

  if [ "$usb_online" = "1" ] && [ "$full" = "0" ] && [ "$remaining_to_full_mah" != "null" ] && ge_value "$battery_current_a" 0.1; then
    eta_minutes=$(awk -v rem="$remaining_to_full_mah" -v curr="$battery_current_a" 'BEGIN { printf "%.0f", (rem / (curr * 1000)) * 60 }')
    if [ "$eta_minutes" -ge 60 ]; then
      eta_text="$((eta_minutes / 60))h $((eta_minutes % 60))m"
    else
      eta_text="${eta_minutes}m"
    fi
  elif [ "$usb_online" = "1" ] && [ "$full" = "0" ]; then
    eta_text="trickle charging"
  elif [ "$full" = "1" ]; then
    eta_text="fully charged"
  else
    eta_text="charger disconnected"
  fi

  if [ "$usb_online" = "1" ]; then
    offline_samples=0
  else
    offline_samples=$((offline_samples + 1))
  fi

  if [ -n "$battery_rm" ] && [ "$battery_rm" != "$last_rm" ]; then
    changed_at="$now_epoch"
    last_rm="$battery_rm"
  fi

  elapsed=$((now_epoch - changed_at))
  remaining_seconds=$((CAPACITY_TIMEOUT - elapsed))
  if [ "$remaining_seconds" -lt 0 ]; then
    remaining_seconds=0
  fi
  capacity_stalled=0
  if [ "$elapsed" -ge "$CAPACITY_TIMEOUT" ]; then
    capacity_stalled=1
  fi

  if [ "$battery_current_a" != "null" ] && [ "$power_w" != "null" ]; then
    if [ "$sample_count" -eq 0 ]; then
      max_current="$battery_current_a"
      min_current="$battery_current_a"
      max_power="$power_w"
      min_power="$power_w"
    else
      awk -v a="$battery_current_a" -v b="$max_current" 'BEGIN { exit !(a > b) }' && max_current="$battery_current_a"
      awk -v a="$battery_current_a" -v b="$min_current" 'BEGIN { exit !(a < b) }' && min_current="$battery_current_a"
      awk -v a="$power_w" -v b="$max_power" 'BEGIN { exit !(a > b) }' && max_power="$power_w"
      awk -v a="$power_w" -v b="$min_power" 'BEGIN { exit !(a < b) }' && min_power="$power_w"
    fi
    sum_current=$(awk -v a="$sum_current" -v b="$battery_current_a" 'BEGIN { printf "%.4f", a + b }')
    sum_power=$(awk -v a="$sum_power" -v b="$power_w" 'BEGIN { printf "%.4f", a + b }')
    sample_count=$((sample_count + 1))
  fi

  if [ "$sample_count" -gt 0 ]; then
    avg_current=$(awk -v s="$sum_current" -v c="$sample_count" 'BEGIN { printf "%.2f", s / c }')
    avg_power=$(awk -v s="$sum_power" -v c="$sample_count" 'BEGIN { printf "%.2f", s / c }')
  else
    avg_current="null"
    avg_power="null"
  fi

  if [ "$usb_online" = "1" ]; then
    battery_status=$([ "$full" = "1" ] && printf 'Charged' || printf 'Charging')
  else
    battery_status="Not charging"
  fi

  csv_path=$(find_latest_csv)
  CSV_JSON=""
  CSV_FIRST=1
  CSV_RAW_EXTRA=""
  if [ -n "$csv_path" ] && [ -f "$csv_path" ]; then
    csv_lines=$(parse_csv_pairs "$csv_path")
    old_ifs=$IFS
    IFS='
'
    for line in $csv_lines; do
      key=${line%%	*}
      value=${line#*	}
      [ -n "$key" ] || continue
      append_string_pair CSV_JSON CSV_FIRST "$key" "$value"
      CSV_RAW_EXTRA="$CSV_RAW_EXTRA,$(json_string "csv.$key"):$(json_string "$value")"
    done
    IFS=$old_ifs
  fi

  RAW_JSON=""
  RAW_FIRST=1
  append_string_pair RAW_JSON RAW_FIRST "device_name" "$market_name"
  append_string_pair RAW_JSON RAW_FIRST "build_id" "$build_id"
  append_string_pair RAW_JSON RAW_FIRST "battery_sn" "$battery_sn"
  append_string_pair RAW_JSON RAW_FIRST "battery_manu_date" "$manufacture_date"
  append_string_pair RAW_JSON RAW_FIRST "usb_online" "$usb_online_raw"
  append_string_pair RAW_JSON RAW_FIRST "battery_notify_code" "$battery_notify_code"
  append_string_pair RAW_JSON RAW_FIRST "charge_type" "$charge_type"
  append_string_pair RAW_JSON RAW_FIRST "fast_chg_type" "$fast_chg_type"
  append_string_pair RAW_JSON RAW_FIRST "svooc_flag" "$svooc_flag"
  append_string_pair RAW_JSON RAW_FIRST "chg_mmi_status" "$chg_mmi_status"
  append_string_pair RAW_JSON RAW_FIRST "capacity" "$capacity"
  append_string_pair RAW_JSON RAW_FIRST "chip_soc" "$chip_soc"
  append_string_pair RAW_JSON RAW_FIRST "gauge_soc" "$gauge_soc"
  append_string_pair RAW_JSON RAW_FIRST "battery_rm" "$battery_rm"
  append_string_pair RAW_JSON RAW_FIRST "battery_fcc" "$battery_fcc"
  append_string_pair RAW_JSON RAW_FIRST "design_capacity" "$design_capacity"
  append_string_pair RAW_JSON RAW_FIRST "battery_soh" "$battery_soh"
  append_string_pair RAW_JSON RAW_FIRST "battery_cc" "$battery_cc"
  append_string_pair RAW_JSON RAW_FIRST "usb_voltage_max" "$usb_voltage_max_raw"
  append_string_pair RAW_JSON RAW_FIRST "battery_voltage_now" "$battery_voltage_raw"
  append_string_pair RAW_JSON RAW_FIRST "usb_voltage_now" "$usb_voltage_raw"
  append_string_pair RAW_JSON RAW_FIRST "battery_current_now" "$battery_current_raw"
  append_string_pair RAW_JSON RAW_FIRST "usb_current_now" "$usb_current_raw"
  append_string_pair RAW_JSON RAW_FIRST "ppschg_power" "$ppschg_power_raw"
  append_string_pair RAW_JSON RAW_FIRST "bdd_voltdiff_trend" "$bdd_voltdiff_trend"
  append_string_pair RAW_JSON RAW_FIRST "vbat_voltdiff" "$vbat_voltdiff"
  append_string_pair RAW_JSON RAW_FIRST "battery_temp" "$battery_temp_raw"
  append_string_pair RAW_JSON RAW_FIRST "thermal_usb_path" "$USB_THERMAL"
  append_string_pair RAW_JSON RAW_FIRST "thermal_vooc_path" "$VOOC_THERMAL"
  append_string_pair RAW_JSON RAW_FIRST "thermal_cpu_path" "$CPU_THERMAL"
  append_string_pair RAW_JSON RAW_FIRST "thermal_gpu_path" "$GPU_THERMAL"
  append_string_pair RAW_JSON RAW_FIRST "thermal_shell_path" "$SHELL_THERMAL"
  append_string_pair RAW_JSON RAW_FIRST "thermal_usb_raw" "$usb_thermal_raw"
  append_string_pair RAW_JSON RAW_FIRST "thermal_vooc_raw" "$vooc_thermal_raw"
  append_string_pair RAW_JSON RAW_FIRST "thermal_cpu_raw" "$cpu_thermal_raw"
  append_string_pair RAW_JSON RAW_FIRST "thermal_gpu_raw" "$gpu_thermal_raw"
  append_string_pair RAW_JSON RAW_FIRST "thermal_shell_raw" "$shell_thermal_raw"
  append_string_pair RAW_JSON RAW_FIRST "usb_online_standard" "$usb_online_raw"
  append_string_pair RAW_JSON RAW_FIRST "usb_voltage_present" "$usb_voltage_present"
  append_string_pair RAW_JSON RAW_FIRST "usb_protocol_active" "$usb_protocol_active"
  append_string_pair RAW_JSON RAW_FIRST "input_current_source" "$input_current_source"
  append_string_pair RAW_JSON RAW_FIRST "input_current_unit" "$input_current_unit_value"
  append_string_pair RAW_JSON RAW_FIRST "ppschg_power_unit" "$pps_power_unit_value"

  battery_voltage_mv=$(product_or_null "$battery_voltage_v" 1000)
  battery_current_ma=$(product_or_null "$battery_current_a" 1000)
  usb_voltage_mv=$(product_or_null "$usb_voltage_v" 1000)
  usb_voltage_max_mv=$(product_or_null "$usb_voltage_max_v" 1000)
  usb_current_ma=$(product_or_null "$input_current_a" 1000)

  json=$(cat <<EOF
{"timestamp":$(json_string "$timestamp"),"source":{"mode":"live-shell","csv_path":$(json_string "$csv_path")},"device":{"market_name":$(json_string "$market_name"),"build_id":$(json_string "$build_id"),"battery_sn":$(json_string "$battery_sn"),"manufacture_date":$(json_string "$manufacture_date")},"battery":{"level":$(json_number "$capacity"),"voltage_mv":$(json_number "$battery_voltage_mv"),"current_ma":$(json_number "$battery_current_ma"),"temperature_c":$(json_number "$battery_temp_c"),"cell_power_w":$(json_number "$cell_power_w"),"current_mah":$(json_number "$battery_rm"),"remaining_to_full_mah":$(json_number "$remaining_to_full_mah"),"full_charge_mah":$(json_number "$battery_fcc"),"design_capacity_mah":$(json_number "$design_capacity"),"soh_pct":$(json_number "$battery_soh"),"health_pct":$(json_number "$health_pct"),"locked_mah":$(json_number "$locked_mah"),"locked_pct":$(json_number "$locked_pct"),"chip_soc":$(json_number "$chip_soc"),"gauge_soc":$(json_number "$gauge_soc"),"cycle_count":$(json_number "$battery_cc"),"status":$(json_string "$battery_status")},"charging":{"usb_online":$(json_bool "$usb_online"),"notify_code":$(json_string "$battery_notify_code"),"charge_type":$(json_string "$charge_type"),"fast_charge_type":$(json_string "$fast_chg_type"),"svooc_flag":$(json_string "$svooc_flag"),"mmi_status":$(json_string "$chg_mmi_status"),"usb_voltage_mv":$(json_number "$usb_voltage_mv"),"usb_voltage_max_mv":$(json_number "$usb_voltage_max_mv"),"usb_current_ma":$(json_number "$usb_current_ma"),"usb_current_source":$(json_string "$input_current_source"),"power_w":$(json_number "$power_w"),"power_source":$(json_string "$power_source"),"pps_power_w":$(json_number "$pps_power_value"),"eta":$(json_string "$eta_text"),"bdd_voltdiff_trend":$(json_string "$bdd_voltdiff_trend"),"vbat_voltdiff_mv":$(json_number "$vbat_voltdiff")},"thermals":{"battery_c":$(json_number "$battery_temp_c"),"usb_c":$(json_number "$usb_temp_c"),"vooc_c":$(json_number "$vooc_temp_c"),"cpu_c":$(json_number "$cpu_temp_c"),"gpu_c":$(json_number "$gpu_temp_c"),"shell_c":$(json_number "$shell_temp_c")},"statistics":{"samples":$sample_count,"current_max_a":$(json_number "$max_current"),"current_min_a":$(json_number "$min_current"),"current_avg_a":$(json_number "$avg_current"),"power_max_w":$(json_number "$max_power"),"power_min_w":$(json_number "$min_power"),"power_avg_w":$(json_number "$avg_power")},"watchdog":{"offline_samples":$offline_samples,"capacity_stalled":$(json_bool "$capacity_stalled"),"remaining_seconds":$remaining_seconds},"raw":{$RAW_JSON$CSV_RAW_EXTRA},"csv":{$CSV_JSON}}
EOF
)

  tmp_file="${STATE_FILE}.tmp"
  if printf '%s' "$json" > "$tmp_file" 2>/dev/null; then
    mv "$tmp_file" "$STATE_FILE"
  fi

  if [ "$usb_online" = "1" ]; then
    sleep "$INTERVAL"
  else
    sleep "$OFFLINE_INTERVAL"
  fi
done
