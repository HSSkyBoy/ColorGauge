#!/system/bin/sh

MODDIR=${0%/*}
COLLECTOR_BIN="$MODDIR/collector.sh"
RUNDIR="$MODDIR/run"
PID_FILE="$RUNDIR/collector.pid"
LOG_FILE="$RUNDIR/collector.log"

is_running() {
  [ -n "$1" ] && kill -0 "$1" 2>/dev/null
}

while [ "$(getprop sys.boot_completed)" != "1" ]; do
  sleep 3
done

mkdir -p "$RUNDIR"
chmod 0755 "$MODDIR/webroot" "$MODDIR/webroot/assets" 2>/dev/null

if [ ! -f "$COLLECTOR_BIN" ]; then
  echo "O-Pulse service: collector is missing: $COLLECTOR_BIN" > "$LOG_FILE"
  exit 0
fi

if [ -f "$PID_FILE" ] && is_running "$(cat "$PID_FILE")"; then
  exit 0
fi

rm -f "$PID_FILE"
echo "O-Pulse service: module_dir=$MODDIR" > "$LOG_FILE"
echo "O-Pulse service: state_file=$RUNDIR/state.json" >> "$LOG_FILE"
echo "O-Pulse service: starting shell collector via /system/bin/sh" >> "$LOG_FILE"
/system/bin/sh "$COLLECTOR_BIN" --state-file "$RUNDIR/state.json" --interval 10 --offline-interval 10 >> "$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"
