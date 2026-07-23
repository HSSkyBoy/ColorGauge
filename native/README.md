# O-Pulse native collector

`chg_daemon` is the primary Android collector. It reads the same Oplus SysFS
nodes used by the reference Shell script and atomically writes one JSON
snapshot to the module `run` directory. Missing vendor nodes become `null`;
they do not stop the collector.

## Build

```sh
cmake -S . -B build/android-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android-arm64 --parallel
```

Copy `build/android-arm64/chg_daemon` to the module's `bin/chg_daemon` before
packing the KernelSU ZIP.

## Runtime options

```text
--state-file /absolute/path       JSON output path
--interval seconds                Polling interval while USB is online
--offline-interval seconds        Polling interval while USB is offline
--cell-type 0|1                   Reference script uses 1 for dual-cell devices
--input-current-node /path        Optional USB input-current node override
--ppschg-unit auto|uw|mw          ppschg_power unit override
--capacity-timeout seconds        Capacity-stall watchdog timeout
```
