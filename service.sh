#!/system/bin/sh

MODDIR=${0%/*}
DAEMON_BIN="$MODDIR/bin/chg_daemon"
RUNDIR="$MODDIR/run"
PID_FILE="$RUNDIR/chg_daemon.pid"
LOG_FILE="$RUNDIR/daemon.log"

is_running() {
  [ -n "$1" ] && kill -0 "$1" 2>/dev/null
}

while [ "$(getprop sys.boot_completed)" != "1" ]; do
  sleep 3
done

mkdir -p "$RUNDIR"
chmod 0755 "$MODDIR/webroot" "$MODDIR/webroot/assets" 2>/dev/null

if [ ! -x "$DAEMON_BIN" ]; then
  echo "O-Pulse service: collector is missing or not executable: $DAEMON_BIN" > "$LOG_FILE"
  exit 0
fi

if [ -f "$PID_FILE" ] && is_running "$(cat "$PID_FILE")"; then
  exit 0
fi

rm -f "$PID_FILE"
echo "O-Pulse service: starting collector" > "$LOG_FILE"
"$DAEMON_BIN" --state-file "$RUNDIR/state.json" --interval 5 --offline-interval 10 >> "$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"
