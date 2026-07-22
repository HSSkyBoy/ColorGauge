# ⚡ O-Pulse

**O-Pulse** 是面向 **Oplus** 设备的 KernelSU WebUI 电池与温控监控模块，适用于
OPPO、OnePlus 和 realme 的 ColorOS、OxygenOS、realme UI 设备。

不需要安装独立 App。刷入模块并重启后，可直接从 KernelSU 管理器打开仪表盘，查看
充电、电池、温度和厂商日志数据。

> 不同机型公开的 SysFS 节点并不一致。缺失的温区或电池字段会显示为 `N/A`，不会阻止
> 模块运行。

## ✨ 功能

- **功率与电流**：显示 USB 电压、输入电流、电芯电流及输入功率。功率优先采用
  `ppschg_power`，其次为 USB 电压乘输入电流，最后才标注为电芯侧估算值。
- **多区域温度**：读取电池、USB、VOOC MOS、CPU、GPU 和机身温区；温区节点带有
  Oplus 常见名称的 fallback。
- **电池状态**：汇总 UI SOC、Chip SOC、Gauge SOC、剩余容量、FCC、设计容量、SOH、
  健康度、锁容估算和预计充满时间。
- **CSV 日志**：解析最新 `/data/vendor/battery/battery-log-*.csv` 的表头与最后一行，
  在 WebUI 中以可搜索的键值网格展示。
- **低频采集**：C++ daemon 直接读取 SysFS 和 CSV，在线默认每 5 秒采样；断开 USB 后
  降至 10 秒。

## 🛠️ 安装

### 前置条件

- 支持 WebUI 的 Root 管理器，如 KernelSU, SuikSU Ultra，Magisk 可以用 WebUI 模组。
- Oplus 系统设备。其他 Android 设备可以安装，但大部分厂商专属字段可能不可用。
- 仅支援 `arm64-v8a` 设备。

### 步骤

1. 在 [Releases](../../releases) 或 GitHub Actions artifact 下载
   `O-Pulse-*-arm64-v8a.zip`。
2. 打开 KernelSU 管理器，进入「模块」并选择「安装模块」。
3. 选择 ZIP，完成后重启设备。
4. 重启后，在 KernelSU 的模块列表打开 O-Pulse WebUI。

## 🔧 构建

构建需要 Android NDK，然后执行：

```powershell
$ndk = "$env:LOCALAPPDATA\Android\Sdk\ndk\29.0.13113456"

cmake -S native -B native/build/android-arm64 `
  -DCMAKE_TOOLCHAIN_FILE="$ndk\build\cmake\android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-26 `
  -DCMAKE_BUILD_TYPE=Release

cmake --build native/build/android-arm64 --parallel
```

将生成的 `chg_daemon` 放入 `bin/` 后，从仓库根目录打包模块文件即可。CI 会自动完成
这一步。

## 📌 设备调试

WebUI 的原始数据区会显示实际读取的节点和值。验证快充时，重点关注：

- `charging.power_source`：`ppschg_power`、`usb_voltage_current` 或
  `battery_cell_estimate`。
- `charging.usb_current_source`：当前输入电流来源。
- `raw.usb_online_standard`、`raw.usb_voltage_present`、`raw.usb_protocol_active`：
  USB 在线状态的判定信号。

如果设备有更可靠的厂商输入电流节点，可为 daemon 增加：

```text
--input-current-node /sys/已验证的节点路径
```

也可以用 `--ppschg-unit uw` 或 `--ppschg-unit mw` 覆盖 PPS 功率单位的自动判断。
