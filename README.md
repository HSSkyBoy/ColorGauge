# O-Pulse

O-Pulse 是一个独立的 Kotlin Android 应用程式，用来查看 Oplus 装置的
充电功率、电池状态、温度区域和电池 CSV 资料。

应用程式会透过 `su` 取得 Root 授权，再启动仓库里的 `collector.sh`。原本的

## 运行方式

1. 启动 App，KernelSU 或 Magisk 会显示 Root 授权提示。
2. App 将 `collector.sh` 储存到 `/data/adb/opulse/`。
3. App 以 Root 权限在背景启动 Shell 采集器。
4. WebView 每 4 秒读取 `/data/adb/opulse/state.json` 并更新介面。

采集器保留原始脚本的读取方式：电池与 USB SysFS 节点、充电功率、电流、
电压、电量、健康度、温度区域、统计资料和厂商电池 CSV。不同 Oplus 装置
缺少的节点会显示 `N/A`，不会让整个采集器停止。

## 建置

需要 JDK 21、Gradle 8.14.14 与 Kotlin 2.4.10-RC：

```powershell
gradle assembleDebug
```

APK 输出在：

`app/build/outputs/apk/debug/app-debug.apk`

GitHub Actions 会自动建置 release APK，并上传为 `O-Pulse-apk` artifact。

## Root 权限

装置需要 KernelSU、Magisk 或其他 `su` 提供者。第一次启动时请允许
O-Pulse 取得 Root 权限。这个应用程式只有读取功能，不会修改充电设置。
