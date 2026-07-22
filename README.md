# O-Pulse

O-Pulse 是面向 Oplus 设备充电、電池、温控与厂商 battery-log 遥测数据的
KernelSU WebUI 模块。当前仓库提供模块外壳与支持离线预览的 Dashboard 原型；
原生采集器已迁移至 `native/`；发布模块前需使用 Android NDK 编译并放入 `bin/`。

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
  bin/chg_daemon              # 编译后的原生采集器
  native/                     # C++20 采集器源代码
  webroot/index.html          # KernelSU WebUI 入口
  webroot/assets/
```

`service.sh` 会等待系统启动完成，并且仅在 `bin/chg_daemon` 存在且可执行时
启动采集器。采集器会原子写入 `run/state.json`，WebUI 通过 KernelSU Shell bridge
读取该快照；`uninstall.sh` 仅终止此模块记录的 PID。

## 状态文件契约

采集器每 2-5 秒原子写入一个完整 JSON 对象。Dashboard 每两秒通过 KernelSU 的
Shell bridge 读取快照；采集器或 bridge 不可用时会显示模拟数据。

```json
{
  "timestamp": "2026-07-22T12:00:00.000Z",
  "source": { "mode": "live", "csv_path": "/data/vendor/battery/battery-log-*.csv" },
  "device": { "market_name": "OnePlus", "build_id": "..." },
  "battery": { "level": 72, "voltage_mv": 4388, "current_ma": 6950, "temperature_c": 35.2, "health_pct": 98, "status": "充电中" },
  "charging": { "usb_online": true, "usb_voltage_mv": 9990, "usb_current_ma": 6500, "power_w": 64.9, "fast_charge_type": "SUPERVOOC", "eta": "18分钟" },
  "thermals": { "usb_c": 33.4, "vooc_c": 37.8, "cpu_c": 43.6, "gpu_c": 41.3, "shell_c": 34.7 },
  "statistics": { "current_max_a": 8.4, "power_avg_w": 62.7 },
  "watchdog": { "capacity_stalled": false, "remaining_seconds": 123 },
  "raw": { "battery_rm": "3512", "battery_fcc": "4500", "chip_soc": "72", "csv.any_vendor_field": "..." }
}
```
