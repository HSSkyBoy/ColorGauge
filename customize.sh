#!/sbin/sh

SKIPUNZIP=0

ui_print "- Installing O-Pulse"
ui_print "- Checking Oplus device properties"

MARKET_NAME=$(getprop ro.vendor.oplus.market.name 2>/dev/null)
if [ -n "$MARKET_NAME" ]; then
  ui_print "- Device: $MARKET_NAME"
else
  ui_print "- Device property unavailable; missing values will show as N/A"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/collector.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
if [ -f "$MODPATH/bin/chg_daemon" ]; then
  set_perm "$MODPATH/bin/chg_daemon" 0 0 0755
fi

ui_print "- O-Pulse installed; reboot to start the collector"
