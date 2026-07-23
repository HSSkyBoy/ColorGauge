#!/system/bin/sh

# O-Pulse Shell collector. The Kotlin App launches this script with Root.

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
STATE_FILE="$RUNDIR/state.json"
INTERVAL=3
CELL_TYPE=1

read_node() {
  value=$(cat "$1" 2>/dev/null)
  [ -n "$value" ] && printf '%s' "$value" || printf '%s' "${2:-N/A}"
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/[[:cntrl:]]/ /g'
}

json_string() { printf '"%s"' "$(json_escape "$1")"; }

json_num() {
  awk -v value="$1" 'BEGIN {
    if (value == "" || value == "N/A" || value == "--") print "null";
    else if (value ~ /^[-+]?[0-9]+([.][0-9]+)?$/) print value;
    else print "null";
  }'
}

scale() {
  awk -v value="$1" -v divisor="$2" 'BEGIN {
    if (value == "" || value == "N/A" || value == "--") print "null";
    else printf "%.2f", value / divisor;
  }'
}

absolute_current() {
  awk -v value="$1" -v cells="$2" 'BEGIN {
    if (value == "" || value == "N/A" || value == "--") print "null";
    else { if (value < 0) value = -value; printf "%.2f", value * cells / 1000; }
  }'
}

usb_current() {
  awk -v value="$1" 'BEGIN {
    if (value == "" || value == "N/A" || value == "--") print "null";
    else { if (value < 0) value = -value; if (value >= 10000) printf "%.2f", value / 1000000; else printf "%.2f", value / 1000; }
  }'
}

multiply() {
  awk -v left="$1" -v right="$2" 'BEGIN {
    if (left == "null" || right == "null" || left == "" || right == "") print "null";
    else printf "%.2f", left * right;
  }'
}

percent() {
  awk -v left="$1" -v right="$2" 'BEGIN {
    if (left == "" || right == "" || right == 0 || left == "N/A" || right == "N/A") print "null";
    else printf "%.2f", left * 100 / right;
  }'
}

thermal_celsius() {
  awk -v value="$1" 'BEGIN {
    if (value == "" || value == "0") print "null";
    else if (value > 1000 || value < -1000) printf "%.2f", value / 1000;
    else printf "%.2f", value / 100;
  }'
}

thermal_path() {
  for zone in /sys/devices/virtual/thermal/thermal_zone*; do
    [ -d "$zone" ] || continue
    type=$(cat "$zone/type" 2>/dev/null)
    case "$type" in
      *"$1"*) printf '%s/temp' "$zone"; return ;;
    esac
  done
}

while [ $# -gt 0 ]; do
  case "$1" in
    --state-file) STATE_FILE="$2"; shift 2 ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    --cell-type) CELL_TYPE="$2"; shift 2 ;;
    *) shift ;;
  esac
done

mkdir -p "$RUNDIR"
USB_THERMAL=$(thermal_path usb)
VOOC_THERMAL=$(thermal_path svooc_mos_btb_usr)
CPU_THERMAL=$(thermal_path cpu-1-0-usr)
[ -n "$CPU_THERMAL" ] || CPU_THERMAL=$(thermal_path cpu-0-0-usr)
GPU_THERMAL=$(thermal_path gpu-usr)
SHELL_THERMAL=$(thermal_path shell_front)
[ -n "$SHELL_THERMAL" ] || SHELL_THERMAL=$(thermal_path quiet_therm)

sample_count=0
sum_current=0
sum_power=0
max_current=null
min_current=null
max_power=null
min_power=null
offline_samples=0
last_rm=
changed_at=$(date +%s)

echo "O-Pulse shell collector started; interval=${INTERVAL}s"

while true; do
  timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null)
  now=$(date +%s)
  market_name=$(getprop ro.vendor.oplus.market.name 2>/dev/null)
  build_id=$(getprop ro.build.display.id 2>/dev/null)
  battery_sn=$(read_node /sys/class/oplus_chg/battery/battery_sn N/A)
  battery_date=$(read_node /sys/class/oplus_chg/battery/battery_manu_date N/A)
  usb_online=$(read_node /sys/class/power_supply/usb/online 0)
  notify_code=$(read_node /sys/class/oplus_chg/battery/battery_notify_code 0)
  charge_type=$(read_node /sys/class/power_supply/battery/charge_type N/A)
  fast_type=$(read_node /sys/class/oplus_chg/battery/fast_chg_type 0)
  svooc_flag=$(read_node /sys/class/oplus_chg/battery/svooc_flag 0)
  mmi_status=$(read_node /sys/class/oplus_chg/battery/chg_mmi_status 0)
  capacity=$(read_node /sys/class/power_supply/battery/capacity "")
  chip_soc=$(read_node /sys/class/oplus_chg/battery/chip_soc --)
  gauge_soc=$(read_node /sys/class/oplus_chg/battery/gauge_soc --)
  rm_mah=$(read_node /sys/class/oplus_chg/battery/battery_rm N/A)
  fcc_mah=$(read_node /sys/class/oplus_chg/battery/battery_fcc N/A)
  design_mah=$(read_node /sys/class/oplus_chg/battery/design_capacity N/A)
  soh=$(read_node /sys/class/oplus_chg/battery/battery_soh N/A)
  cycle_count=$(read_node /sys/class/oplus_chg/battery/battery_cc --)
  battery_voltage=$(read_node /sys/class/power_supply/battery/voltage_now N/A)
  usb_voltage=$(read_node /sys/class/power_supply/usb/voltage_now N/A)
  usb_voltage_max=$(read_node /sys/class/power_supply/usb/voltage_max N/A)
  battery_current=$(read_node /sys/class/power_supply/battery/current_now N/A)
  usb_current=$(read_node /sys/class/power_supply/usb/current_now N/A)
  pps_raw=$(read_node /sys/devices/virtual/oplus_chg/battery/ppschg_power N/A)
  battery_temp=$(read_node /sys/class/power_supply/battery/temp N/A)
  bdd_trend=$(read_node /sys/class/oplus_chg/battery/bdd_voltdiff_trend N/A)
  vbat_diff=$(read_node /sys/class/oplus_chg/battery/vbat_voltdiff N/A)
  usb_temp=$(read_node "$USB_THERMAL" N/A)
  vooc_temp=$(read_node "$VOOC_THERMAL" N/A)
  cpu_temp=$(read_node "$CPU_THERMAL" N/A)
  gpu_temp=$(read_node "$GPU_THERMAL" N/A)
  shell_temp=$(read_node "$SHELL_THERMAL" N/A)

  [ -n "$capacity" ] || capacity=$(percent "$rm_mah" "$fcc_mah")
  battery_voltage_v=$(scale "$battery_voltage" 1000000)
  usb_voltage_v=$(scale "$usb_voltage" 1000000)
  usb_voltage_max_v=$(scale "$usb_voltage_max" 1000000)
  battery_current_a=$(absolute_current "$battery_current" "$([ "$CELL_TYPE" = "1" ] && printf 2 || printf 1)")
  usb_current_a=$(usb_current "$usb_current")
  battery_power=$(multiply "$battery_voltage_v" "$battery_current_a")
  usb_power=$(multiply "$usb_voltage_v" "$usb_current_a")
  pps_power=$(awk -v value="$pps_raw" 'BEGIN { if (value <= 0) print "null"; else if (value > 200000) printf "%.2f", value / 1000000; else if (value > 1000) printf "%.2f", value / 1000; else printf "%.2f", value }')

  power_source=battery_cell_estimate
  power=$battery_power
  if [ "$pps_power" != "null" ]; then power=$pps_power; power_source=ppschg_power
  elif [ "$usb_power" != "null" ]; then power=$usb_power; power_source=usb_voltage_current
  fi

  remaining=$(awk -v f="$fcc_mah" -v r="$rm_mah" 'BEGIN { if (f == "" || r == "" || f == 0) print "null"; else { value=f-r; if (value < 0) value=0; printf "%.2f", value } }')
  locked_pct=$(percent "$remaining" "$fcc_mah")
  health_pct=$(percent "$fcc_mah" "$design_mah")
  battery_temp_c=$(scale "$battery_temp" 10)
  usb_temp_c=$(thermal_celsius "$usb_temp")
  vooc_temp_c=$(thermal_celsius "$vooc_temp")
  cpu_temp_c=$(thermal_celsius "$cpu_temp")
  gpu_temp_c=$(thermal_celsius "$gpu_temp")
  shell_temp_c=$(thermal_celsius "$shell_temp")
  full=false
  [ "$notify_code" != "0" ] && full=true
  if [ "$usb_online" = "1" ]; then offline_samples=0; else offline_samples=$((offline_samples + 1)); fi

  if [ "$rm_mah" != "$last_rm" ]; then last_rm="$rm_mah"; changed_at=$now; fi
  elapsed=$((now - changed_at))
  remaining_seconds=$((123 - elapsed))
  [ "$remaining_seconds" -lt 0 ] && remaining_seconds=0
  stalled=false
  [ "$elapsed" -ge 123 ] && stalled=true

  if [ "$battery_current_a" != "null" ] && [ "$power" != "null" ]; then
    if [ "$sample_count" -eq 0 ]; then
      max_current=$battery_current_a; min_current=$battery_current_a; max_power=$power; min_power=$power
    else
      [ "$(awk -v a="$battery_current_a" -v b="$max_current" 'BEGIN { print (a > b) ? 1 : 0 }')" = "1" ] && max_current=$battery_current_a
      [ "$(awk -v a="$battery_current_a" -v b="$min_current" 'BEGIN { print (a < b) ? 1 : 0 }')" = "1" ] && min_current=$battery_current_a
      [ "$(awk -v a="$power" -v b="$max_power" 'BEGIN { print (a > b) ? 1 : 0 }')" = "1" ] && max_power=$power
      [ "$(awk -v a="$power" -v b="$min_power" 'BEGIN { print (a < b) ? 1 : 0 }')" = "1" ] && min_power=$power
    fi
    sum_current=$(awk -v a="$sum_current" -v b="$battery_current_a" 'BEGIN { printf "%.4f", a+b }')
    sum_power=$(awk -v a="$sum_power" -v b="$power" 'BEGIN { printf "%.4f", a+b }')
    sample_count=$((sample_count + 1))
  fi
  avg_current=null; avg_power=null
  if [ "$sample_count" -gt 0 ]; then
    avg_current=$(awk -v s="$sum_current" -v n="$sample_count" 'BEGIN { printf "%.2f", s/n }')
    avg_power=$(awk -v s="$sum_power" -v n="$sample_count" 'BEGIN { printf "%.2f", s/n }')
  fi

  status="Not charging"; eta="charger disconnected"
  if [ "$usb_online" = "1" ]; then
    status="Charging"
    [ "$full" = "true" ] && status="Fully charged"
    [ "$full" = "true" ] && eta="fully charged"
  fi

  tmp_file="$STATE_FILE.tmp"
  cat > "$tmp_file" <<EOF
{"timestamp":$(json_string "$timestamp"),"source":{"mode":"live-shell","csv_path":""},"device":{"market_name":$(json_string "$market_name"),"build_id":$(json_string "$build_id"),"battery_sn":$(json_string "$battery_sn"),"manufacture_date":$(json_string "$battery_date")},"battery":{"level":$(json_num "$capacity"),"voltage_mv":$(multiply "$battery_voltage_v" 1000),"current_ma":$(multiply "$battery_current_a" 1000),"temperature_c":$(json_num "$battery_temp_c"),"cell_power_w":$(json_num "$battery_power"),"current_mah":$(json_num "$rm_mah"),"remaining_to_full_mah":$(json_num "$remaining"),"full_charge_mah":$(json_num "$fcc_mah"),"design_capacity_mah":$(json_num "$design_mah"),"soh_pct":$(json_num "$soh"),"health_pct":$(json_num "$health_pct"),"locked_mah":$(json_num "$remaining"),"locked_pct":$(json_num "$locked_pct"),"chip_soc":$(json_num "$chip_soc"),"gauge_soc":$(json_num "$gauge_soc"),"cycle_count":$(json_num "$cycle_count"),"status":$(json_string "$status")},"charging":{"usb_online":$([ "$usb_online" = "1" ] && printf true || printf false),"notify_code":$(json_string "$notify_code"),"charge_type":$(json_string "$charge_type"),"fast_charge_type":$(json_string "$fast_type"),"svooc_flag":$(json_string "$svooc_flag"),"mmi_status":$(json_string "$mmi_status"),"usb_voltage_mv":$(multiply "$usb_voltage_v" 1000),"usb_voltage_max_mv":$(multiply "$usb_voltage_max_v" 1000),"usb_current_ma":$(multiply "$usb_current_a" 1000),"usb_current_source":$(json_string /sys/class/power_supply/usb/current_now),"power_w":$(json_num "$power"),"power_source":$(json_string "$power_source"),"pps_power_w":$(json_num "$pps_power"),"eta":$(json_string "$eta"),"bdd_voltdiff_trend":$(json_string "$bdd_trend"),"vbat_voltdiff_mv":$(json_num "$vbat_diff")},"thermals":{"battery_c":$(json_num "$battery_temp_c"),"usb_c":$(json_num "$usb_temp_c"),"vooc_c":$(json_num "$vooc_temp_c"),"cpu_c":$(json_num "$cpu_temp_c"),"gpu_c":$(json_num "$gpu_temp_c"),"shell_c":$(json_num "$shell_temp_c")},"statistics":{"samples":$sample_count,"current_max_a":$(json_num "$max_current"),"current_min_a":$(json_num "$min_current"),"current_avg_a":$(json_num "$avg_current"),"power_max_w":$(json_num "$max_power"),"power_min_w":$(json_num "$min_power"),"power_avg_w":$(json_num "$avg_power")},"watchdog":{"offline_samples":$offline_samples,"capacity_stalled":$stalled,"remaining_seconds":$remaining_seconds},"raw":{"battery_rm":$(json_string "$rm_mah"),"battery_fcc":$(json_string "$fcc_mah"),"design_capacity":$(json_string "$design_mah"),"battery_soh":$(json_string "$soh"),"chip_soc":$(json_string "$chip_soc"),"gauge_soc":$(json_string "$gauge_soc"),"usb_online":$(json_string "$usb_online"),"charge_type":$(json_string "$charge_type"),"fast_chg_type":$(json_string "$fast_type"),"battery_voltage_now":$(json_string "$battery_voltage"),"battery_current_now":$(json_string "$battery_current"),"usb_voltage_now":$(json_string "$usb_voltage"),"usb_current_now":$(json_string "$usb_current"),"battery_temp":$(json_string "$battery_temp"),"service.collector":"shell"},"csv":{}}
EOF
  mv "$tmp_file" "$STATE_FILE"
  sleep "$INTERVAL"
done
