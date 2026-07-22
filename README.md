# O-Pulse

O-Pulse 是面向 Oplus 设备充电、電池、温控与厂商 battery-log 遥测数据的
KernelSU WebUI 模块。当前仓库提供模块外壳与支持离线预览的 Dashboard 原型；
静态原生采集器是下一阶段的实现目标。

## 安装目录

KernelSU 从 `webroot/` 提供 WebUI 文件，因此本项目遵循官方目录布局，
而不是使用单独的 `webui/ksu.json` 清单：

```text
o_pulse/
  module.prop
  customize.sh
  service.sh
  uninstall.sh
  skip_mount
  bin/chg_daemon              # Phase 1 原生采集器
  webroot/index.html          # KernelSU WebUI 入口
  webroot/assets/
```

`service.sh` 会等待系统启动完成，并且仅在 `bin/chg_daemon` 存在且可执行时
启动采集器。它仅监听 `127.0.0.1:28888`；`uninstall.sh` 仅终止此模块记录的 PID。

## WebSocket 契约

采集器必须每 2-5 秒发送一个完整 JSON 对象。Dashboard 在 WebSocket 可用前使用
模拟数据，并会在断线后使用指数退避自动重连。

```json
{
  "timestamp": "2026-07-22T12:00:00.000Z",
  "source": { "mode": "live", "csv_path": "/data/vendor/battery/battery-log-*.csv" },
  "battery": { "level": 72, "voltage_mv": 4388, "current_ma": 6950, "temperature_c": 35.2, "health_pct": 98, "status": "充电中" },
  "charging": { "usb_online": true, "usb_voltage_mv": 9990, "usb_current_ma": 6500, "power_w": 64.9, "fast_charge_type": "SUPERVOOC" },
  "thermals": { "usb_c": 33.4, "vooc_c": 37.8, "cpu_c": 43.6, "gpu_c": 41.3, "shell_c": 34.7 },
  "raw": { "battery_rm": "3512", "battery_fcc": "4500", "chip_soc": "72" }
}
```

## 参考脚本映射

提供的 Shell 参考脚本将决定首版采集器实现：

- 扫描 `/sys/devices/virtual/thermal/thermal_zone*/type`，包括 `usb`、
  `svooc_mos_btb_usr`、CPU、GPU 与 shell 的 fallback 节点。
- 从 `/sys/class/power_supply/*` 读取电池和 USB 数据，并从
  `/sys/class/oplus_chg/battery/*` 读取 Oplus 专属字段。
- 解析最新 `/data/vendor/battery/battery-log-*.csv` 的表头及最后一行到 `raw`；
  缺失 key 不应导致采集失败。

可直接在桌面浏览器打开 `webroot/index.html` 进行布局开发；在 KernelSU 以外会显示
模拟数据。
