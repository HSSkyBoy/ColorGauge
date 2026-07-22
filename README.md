# O-Pulse

O-Pulse 是一个面向 Oplus 设备的 KernelSU WebUI 模组，主要用来查看充电功率、电池状态、温区温度和厂商电池日志。

现在仓库只保留一套实际使用中的方案：

- `collector.sh`
  纯 Shell 采集器，后台定时读取 SysFS 和 `/data/vendor/battery/battery-log-*.csv`，写入 `run/state.json`
- `service.sh`
  开机后启动采集器
- `webroot/`
  WebUI 页面

不再包含 C++ / NDK / `chg_daemon` 构建链路。

## 功能

- 显示输入功率、USB 电压、电池电流、电池电压
- 显示电池温度和常见温区温度
- 显示电量、SOH、健康度、锁容估算、预计充满时间
- 读取最新电池 CSV 并在 WebUI 中展示原始字段

## 安装

1. 下载发布包或 GitHub Actions 生成的 ZIP
2. 在 KernelSU 中安装模组
3. 重启设备
4. 在模组列表打开 O-Pulse WebUI

## 刷新频率

默认每 10 秒刷新一次，由 [service.sh](E:\GitHubRepo\ColorGauge\service.sh) 启动：

```sh
"$COLLECTOR_BIN" --state-file "$RUNDIR/state.json" --interval 10 --offline-interval 10
```

## 兼容性

- 主要面向 OPPO、OnePlus、realme 等 Oplus 系设备
- 不同机型公开的节点不完全一致，缺失字段会显示为 `N/A`
- 模组用途以查看为主，当前不包含额外控制功能

## 打包

仓库已经自带 GitHub Actions 打包流程，会直接把 Shell 采集器和 WebUI 打成可安装 ZIP。
