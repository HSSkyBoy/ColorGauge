#!/system/bin/sh

MODDIR=${0%/*}
PID_FILE="$MODDIR/run/chg_daemon.pid"

if [ -f "$PID_FILE" ]; then
  OPULSE_PID=$(cat "$PID_FILE")
  if [ -n "$OPULSE_PID" ] && kill -0 "$OPULSE_PID" 2>/dev/null; then
    kill "$OPULSE_PID" 2>/dev/null
    sleep 1
    kill -9 "$OPULSE_PID" 2>/dev/null
  fi
  rm -f "$PID_FILE"
fi

exit 0
