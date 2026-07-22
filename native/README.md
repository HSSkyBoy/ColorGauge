# O-Pulse native collector

`chg_daemon` is a no-dependency C++20 collector for Android. It scans the
Oplus SysFS nodes once, polls only local files, reads the newest battery CSV
without invoking a shell, and atomically writes the full JSON snapshot to the
module run directory.

## Android build

Install Android NDK r26 or newer, then build from this directory:

```sh
cmake -S . -B build/android-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android-arm64 --parallel
```

Copy `build/android-arm64/chg_daemon` to the module's `bin/chg_daemon` before
packing the KernelSU ZIP. The module starts it with
`--state-file /data/adb/modules/o_pulse/run/state.json`.

## Runtime options

```text
--interval 5                 Online polling interval in seconds.
--offline-interval 10        Polling interval without USB power.
--state-file /absolute/path  Atomically write the JSON snapshot to this file.
--input-current-node /path   Optional verified vendor input-current node; defaults to usb/current_now.
--ppschg-unit auto|uw|mw     ppschg_power unit override; default auto records its chosen scale.
--cell-type 1                Use the dual-cell current multiplier from the reference script.
--capacity-timeout 123       Capacity-stall watchdog timeout.
--offline-action slow|exit   Default: slow polling; exit matches the script.
--stalled-action continue|exit
                              Default: report the watchdog; exit matches the script.
```

The defaults retain the module design requirement for a persistent, low-power
service. Passing both `exit` actions reproduces the original terminal script's
automatic-stop behavior.
