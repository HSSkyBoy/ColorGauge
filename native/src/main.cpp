#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace {

constexpr char kThermalDir[] = "/sys/devices/virtual/thermal";
constexpr char kCsvDir[] = "/data/vendor/battery";

std::atomic<bool> g_running{true};

struct Config {
  int interval_seconds = 5;
  int offline_interval_seconds = 10;
  int capacity_timeout_seconds = 123;
  int cell_type = 1;
  bool exit_when_offline = false;
  bool exit_when_capacity_stalled = false;
  std::string state_file = "/data/adb/modules/o_pulse/run/state.json";
  std::string input_current_node;
  std::string pps_power_unit = "auto";
};

struct ThermalPaths {
  std::string usb;
  std::string vooc;
  std::string cpu;
  std::string gpu;
  std::string shell;
};

struct Stats {
  double max_current_a = 0.0;
  double min_current_a = 0.0;
  double sum_current_a = 0.0;
  double max_power_w = 0.0;
  double min_power_w = 0.0;
  double sum_power_w = 0.0;
  uint64_t samples = 0;

  void add(double current_a, double power_w) {
    if (samples == 0) {
      max_current_a = min_current_a = current_a;
      max_power_w = min_power_w = power_w;
    } else {
      max_current_a = std::max(max_current_a, current_a);
      min_current_a = std::min(min_current_a, current_a);
      max_power_w = std::max(max_power_w, power_w);
      min_power_w = std::min(min_power_w, power_w);
    }
    sum_current_a += current_a;
    sum_power_w += power_w;
    ++samples;
  }
};

struct Watchdog {
  std::optional<long long> last_rm;
  std::chrono::steady_clock::time_point changed_at = std::chrono::steady_clock::now();
  int offline_samples = 0;
};

struct Snapshot {
  std::map<std::string, std::string> raw;
  std::map<std::string, std::string> csv;
  std::string csv_path;
  std::string timestamp;
  bool usb_online = false;
  bool full = false;
  bool watchdog_expired = false;
  int watchdog_remaining_seconds = 0;
  int offline_samples = 0;
  std::optional<double> capacity;
  std::optional<double> voltage_bat_v;
  std::optional<double> voltage_usb_v;
  std::optional<double> voltage_max_v;
  std::optional<double> current_a;
  std::optional<double> input_current_a;
  std::optional<double> power_w;
  std::optional<double> cell_power_w;
  std::optional<double> pps_w;
  std::optional<double> battery_temp_c;
  std::optional<double> usb_temp_c;
  std::optional<double> vooc_temp_c;
  std::optional<double> cpu_temp_c;
  std::optional<double> gpu_temp_c;
  std::optional<double> shell_temp_c;
  std::optional<double> remaining_mah;
  std::optional<double> locked_mah;
  std::optional<double> locked_percent;
  std::optional<double> health_percent;
  std::string input_current_source;
  std::string power_source;
  std::string eta_text;
};


std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string read_node(const std::string& path, const std::string& fallback = "") {
  std::ifstream input(path);
  if (!input) return fallback;
  std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  value = trim(value);
  return value.empty() ? fallback : value;
}

std::optional<double> number(const std::string& value) {
  if (value.empty() || value == "--" || value == "N/A") return std::nullopt;
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(value.c_str(), &end);
  if (errno != 0 || end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
  return parsed;
}

std::optional<long long> integer(const std::string& value) {
  if (value.empty() || value == "--" || value == "N/A") return std::nullopt;
  char* end = nullptr;
  errno = 0;
  const long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0') return std::nullopt;
  return parsed;
}

std::string android_property(const char* key) {
#if defined(__ANDROID__)
  char value[PROP_VALUE_MAX] = {};
  return __system_property_get(key, value) > 0 ? value : "";
#else
  (void)key;
  return "";
#endif
}

std::string find_thermal_node(std::string_view keyword) {
  DIR* root = opendir(kThermalDir);
  if (root == nullptr) return {};
  std::string result;
  while (const dirent* entry = readdir(root)) {
    const std::string name(entry->d_name);
    if (name.rfind("thermal_zone", 0) != 0) continue;
    const std::string base = std::string(kThermalDir) + "/" + name;
    if (read_node(base + "/type").find(keyword) != std::string::npos) {
      result = base + "/temp";
      break;
    }
  }
  closedir(root);
  return result;
}

ThermalPaths discover_thermal_paths() {
  ThermalPaths paths;
  paths.usb = find_thermal_node("usb");
  paths.vooc = find_thermal_node("svooc_mos_btb_usr");
  paths.cpu = find_thermal_node("cpu-1-0-usr");
  if (paths.cpu.empty()) paths.cpu = find_thermal_node("cpu-0-0-usr");
  paths.gpu = find_thermal_node("gpu-usr");
  paths.shell = find_thermal_node("shell_front");
  if (paths.shell.empty()) paths.shell = find_thermal_node("quiet_therm");
  return paths;
}

std::string latest_csv_file() {
  DIR* directory = opendir(kCsvDir);
  if (directory == nullptr) return {};
  std::time_t newest = 0;
  std::string result;
  while (const dirent* entry = readdir(directory)) {
    const std::string name(entry->d_name);
    if (name.rfind("battery-log-", 0) != 0 || name.size() < 5 || name.substr(name.size() - 4) != ".csv") continue;
    const std::string path = std::string(kCsvDir) + "/" + name;
    struct stat info {};
    if (stat(path.c_str(), &info) == 0 && info.st_mtime >= newest) {
      newest = info.st_mtime;
      result = path;
    }
  }
  closedir(directory);
  return result;
}

std::vector<std::string> split_csv(std::string_view line) {
  std::vector<std::string> fields;
  std::string current;
  bool quoted = false;
  for (size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (character == '"') {
      if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
        current.push_back(character);
        ++index;
      } else {
        quoted = !quoted;
      }
    } else if (character == ',' && !quoted) {
      fields.push_back(trim(current));
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  fields.push_back(trim(current));
  return fields;
}

std::string normalize_header(std::string value) {
  std::string normalized;
  for (const unsigned char character : value) {
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_') {
      normalized.push_back(static_cast<char>(character));
    }
  }
  return normalized;
}

std::map<std::string, std::string> parse_latest_csv_row(const std::string& path) {
  std::ifstream input(path);
  if (!input) return {};
  std::string header;
  std::string line;
  std::string last_row;
  if (!std::getline(input, header)) return {};
  while (std::getline(input, line)) {
    if (!trim(line).empty()) last_row = line;
  }
  if (last_row.empty()) return {};
  const auto headers = split_csv(header);
  const auto values = split_csv(last_row);
  std::map<std::string, std::string> result;
  for (size_t index = 0; index < headers.size(); ++index) {
    const std::string key = normalize_header(headers[index]);
    if (!key.empty()) result[key] = index < values.size() ? values[index] : "";
  }
  return result;
}

std::optional<double> scaled(const std::string& raw, double divisor) {
  const auto value = number(raw);
  return value ? std::optional<double>(*value / divisor) : std::nullopt;
}

std::optional<double> thermal_celsius(const std::string& raw) {
  const auto value = integer(raw);
  if (!value) return std::nullopt;
  const long long milli = *value / 1000;
  return static_cast<double>(milli == 0 ? *value / 100 : milli);
}

bool active_charge_signal(const std::string& value) {
  return !value.empty() && value != "0" && value != "--" && value != "N/A";
}

std::optional<double> pps_power_w(const std::string& raw, const std::string& unit, std::string& resolved_unit) {
  const auto value = number(raw);
  if (!value || *value <= 0) return std::nullopt;
  if (unit == "uw") {
    resolved_unit = "uw";
    return *value / 1000000.0;
  }
  if (unit == "mw") {
    resolved_unit = "mw";
    return *value / 1000.0;
  }
  resolved_unit = *value > 200000.0 ? "uw(auto)" : "mw(auto)";
  return *value > 200000.0 ? *value / 1000000.0 : *value / 1000.0;
}

std::string iso_timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm utc {};
  gmtime_r(&now, &utc);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}

void add_node(Snapshot& snapshot, const std::string& key, const std::string& path, const std::string& fallback = "") {
  snapshot.raw[key] = read_node(path, fallback);
}

Snapshot collect(const ThermalPaths& thermal_paths, const Config& config, Stats& stats, Watchdog& watchdog) {
  Snapshot snapshot;
  snapshot.timestamp = iso_timestamp();

  snapshot.raw["device_name"] = android_property("ro.vendor.oplus.market.name");
  snapshot.raw["build_id"] = android_property("ro.build.display.id");
  add_node(snapshot, "battery_sn", "/sys/class/oplus_chg/battery/battery_sn", "N/A");
  add_node(snapshot, "battery_manu_date", "/sys/class/oplus_chg/battery/battery_manu_date", "N/A");

  add_node(snapshot, "usb_online", "/sys/class/power_supply/usb/online", "0");
  add_node(snapshot, "battery_notify_code", "/sys/class/oplus_chg/battery/battery_notify_code", "0");
  add_node(snapshot, "charge_type", "/sys/class/power_supply/battery/charge_type", "N/A");
  add_node(snapshot, "fast_chg_type", "/sys/class/oplus_chg/battery/fast_chg_type", "0");
  add_node(snapshot, "svooc_flag", "/sys/class/oplus_chg/battery/svooc_flag", "0");
  add_node(snapshot, "chg_mmi_status", "/sys/class/oplus_chg/battery/chg_mmi_status", "0");

  add_node(snapshot, "capacity", "/sys/class/power_supply/battery/capacity", "");
  add_node(snapshot, "chip_soc", "/sys/class/oplus_chg/battery/chip_soc", "--");
  add_node(snapshot, "gauge_soc", "/sys/class/oplus_chg/battery/gauge_soc", "--");
  add_node(snapshot, "battery_rm", "/sys/class/oplus_chg/battery/battery_rm", "0");
  add_node(snapshot, "battery_fcc", "/sys/class/oplus_chg/battery/battery_fcc", "0");
  add_node(snapshot, "design_capacity", "/sys/class/oplus_chg/battery/design_capacity", "0");
  add_node(snapshot, "battery_soh", "/sys/class/oplus_chg/battery/battery_soh", "0");
  add_node(snapshot, "battery_cc", "/sys/class/oplus_chg/battery/battery_cc", "--");

  add_node(snapshot, "usb_voltage_max", "/sys/class/power_supply/usb/voltage_max", "0");
  add_node(snapshot, "battery_voltage_now", "/sys/class/power_supply/battery/voltage_now", "0");
  add_node(snapshot, "usb_voltage_now", "/sys/class/power_supply/usb/voltage_now", "0");
  add_node(snapshot, "battery_current_now", "/sys/class/power_supply/battery/current_now", "0");
  add_node(snapshot, "usb_current_now", "/sys/class/power_supply/usb/current_now", "0");
  const std::string input_current_path = config.input_current_node.empty() ? "/sys/class/power_supply/usb/current_now" : config.input_current_node;
  add_node(snapshot, "input_current_raw", input_current_path, "0");
  snapshot.raw["input_current_source"] = input_current_path;
  add_node(snapshot, "ppschg_power", "/sys/devices/virtual/oplus_chg/battery/ppschg_power", "0");
  add_node(snapshot, "bdd_voltdiff_trend", "/sys/class/oplus_chg/battery/bdd_voltdiff_trend", "");
  add_node(snapshot, "vbat_voltdiff", "/sys/class/oplus_chg/battery/vbat_voltdiff", "0");

  add_node(snapshot, "battery_temp", "/sys/class/power_supply/battery/temp", "0");
  snapshot.raw["thermal_usb_path"] = thermal_paths.usb;
  snapshot.raw["thermal_vooc_path"] = thermal_paths.vooc;
  snapshot.raw["thermal_cpu_path"] = thermal_paths.cpu;
  snapshot.raw["thermal_gpu_path"] = thermal_paths.gpu;
  snapshot.raw["thermal_shell_path"] = thermal_paths.shell;
  snapshot.raw["thermal_usb_raw"] = read_node(thermal_paths.usb, "0");
  snapshot.raw["thermal_vooc_raw"] = read_node(thermal_paths.vooc, "0");
  snapshot.raw["thermal_cpu_raw"] = read_node(thermal_paths.cpu, "0");
  snapshot.raw["thermal_gpu_raw"] = read_node(thermal_paths.gpu, "0");
  snapshot.raw["thermal_shell_raw"] = read_node(thermal_paths.shell, "0");

  snapshot.full = integer(snapshot.raw["battery_notify_code"]).value_or(0) != 0;
  const auto rm = number(snapshot.raw["battery_rm"]);
  const auto fcc = number(snapshot.raw["battery_fcc"]);
  const auto design = number(snapshot.raw["design_capacity"]);
  snapshot.capacity = number(snapshot.raw["capacity"]);
  if (!snapshot.capacity && rm && fcc && *fcc > 0) snapshot.capacity = *rm * 100.0 / *fcc;

  snapshot.voltage_max_v = scaled(snapshot.raw["usb_voltage_max"], 1000000.0);
  snapshot.voltage_bat_v = scaled(snapshot.raw["battery_voltage_now"], 1000000.0);
  snapshot.voltage_usb_v = scaled(snapshot.raw["usb_voltage_now"], 1000000.0);
  const bool standard_usb_online = integer(snapshot.raw["usb_online"]).value_or(0) != 0;
  const bool usb_voltage_present = snapshot.voltage_usb_v && *snapshot.voltage_usb_v >= 4.0;
  const bool protocol_active = active_charge_signal(snapshot.raw["fast_chg_type"]) ||
                               active_charge_signal(snapshot.raw["svooc_flag"]) ||
                               active_charge_signal(snapshot.raw["chg_mmi_status"]);
  snapshot.usb_online = standard_usb_online && (usb_voltage_present || protocol_active);
  snapshot.raw["usb_online_standard"] = standard_usb_online ? "1" : "0";
  snapshot.raw["usb_voltage_present"] = usb_voltage_present ? "1" : "0";
  snapshot.raw["usb_protocol_active"] = protocol_active ? "1" : "0";
  const auto battery_current_raw = number(snapshot.raw["battery_current_now"]);
  if (battery_current_raw) {
    const double multiplier = config.cell_type == 1 ? 2.0 : 1.0;
    snapshot.current_a = std::abs(*battery_current_raw) * multiplier / 1000.0;
  }
  snapshot.input_current_a = scaled(snapshot.raw["input_current_raw"], 1000.0);
  snapshot.input_current_source = input_current_path;
  if (snapshot.voltage_bat_v && snapshot.current_a) snapshot.cell_power_w = *snapshot.voltage_bat_v * *snapshot.current_a;
  std::string pps_unit;
  snapshot.pps_w = pps_power_w(snapshot.raw["ppschg_power"], config.pps_power_unit, pps_unit);
  snapshot.raw["ppschg_power_unit"] = pps_unit.empty() ? "unavailable" : pps_unit;
  if (snapshot.pps_w && *snapshot.pps_w >= 0.5 && *snapshot.pps_w <= 300.0) {
    snapshot.power_w = snapshot.pps_w;
    snapshot.power_source = "ppschg_power";
  } else if (snapshot.voltage_usb_v && snapshot.input_current_a && *snapshot.voltage_usb_v > 0 && *snapshot.input_current_a > 0) {
    snapshot.power_w = *snapshot.voltage_usb_v * *snapshot.input_current_a;
    snapshot.power_source = "usb_voltage_current";
  } else if (snapshot.cell_power_w) {
    snapshot.power_w = snapshot.cell_power_w;
    snapshot.power_source = "battery_cell_estimate";
  } else {
    snapshot.power_source = "unavailable";
  }

  snapshot.battery_temp_c = scaled(snapshot.raw["battery_temp"], 10.0);
  snapshot.usb_temp_c = thermal_celsius(snapshot.raw["thermal_usb_raw"]);
  snapshot.vooc_temp_c = thermal_celsius(snapshot.raw["thermal_vooc_raw"]);
  snapshot.cpu_temp_c = thermal_celsius(snapshot.raw["thermal_cpu_raw"]);
  snapshot.gpu_temp_c = thermal_celsius(snapshot.raw["thermal_gpu_raw"]);
  snapshot.shell_temp_c = thermal_celsius(snapshot.raw["thermal_shell_raw"]);

  if (rm && fcc) {
    snapshot.remaining_mah = std::max(0.0, *fcc - *rm);
    snapshot.locked_mah = std::max(0.0, *fcc - *rm);
    if (*fcc > 0) snapshot.locked_percent = *snapshot.locked_mah * 100.0 / *fcc;
  }
  if (fcc && design && *fcc > 0 && *design > 0) snapshot.health_percent = std::floor(*fcc * 100.0 / *design);

  if (snapshot.usb_online && !snapshot.full) {
    if (snapshot.remaining_mah && snapshot.current_a && *snapshot.current_a > 0.1) {
      const double charging_ma = *snapshot.current_a * 1000.0;
      const int total_minutes = static_cast<int>(std::round(*snapshot.remaining_mah / charging_ma * 60.0));
      snapshot.eta_text = total_minutes >= 60 ? std::to_string(total_minutes / 60) + "小时" + std::to_string(total_minutes % 60) + "分钟" : std::to_string(total_minutes) + "分钟";
    } else if (snapshot.remaining_mah && *snapshot.remaining_mah <= 0) {
      snapshot.eta_text = "电池已充满";
    } else {
      snapshot.eta_text = "涓流或弱电流充电中";
    }
  } else {
    snapshot.eta_text = snapshot.full ? "已完全充满" : "未连接充电器";
  }

  if (snapshot.current_a && snapshot.power_w) stats.add(*snapshot.current_a, *snapshot.power_w);
  if (snapshot.usb_online) watchdog.offline_samples = 0; else ++watchdog.offline_samples;
  snapshot.offline_samples = watchdog.offline_samples;
  if (rm) {
    const auto rounded_rm = static_cast<long long>(*rm);
    if (!watchdog.last_rm || *watchdog.last_rm != rounded_rm) {
      watchdog.last_rm = rounded_rm;
      watchdog.changed_at = std::chrono::steady_clock::now();
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - watchdog.changed_at).count();
  snapshot.watchdog_remaining_seconds = std::max(0, config.capacity_timeout_seconds - static_cast<int>(elapsed));
  snapshot.watchdog_expired = elapsed >= config.capacity_timeout_seconds;

  snapshot.csv_path = latest_csv_file();
  snapshot.csv = parse_latest_csv_row(snapshot.csv_path);
  for (const auto& [key, value] : snapshot.csv) snapshot.raw["csv." + key] = value;
  return snapshot;
}

std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const unsigned char character : value) {
    switch (character) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (character < 0x20) {
          char buffer[7] = {};
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
          escaped += buffer;
        } else {
          escaped.push_back(static_cast<char>(character));
        }
    }
  }
  return escaped;
}

std::string json_string(const std::string& value) { return "\"" + escape_json(value) + "\""; }

std::string json_number(const std::optional<double>& value) {
  if (!value || !std::isfinite(*value)) return "null";
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << *value;
  return output.str();
}

std::string raw_value(const Snapshot& snapshot, const std::string& key) {
  const auto found = snapshot.raw.find(key);
  return found == snapshot.raw.end() ? "" : found->second;
}

std::string snapshot_json(const Snapshot& snapshot, const Stats& stats) {
  std::ostringstream output;
  output << "{\"timestamp\":" << json_string(snapshot.timestamp)
         << ",\"source\":{\"mode\":\"live\",\"csv_path\":" << json_string(snapshot.csv_path) << "}"
         << ",\"device\":{\"market_name\":" << json_string(raw_value(snapshot, "device_name"))
         << ",\"build_id\":" << json_string(raw_value(snapshot, "build_id"))
         << ",\"battery_sn\":" << json_string(raw_value(snapshot, "battery_sn"))
         << ",\"manufacture_date\":" << json_string(raw_value(snapshot, "battery_manu_date")) << "}"
         << ",\"battery\":{\"level\":" << json_number(snapshot.capacity)
         << ",\"voltage_mv\":" << json_number(snapshot.voltage_bat_v ? std::optional<double>(*snapshot.voltage_bat_v * 1000.0) : std::nullopt)
         << ",\"current_ma\":" << json_number(snapshot.current_a ? std::optional<double>(*snapshot.current_a * 1000.0) : std::nullopt)
         << ",\"temperature_c\":" << json_number(snapshot.battery_temp_c)
         << ",\"cell_power_w\":" << json_number(snapshot.cell_power_w)
         << ",\"current_mah\":" << json_number(number(raw_value(snapshot, "battery_rm")))
         << ",\"remaining_to_full_mah\":" << json_number(snapshot.remaining_mah)
         << ",\"full_charge_mah\":" << json_number(number(raw_value(snapshot, "battery_fcc")))
         << ",\"design_capacity_mah\":" << json_number(number(raw_value(snapshot, "design_capacity")))
         << ",\"soh_pct\":" << json_number(number(raw_value(snapshot, "battery_soh")))
         << ",\"health_pct\":" << json_number(snapshot.health_percent)
         << ",\"locked_mah\":" << json_number(snapshot.locked_mah)
         << ",\"locked_pct\":" << json_number(snapshot.locked_percent)
         << ",\"chip_soc\":" << json_number(number(raw_value(snapshot, "chip_soc")))
         << ",\"gauge_soc\":" << json_number(number(raw_value(snapshot, "gauge_soc")))
         << ",\"cycle_count\":" << json_number(number(raw_value(snapshot, "battery_cc")))
         << ",\"status\":" << json_string(snapshot.usb_online ? (snapshot.full ? "已充满" : "充电中") : "未充电") << "}"
         << ",\"charging\":{\"usb_online\":" << (snapshot.usb_online ? "true" : "false")
         << ",\"notify_code\":" << json_string(raw_value(snapshot, "battery_notify_code"))
         << ",\"charge_type\":" << json_string(raw_value(snapshot, "charge_type"))
         << ",\"fast_charge_type\":" << json_string(raw_value(snapshot, "fast_chg_type"))
         << ",\"svooc_flag\":" << json_string(raw_value(snapshot, "svooc_flag"))
         << ",\"mmi_status\":" << json_string(raw_value(snapshot, "chg_mmi_status"))
         << ",\"usb_voltage_mv\":" << json_number(snapshot.voltage_usb_v ? std::optional<double>(*snapshot.voltage_usb_v * 1000.0) : std::nullopt)
         << ",\"usb_voltage_max_mv\":" << json_number(snapshot.voltage_max_v ? std::optional<double>(*snapshot.voltage_max_v * 1000.0) : std::nullopt)
         << ",\"usb_current_ma\":" << json_number(snapshot.input_current_a ? std::optional<double>(*snapshot.input_current_a * 1000.0) : std::nullopt)
         << ",\"usb_current_source\":" << json_string(snapshot.input_current_source)
         << ",\"power_w\":" << json_number(snapshot.power_w)
         << ",\"power_source\":" << json_string(snapshot.power_source)
         << ",\"pps_power_w\":" << json_number(snapshot.pps_w)
         << ",\"eta\":" << json_string(snapshot.eta_text)
         << ",\"bdd_voltdiff_trend\":" << json_string(raw_value(snapshot, "bdd_voltdiff_trend"))
         << ",\"vbat_voltdiff_mv\":" << json_number(number(raw_value(snapshot, "vbat_voltdiff"))) << "}"
         << ",\"thermals\":{\"battery_c\":" << json_number(snapshot.battery_temp_c)
         << ",\"usb_c\":" << json_number(snapshot.usb_temp_c)
         << ",\"vooc_c\":" << json_number(snapshot.vooc_temp_c)
         << ",\"cpu_c\":" << json_number(snapshot.cpu_temp_c)
         << ",\"gpu_c\":" << json_number(snapshot.gpu_temp_c)
         << ",\"shell_c\":" << json_number(snapshot.shell_temp_c) << "}"
         << ",\"statistics\":{\"samples\":" << stats.samples
         << ",\"current_max_a\":" << json_number(stats.samples ? std::optional<double>(stats.max_current_a) : std::nullopt)
         << ",\"current_min_a\":" << json_number(stats.samples ? std::optional<double>(stats.min_current_a) : std::nullopt)
         << ",\"current_avg_a\":" << json_number(stats.samples ? std::optional<double>(stats.sum_current_a / stats.samples) : std::nullopt)
         << ",\"power_max_w\":" << json_number(stats.samples ? std::optional<double>(stats.max_power_w) : std::nullopt)
         << ",\"power_min_w\":" << json_number(stats.samples ? std::optional<double>(stats.min_power_w) : std::nullopt)
         << ",\"power_avg_w\":" << json_number(stats.samples ? std::optional<double>(stats.sum_power_w / stats.samples) : std::nullopt) << "}"
         << ",\"watchdog\":{\"offline_samples\":" << snapshot.offline_samples
         << ",\"capacity_stalled\":" << (snapshot.watchdog_expired ? "true" : "false")
         << ",\"remaining_seconds\":" << snapshot.watchdog_remaining_seconds << "}"
         << ",\"raw\":{";
  bool first = true;
  for (const auto& [key, value] : snapshot.raw) {
    if (!first) output << ',';
    first = false;
    output << json_string(key) << ':' << json_string(value);
  }
  output << "},\"csv\":{";
  first = true;
  for (const auto& [key, value] : snapshot.csv) {
    if (!first) output << ',';
    first = false;
    output << json_string(key) << ':' << json_string(value);
  }
  output << "}}";
  return output.str();
}

// The previous WebSocket transport is deliberately excluded. KernelSU WebUI
// reads the atomically-written snapshot through its privileged Shell bridge.
#if 0
std::array<uint32_t, 5> sha1(std::string_view input) {
  std::vector<uint8_t> bytes(input.begin(), input.end());
  const uint64_t bit_length = static_cast<uint64_t>(bytes.size()) * 8;
  bytes.push_back(0x80);
  while (bytes.size() % 64 != 56) bytes.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<uint8_t>(bit_length >> shift));
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  for (size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::array<uint32_t, 80> words {};
    for (size_t i = 0; i < 16; ++i) {
      words[i] = (static_cast<uint32_t>(bytes[offset + i * 4]) << 24) | (static_cast<uint32_t>(bytes[offset + i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(bytes[offset + i * 4 + 2]) << 8) | bytes[offset + i * 4 + 3];
    }
    for (size_t i = 16; i < 80; ++i) words[i] = std::rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (size_t i = 0; i < 80; ++i) {
      const uint32_t f = i < 20 ? ((b & c) | ((~b) & d)) : i < 40 ? (b ^ c ^ d) : i < 60 ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d);
      const uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
      const uint32_t temp = std::rotl(a, 5) + f + e + k + words[i];
      e = d; d = c; c = std::rotl(b, 30); b = a; a = temp;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }
  return {h0, h1, h2, h3, h4};
}

std::string base64(const std::vector<uint8_t>& bytes) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const uint32_t group = static_cast<uint32_t>(bytes[i]) << 16 | (i + 1 < bytes.size() ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
    output.push_back(alphabet[(group >> 18) & 63]);
    output.push_back(alphabet[(group >> 12) & 63]);
    output.push_back(i + 1 < bytes.size() ? alphabet[(group >> 6) & 63] : '=');
    output.push_back(i + 2 < bytes.size() ? alphabet[group & 63] : '=');
  }
  return output;
}

std::string websocket_accept(std::string_view key) {
  const auto digest = sha1(std::string(key) + kWebSocketGuid);
  std::vector<uint8_t> bytes;
  bytes.reserve(20);
  for (const uint32_t word : digest) {
    bytes.push_back(static_cast<uint8_t>(word >> 24)); bytes.push_back(static_cast<uint8_t>(word >> 16));
    bytes.push_back(static_cast<uint8_t>(word >> 8)); bytes.push_back(static_cast<uint8_t>(word));
  }
  return base64(bytes);
}

bool send_all(int fd, const uint8_t* data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
    const ssize_t result = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
    if (result <= 0) return false;
    sent += static_cast<size_t>(result);
  }
  return true;
}

class WebSocketServer {
 public:
  explicit WebSocketServer(uint16_t port) {
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) throw std::runtime_error("socket() failed");
    const int reuse = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener_, 4) != 0) {
      close(listener_);
      throw std::runtime_error("cannot bind 127.0.0.1:" + std::to_string(port));
    }
  }

  ~WebSocketServer() {
    for (const Client& client : clients_) close(client.fd);
    if (listener_ >= 0) close(listener_);
  }

  void poll_once(int timeout_ms) {
    std::vector<pollfd> fds;
    fds.push_back({listener_, POLLIN, 0});
    for (const Client& client : clients_) fds.push_back({client.fd, POLLIN, 0});
    const int ready = poll(fds.data(), fds.size(), timeout_ms);
    if (ready <= 0) return;
    if (fds[0].revents & POLLIN) accept_client();
    for (size_t index = 0; index < clients_.size(); ++index) {
      const short events = fds[index + 1].revents;
      if (events & (POLLERR | POLLHUP | POLLNVAL)) clients_[index].fd = -1;
      if (events & POLLIN) read_client(clients_[index]);
    }
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const Client& client) { if (client.fd >= 0) return false; return true; }), clients_.end());
  }

  void broadcast(const std::string& text) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81);
    if (text.size() <= 125) {
      frame.push_back(static_cast<uint8_t>(text.size()));
    } else if (text.size() <= 65535) {
      frame.push_back(126);
      frame.push_back(static_cast<uint8_t>(text.size() >> 8));
      frame.push_back(static_cast<uint8_t>(text.size()));
    } else {
      return;
    }
    frame.insert(frame.end(), text.begin(), text.end());
    for (Client& client : clients_) {
      if (client.ready && !send_all(client.fd, frame.data(), frame.size())) client.fd = -1;
    }
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const Client& client) { return client.fd < 0; }), clients_.end());
  }

 private:
  int listener_ = -1;
  std::vector<Client> clients_;

  void accept_client() {
    sockaddr_in address {};
    socklen_t length = sizeof(address);
    const int fd = accept(listener_, reinterpret_cast<sockaddr*>(&address), &length);
    if (fd < 0) return;
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    clients_.push_back({fd, false, {}});
  }

  static std::string header_value(const std::string& request, const std::string& name) {
    const std::string needle = name + ":";
    const size_t start = request.find(needle);
    if (start == std::string::npos) return {};
    const size_t value_start = start + needle.size();
    const size_t end = request.find("\r\n", value_start);
    return trim(request.substr(value_start, end == std::string::npos ? std::string::npos : end - value_start));
  }

  void read_client(Client& client) {
    std::array<char, 4096> buffer {};
    while (true) {
      const ssize_t count = recv(client.fd, buffer.data(), buffer.size(), 0);
      if (count == 0) { close(client.fd); client.fd = -1; return; }
      if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        close(client.fd); client.fd = -1; return;
      }
      if (!client.ready) client.request.append(buffer.data(), static_cast<size_t>(count));
      if (client.request.size() > 8192) { close(client.fd); client.fd = -1; return; }
    }
    if (client.ready || client.request.find("\r\n\r\n") == std::string::npos) return;
    const std::string key = header_value(client.request, "Sec-WebSocket-Key");
    if (key.empty()) { close(client.fd); client.fd = -1; return; }
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + websocket_accept(key) + "\r\n\r\n";
    if (!send_all(client.fd, reinterpret_cast<const uint8_t*>(response.data()), response.size())) { close(client.fd); client.fd = -1; return; }
    client.ready = true;
    client.request.clear();
  }
};

#endif

bool write_state_file(const std::string& path, const std::string& json) {
  const std::string temporary = path + ".tmp";
  const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return false;
  size_t offset = 0;
  while (offset < json.size()) {
    const ssize_t written = write(fd, json.data() + offset, json.size() - offset);
    if (written <= 0) { close(fd); unlink(temporary.c_str()); return false; }
    offset += static_cast<size_t>(written);
  }
  const bool flushed = fsync(fd) == 0;
  close(fd);
  if (!flushed || rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    return false;
  }
  return true;
}

void handle_signal(int) { g_running = false; }

void print_usage() {
  std::cout << "Usage: chg_daemon [--state-file path] [--interval seconds] [--offline-interval seconds] [--cell-type 0|1] [--input-current-node path] [--ppschg-unit auto|uw|mw] [--capacity-timeout seconds] [--offline-action slow|exit] [--stalled-action continue|exit]\n";
}

bool parse_args(int argc, char** argv, Config& config) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument == "--help") { print_usage(); return false; }
    if (index + 1 >= argc) return false;
    const std::string value(argv[++index]);
    if (argument == "--state-file") {
      if (value.empty() || value.front() != '/') return false;
      config.state_file = value;
    } else if (argument == "--input-current-node") {
      if (value.empty() || value.front() != '/') return false;
      config.input_current_node = value;
    } else if (argument == "--ppschg-unit") {
      if (value != "auto" && value != "uw" && value != "mw") return false;
      config.pps_power_unit = value;
    } else if (argument == "--interval") {
      const auto parsed = integer(value); if (!parsed || *parsed < 1) return false; config.interval_seconds = static_cast<int>(*parsed);
    } else if (argument == "--offline-interval") {
      const auto parsed = integer(value); if (!parsed || *parsed < 1) return false; config.offline_interval_seconds = static_cast<int>(*parsed);
    } else if (argument == "--capacity-timeout") {
      const auto parsed = integer(value); if (!parsed || *parsed < 1) return false; config.capacity_timeout_seconds = static_cast<int>(*parsed);
    } else if (argument == "--cell-type") {
      const auto parsed = integer(value); if (!parsed || (*parsed != 0 && *parsed != 1)) return false; config.cell_type = static_cast<int>(*parsed);
    } else if (argument == "--offline-action") {
      if (value == "slow") config.exit_when_offline = false; else if (value == "exit") config.exit_when_offline = true; else return false;
    } else if (argument == "--stalled-action") {
      if (value == "continue") config.exit_when_capacity_stalled = false; else if (value == "exit") config.exit_when_capacity_stalled = true; else return false;
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  if (!parse_args(argc, argv, config)) return argc > 1 && std::string(argv[1]) == "--help" ? 0 : 2;
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  try {
    const ThermalPaths paths = discover_thermal_paths();
    Stats stats;
    Watchdog watchdog;
    auto next_sample = std::chrono::steady_clock::now();
    while (g_running) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_sample) {
        const Snapshot snapshot = collect(paths, config, stats, watchdog);
        if (!write_state_file(config.state_file, snapshot_json(snapshot, stats))) {
          std::cerr << "O-Pulse daemon: cannot write " << config.state_file << '\n';
        }
        if (config.exit_when_offline && watchdog.offline_samples >= 2) break;
        if (config.exit_when_capacity_stalled && snapshot.watchdog_expired) break;
        const int seconds = snapshot.usb_online ? config.interval_seconds : config.offline_interval_seconds;
        next_sample = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next_sample - std::chrono::steady_clock::now()).count();
      usleep(static_cast<useconds_t>(std::clamp<long long>(remaining, 1, 1000) * 1000));
    }
  } catch (const std::exception& error) {
    std::cerr << "O-Pulse daemon: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
