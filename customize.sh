#!/sbin/sh

SKIPUNZIP=0

ui_print "- 正在安装 O-Pulse..."
ui_print "- 正在检查 Oplus 属性..."

OPULSE_MARKET_NAME=$(getprop ro.vendor.oplus.market.name)
if [ -n "$OPULSE_MARKET_NAME" ]; then
  ui_print "- 检测到 Oplus 设备: $OPULSE_MARKET_NAME"
else
  ui_print "! 未检测到 Oplus 属性，缺失节点将显示为 N/A。"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

if [ -f "$MODPATH/bin/chg_daemon" ]; then
  set_perm "$MODPATH/bin/chg_daemon" 0 0 0755
fi

ui_print "- O-Pulse 安装完成，重启后将启动监控服务。"
