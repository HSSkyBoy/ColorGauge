cmake_minimum_required(VERSION 3.22)
project(o_pulse_daemon LANGUAGES CXX)

add_executable(chg_daemon src/main.cpp)
target_compile_features(chg_daemon PRIVATE cxx_std_20)
target_compile_options(chg_daemon PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  -Oz -ffunction-sections -fdata-sections -fno-rtti
)
# Keep the Android C++ runtime self-contained in the module binary.
if(ANDROID)
  target_link_options(chg_daemon PRIVATE
    -static-libstdc++
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,--build-id=none
  )
endif()
