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
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace {

constexpr char kThermalRoot[] = "/sys/devices/virtual/thermal";
constexpr char kCsvRoot[] = "/data/vendor/battery";
constexpr int kDefaultInterval = 3;
constexpr int kDefaultOfflineInterval = 5;
constexpr int kDefaultCapacityTimeout = 123;
std::atomic<bool> running{true};

struct Config {
  int interval_seconds = kDefaultInterval;
  int offline_interval_seconds = kDefaultOfflineInterval;
  int capacity_timeout_seconds = kDefaultCapacityTimeout;
  int cell_type = 1;
  std::string state_file = "/data/adb/modules/o_pulse/run/state.json";
  std::string input_current_node;
  std::string pps_unit = "auto";
};

struct ThermalPaths {
  std::string usb;
  std::string vooc;
  std::string cpu;
  std::string gpu;
  std::string shell;
};

struct Statistics {
  uint64_t samples = 0;
  double current_sum = 0.0;
  double current_min = 0.0;
  double current_max = 0.0;
  double power_sum = 0.0;
  double power_min = 0.0;
  double power_max = 0.0;

  void add(double current, double power) {
    if (samples == 0) {
      current_min = current;
      current_max = current;
      power_min = power;
      power_max = power;
    } else {
      current_min = std::min(current_min, current);
      current_max = std::max(current_max, current);
      power_min = std::min(power_min, power);
      power_max = std::max(power_max, power);
    }
    current_sum += current;
    power_sum += power;
    ++samples;
  }
};

struct Watchdog {
  std::optional<long long> last_capacity;
  std::chrono::steady_clock::time_point changed_at = std::chrono::steady_clock::now();
  int offline_samples = 0;
};

struct Snapshot {
  std::map<std::string, std::string> raw;
  std::map<std::string, std::string> csv;
  std::string timestamp;
  std::string csv_path;
  bool usb_online = false;
  bool full = false;
  int offline_samples = 0;
  int watchdog_remaining_seconds = 0;
  bool capacity_stalled = false;
  std::optional<double> level;
  std::optional<double> battery_voltage_v;
  std::optional<double> usb_voltage_v;
  std::optional<double> usb_voltage_max_v;
  std::optional<double> battery_current_a;
  std::optional<double> usb_current_a;
  std::optional<double> power_w;
  std::optional<double> cell_power_w;
  std::optional<double> pps_power_w;
  std::optional<double> battery_temp_c;
  std::optional<double> usb_temp_c;
  std::optional<double> vooc_temp_c;
  std::optional<double> cpu_temp_c;
  std::optional<double> gpu_temp_c;
  std::optional<double> shell_temp_c;
  std::optional<double> remaining_mah;
  std::optional<double> locked_pct;
  std::optional<double> health_pct;
  std::string power_source = "unavailable";
  std::string pps_unit = "unavailable";
  std::string eta = "charger disconnected";
};

std::string trim(std::string value) {
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string read_node(const std::string& path, const std::string& fallback = {}) {
  if (path.empty()) return fallback;
  std::ifstream input(path);
  if (!input) return fallback;
  std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  value = trim(value);
  return value.empty() ? fallback : value;
}

std::optional<double> parse_number(const std::string& value) {
  if (value.empty() || value == "N/A" || value == "--") return std::nullopt;
  char* end = nullptr;
  errno = 0;
  const double result = std::strtod(value.c_str(), &end);
  if (errno != 0 || end == value.c_str() || *end != '\0' || !std::isfinite(result)) return std::nullopt;
  return result;
}

std::optional<long long> parse_integer(const std::string& value) {
  if (value.empty() || value == "N/A" || value == "--") return std::nullopt;
  char* end = nullptr;
  errno = 0;
  const long long result = std::strtoll(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0') return std::nullopt;
  return result;
}

std::optional<double> divide(const std::optional<double>& value, double divisor) {
  if (!value || divisor == 0.0) return std::nullopt;
  return *value / divisor;
}

std::optional<double> multiply(const std::optional<double>& left, const std::optional<double>& right) {
  if (!left || !right) return std::nullopt;
  return *left * *right;
}

std::string android_property(const char* key) {
#if defined(__ANDROID__)
  char value[PROP_VALUE_MAX] = {};
  return __system_property_get(key, value) > 0 ? value : "N/A";
#else
  (void)key;
  return "N/A";
#endif
}

std::string iso_timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm utc {};
  gmtime_r(&now, &utc);
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}

std::string thermal_node(std::string_view keyword) {
  DIR* root = opendir(kThermalRoot);
  if (root == nullptr) return {};
  std::string result;
  while (const dirent* entry = readdir(root)) {
    const std::string name(entry->d_name);
    if (name.rfind("thermal_zone", 0) != 0) continue;
    const std::string base = std::string(kThermalRoot) + "/" + name;
    if (read_node(base + "/type").find(keyword) != std::string::npos) {
      result = base + "/temp";
      break;
    }
  }
  closedir(root);
  return result;
}

ThermalPaths discover_thermal() {
  ThermalPaths paths;
  paths.usb = thermal_node("usb");
  paths.vooc = thermal_node("svooc_mos_btb_usr");
  paths.cpu = thermal_node("cpu-1-0-usr");
  if (paths.cpu.empty()) paths.cpu = thermal_node("cpu-0-0-usr");
  paths.gpu = thermal_node("gpu-usr");
  paths.shell = thermal_node("shell_front");
  if (paths.shell.empty()) paths.shell = thermal_node("quiet_therm");
  return paths;
}

std::optional<double> raw_to_celsius(const std::string& value) {
  const auto raw = parse_number(value);
  if (!raw) return std::nullopt;
  return *raw > 1000.0 || *raw < -1000.0 ? divide(raw, 1000.0) : divide(raw, 100.0);
}

std::optional<double> raw_to_usb_current(const std::string& value, std::string& unit) {
  const auto raw = parse_number(value);
  if (!raw) {
    unit = "unavailable";
    return std::nullopt;
  }
  const double absolute = std::abs(*raw);
  if (absolute >= 10000.0) {
    unit = "uA";
    return absolute / 1000000.0;
  }
  unit = "mA";
  return absolute / 1000.0;
}

std::optional<double> raw_to_pps(const std::string& value, const std::string& requested, std::string& unit) {
  const auto raw = parse_number(value);
  if (!raw || *raw <= 0.0) {
    unit = "unavailable";
    return std::nullopt;
  }
  if (requested == "uw" || (requested == "auto" && *raw > 200000.0)) {
    unit = "uW";
    return *raw / 1000000.0;
  }
  unit = "mW";
  return *raw / 1000.0;
}

std::string normalize_header(std::string value) {
  std::string result;
  for (const unsigned char character : value) {
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_') {
      result.push_back(static_cast<char>(character));
    }
  }
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
        current.push_back('"');
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

std::string latest_csv() {
  DIR* root = opendir(kCsvRoot);
  if (root == nullptr) return {};
  std::string result;
  std::time_t newest = 0;
  while (const dirent* entry = readdir(root)) {
    const std::string name(entry->d_name);
    if (name.rfind("battery-log-", 0) != 0 || name.size() < 4 || name.substr(name.size() - 4) != ".csv") continue;
    const std::string path = std::string(kCsvRoot) + "/" + name;
    struct stat info {};
    if (stat(path.c_str(), &info) == 0 && info.st_mtime >= newest) {
      newest = info.st_mtime;
      result = path;
    }
  }
  closedir(root);
  return result;
}

std::map<std::string, std::string> latest_csv_row(const std::string& path) {
  std::map<std::string, std::string> result;
  if (path.empty()) return result;
  std::ifstream input(path);
  if (!input) return result;
  std::string header;
  if (!std::getline(input, header)) return result;
  std::string line;
  std::string last;
  while (std::getline(input, line)) {
    if (!trim(line).empty()) last = line;
  }
  if (last.empty()) return result;
  const auto headers = split_csv(header);
  const auto values = split_csv(last);
  for (size_t index = 0; index < headers.size() && index < values.size(); ++index) {
    const std::string key = normalize_header(headers[index]);
    if (!key.empty()) result[key] = values[index];
  }
  return result;
}

void put(Snapshot& snapshot, const std::string& key, const std::string& path, const std::string& fallback = {}) {
  snapshot.raw[key] = read_node(path, fallback);
}

Snapshot collect(const ThermalPaths& thermal, const Config& config, Statistics& stats, Watchdog& watchdog) {
  Snapshot snapshot;
  snapshot.timestamp = iso_timestamp();
  snapshot.raw["device_name"] = android_property("ro.vendor.oplus.market.name");
  snapshot.raw["build_id"] = android_property("ro.build.display.id");
  put(snapshot, "battery_sn", "/sys/class/oplus_chg/battery/battery_sn", "N/A");
  put(snapshot, "battery_manu_date", "/sys/class/oplus_chg/battery/battery_manu_date", "N/A");
  put(snapshot, "usb_online", "/sys/class/power_supply/usb/online", "0");
  put(snapshot, "battery_notify_code", "/sys/class/oplus_chg/battery/battery_notify_code", "0");
  put(snapshot, "charge_type", "/sys/class/power_supply/battery/charge_type", "N/A");
  put(snapshot, "fast_chg_type", "/sys/class/oplus_chg/battery/fast_chg_type", "0");
  put(snapshot, "svooc_flag", "/sys/class/oplus_chg/battery/svooc_flag", "0");
  put(snapshot, "chg_mmi_status", "/sys/class/oplus_chg/battery/chg_mmi_status", "0");
  put(snapshot, "capacity", "/sys/class/power_supply/battery/capacity", "");
  put(snapshot, "chip_soc", "/sys/class/oplus_chg/battery/chip_soc", "--");
  put(snapshot, "gauge_soc", "/sys/class/oplus_chg/battery/gauge_soc", "--");
  put(snapshot, "battery_rm", "/sys/class/oplus_chg/battery/battery_rm", "N/A");
  put(snapshot, "battery_fcc", "/sys/class/oplus_chg/battery/battery_fcc", "N/A");
  put(snapshot, "design_capacity", "/sys/class/oplus_chg/battery/design_capacity", "N/A");
  put(snapshot, "battery_soh", "/sys/class/oplus_chg/battery/battery_soh", "N/A");
  put(snapshot, "battery_cc", "/sys/class/oplus_chg/battery/battery_cc", "--");
  put(snapshot, "usb_voltage_max", "/sys/class/power_supply/usb/voltage_max", "N/A");
  put(snapshot, "battery_voltage_now", "/sys/class/power_supply/battery/voltage_now", "N/A");
  put(snapshot, "usb_voltage_now", "/sys/class/power_supply/usb/voltage_now", "N/A");
  put(snapshot, "battery_current_now", "/sys/class/power_supply/battery/current_now", "N/A");
  const std::string usb_current_path = config.input_current_node.empty() ? "/sys/class/power_supply/usb/current_now" : config.input_current_node;
  put(snapshot, "usb_current_now", usb_current_path, "N/A");
  snapshot.raw["input_current_source"] = usb_current_path;
  put(snapshot, "ppschg_power", "/sys/devices/virtual/oplus_chg/battery/ppschg_power", "N/A");
  put(snapshot, "bdd_voltdiff_trend", "/sys/class/oplus_chg/battery/bdd_voltdiff_trend", "N/A");
  put(snapshot, "vbat_voltdiff", "/sys/class/oplus_chg/battery/vbat_voltdiff", "N/A");
  put(snapshot, "battery_temp", "/sys/class/power_supply/battery/temp", "N/A");
  snapshot.raw["thermal_usb_path"] = thermal.usb;
  snapshot.raw["thermal_vooc_path"] = thermal.vooc;
  snapshot.raw["thermal_cpu_path"] = thermal.cpu;
  snapshot.raw["thermal_gpu_path"] = thermal.gpu;
  snapshot.raw["thermal_shell_path"] = thermal.shell;
  snapshot.raw["thermal_usb_raw"] = read_node(thermal.usb, "N/A");
  snapshot.raw["thermal_vooc_raw"] = read_node(thermal.vooc, "N/A");
  snapshot.raw["thermal_cpu_raw"] = read_node(thermal.cpu, "N/A");
  snapshot.raw["thermal_gpu_raw"] = read_node(thermal.gpu, "N/A");
  snapshot.raw["thermal_shell_raw"] = read_node(thermal.shell, "N/A");

  const auto rm = parse_number(snapshot.raw["battery_rm"]);
  const auto fcc = parse_number(snapshot.raw["battery_fcc"]);
  const auto design = parse_number(snapshot.raw["design_capacity"]);
  snapshot.level = parse_number(snapshot.raw["capacity"]);
  if (!snapshot.level && rm && fcc && *fcc > 0.0) snapshot.level = *rm * 100.0 / *fcc;
  snapshot.battery_voltage_v = divide(parse_number(snapshot.raw["battery_voltage_now"]), 1000000.0);
  snapshot.usb_voltage_v = divide(parse_number(snapshot.raw["usb_voltage_now"]), 1000000.0);
  snapshot.usb_voltage_max_v = divide(parse_number(snapshot.raw["usb_voltage_max"]), 1000000.0);
  const auto battery_current_raw = parse_number(snapshot.raw["battery_current_now"]);
  if (battery_current_raw) snapshot.battery_current_a = std::abs(*battery_current_raw) * (config.cell_type == 1 ? 2.0 : 1.0) / 1000.0;
  std::string usb_current_unit;
  snapshot.usb_current_a = raw_to_usb_current(snapshot.raw["usb_current_now"], usb_current_unit);
  snapshot.raw["input_current_unit"] = usb_current_unit;
  snapshot.cell_power_w = multiply(snapshot.battery_voltage_v, snapshot.battery_current_a);
  snapshot.pps_power_w = raw_to_pps(snapshot.raw["ppschg_power"], config.pps_unit, snapshot.pps_unit);

  const auto raw_online = parse_integer(snapshot.raw["usb_online"]);
  snapshot.usb_online = raw_online && *raw_online == 1;
  snapshot.raw["usb_online_standard"] = snapshot.usb_online ? "1" : "0";
  if (snapshot.pps_power_w && *snapshot.pps_power_w >= 0.5 && *snapshot.pps_power_w <= 300.0) {
    snapshot.power_w = snapshot.pps_power_w;
    snapshot.power_source = "ppschg_power";
  } else if (snapshot.usb_voltage_v && snapshot.usb_current_a && *snapshot.usb_voltage_v > 0.0 && *snapshot.usb_current_a > 0.0) {
    snapshot.power_w = *snapshot.usb_voltage_v * *snapshot.usb_current_a;
    snapshot.power_source = "usb_voltage_current";
  } else if (snapshot.cell_power_w) {
    snapshot.power_w = snapshot.cell_power_w;
    snapshot.power_source = "battery_cell_estimate";
  }

  snapshot.battery_temp_c = divide(parse_number(snapshot.raw["battery_temp"]), 10.0);
  snapshot.usb_temp_c = raw_to_celsius(snapshot.raw["thermal_usb_raw"]);
  snapshot.vooc_temp_c = raw_to_celsius(snapshot.raw["thermal_vooc_raw"]);
  snapshot.cpu_temp_c = raw_to_celsius(snapshot.raw["thermal_cpu_raw"]);
  snapshot.gpu_temp_c = raw_to_celsius(snapshot.raw["thermal_gpu_raw"]);
  snapshot.shell_temp_c = raw_to_celsius(snapshot.raw["thermal_shell_raw"]);
  if (rm && fcc) {
    snapshot.remaining_mah = std::max(0.0, *fcc - *rm);
    if (*fcc > 0.0) snapshot.locked_pct = *snapshot.remaining_mah * 100.0 / *fcc;
  }
  if (fcc && design && *design > 0.0) snapshot.health_pct = *fcc * 100.0 / *design;
  snapshot.full = parse_integer(snapshot.raw["battery_notify_code"]).value_or(0) != 0;

  if (snapshot.usb_online && !snapshot.full && snapshot.remaining_mah && snapshot.battery_current_a && *snapshot.battery_current_a > 0.1) {
    const int minutes = static_cast<int>(std::round(*snapshot.remaining_mah / (*snapshot.battery_current_a * 1000.0) * 60.0));
    snapshot.eta = minutes >= 60 ? std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m" : std::to_string(minutes) + "m";
  } else if (snapshot.full) {
    snapshot.eta = "fully charged";
  } else if (snapshot.usb_online) {
    snapshot.eta = "trickle charging";
  }

  if (snapshot.power_w && snapshot.battery_current_a) stats.add(*snapshot.battery_current_a, *snapshot.power_w);
  if (snapshot.usb_online) snapshot.offline_samples = watchdog.offline_samples = 0;
  else snapshot.offline_samples = ++watchdog.offline_samples;
  if (rm) {
    const long long rounded = static_cast<long long>(*rm);
    if (!watchdog.last_capacity || *watchdog.last_capacity != rounded) {
      watchdog.last_capacity = rounded;
      watchdog.changed_at = std::chrono::steady_clock::now();
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - watchdog.changed_at).count();
  snapshot.watchdog_remaining_seconds = std::max(0, config.capacity_timeout_seconds - static_cast<int>(elapsed));
  snapshot.capacity_stalled = elapsed >= config.capacity_timeout_seconds;
  snapshot.csv_path = latest_csv();
  snapshot.csv = latest_csv_row(snapshot.csv_path);
  for (const auto& [key, value] : snapshot.csv) snapshot.raw["csv." + key] = value;
  return snapshot;
}

std::string json_escape(std::string_view value) {
  std::string result;
  for (const unsigned char character : value) {
    if (character == '"') result += "\\\"";
    else if (character == '\\') result += "\\\\";
    else if (character == '\n') result += "\\n";
    else if (character == '\r') result += "\\r";
    else if (character == '\t') result += "\\t";
    else if (character < 0x20) {
      char buffer[7] = {};
      std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
      result += buffer;
    } else result.push_back(static_cast<char>(character));
  }
  return result;
}

std::string json_string(const std::string& value) { return "\"" + json_escape(value) + "\""; }

std::string json_number(const std::optional<double>& value) {
  if (!value || !std::isfinite(*value)) return "null";
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << *value;
  return output.str();
}

std::string raw(const Snapshot& snapshot, const std::string& key) {
  const auto found = snapshot.raw.find(key);
  return found == snapshot.raw.end() ? "N/A" : found->second;
}

void write_map(std::ostringstream& output, const std::map<std::string, std::string>& values) {
  bool first = true;
  for (const auto& [key, value] : values) {
    if (!first) output << ',';
    first = false;
    output << json_string(key) << ':' << json_string(value);
  }
}

std::string state_json(const Snapshot& snapshot, const Statistics& stats) {
  std::ostringstream output;
  output << "{\"timestamp\":" << json_string(snapshot.timestamp)
         << ",\"source\":{\"mode\":\"live\",\"csv_path\":" << json_string(snapshot.csv_path) << "}"
         << ",\"device\":{\"market_name\":" << json_string(raw(snapshot, "device_name"))
         << ",\"build_id\":" << json_string(raw(snapshot, "build_id"))
         << ",\"battery_sn\":" << json_string(raw(snapshot, "battery_sn"))
         << ",\"manufacture_date\":" << json_string(raw(snapshot, "battery_manu_date")) << "}"
         << ",\"battery\":{\"level\":" << json_number(snapshot.level)
         << ",\"voltage_mv\":" << json_number(multiply(snapshot.battery_voltage_v, std::optional<double>(1000.0)))
         << ",\"current_ma\":" << json_number(multiply(snapshot.battery_current_a, std::optional<double>(1000.0)))
         << ",\"temperature_c\":" << json_number(snapshot.battery_temp_c)
         << ",\"cell_power_w\":" << json_number(snapshot.cell_power_w)
         << ",\"current_mah\":" << json_number(parse_number(raw(snapshot, "battery_rm")))
         << ",\"remaining_to_full_mah\":" << json_number(snapshot.remaining_mah)
         << ",\"full_charge_mah\":" << json_number(parse_number(raw(snapshot, "battery_fcc")))
         << ",\"design_capacity_mah\":" << json_number(parse_number(raw(snapshot, "design_capacity")))
         << ",\"soh_pct\":" << json_number(parse_number(raw(snapshot, "battery_soh")))
         << ",\"health_pct\":" << json_number(snapshot.health_pct)
         << ",\"locked_mah\":" << json_number(snapshot.remaining_mah)
         << ",\"locked_pct\":" << json_number(snapshot.locked_pct)
         << ",\"chip_soc\":" << json_number(parse_number(raw(snapshot, "chip_soc")))
         << ",\"gauge_soc\":" << json_number(parse_number(raw(snapshot, "gauge_soc")))
         << ",\"cycle_count\":" << json_number(parse_number(raw(snapshot, "battery_cc")))
         << ",\"status\":" << json_string(snapshot.usb_online ? (snapshot.full ? "Fully charged" : "Charging") : "Not charging") << "}"
         << ",\"charging\":{\"usb_online\":" << (snapshot.usb_online ? "true" : "false")
         << ",\"notify_code\":" << json_string(raw(snapshot, "battery_notify_code"))
         << ",\"charge_type\":" << json_string(raw(snapshot, "charge_type"))
         << ",\"fast_charge_type\":" << json_string(raw(snapshot, "fast_chg_type"))
         << ",\"svooc_flag\":" << json_string(raw(snapshot, "svooc_flag"))
         << ",\"mmi_status\":" << json_string(raw(snapshot, "chg_mmi_status"))
         << ",\"usb_voltage_mv\":" << json_number(multiply(snapshot.usb_voltage_v, std::optional<double>(1000.0)))
         << ",\"usb_voltage_max_mv\":" << json_number(multiply(snapshot.usb_voltage_max_v, std::optional<double>(1000.0)))
         << ",\"usb_current_ma\":" << json_number(multiply(snapshot.usb_current_a, std::optional<double>(1000.0)))
         << ",\"usb_current_source\":" << json_string(raw(snapshot, "input_current_source"))
         << ",\"power_w\":" << json_number(snapshot.power_w)
         << ",\"power_source\":" << json_string(snapshot.power_source)
         << ",\"pps_power_w\":" << json_number(snapshot.pps_power_w)
         << ",\"eta\":" << json_string(snapshot.eta)
         << ",\"bdd_voltdiff_trend\":" << json_string(raw(snapshot, "bdd_voltdiff_trend"))
         << ",\"vbat_voltdiff_mv\":" << json_number(parse_number(raw(snapshot, "vbat_voltdiff"))) << "}"
         << ",\"thermals\":{\"battery_c\":" << json_number(snapshot.battery_temp_c)
         << ",\"usb_c\":" << json_number(snapshot.usb_temp_c)
         << ",\"vooc_c\":" << json_number(snapshot.vooc_temp_c)
         << ",\"cpu_c\":" << json_number(snapshot.cpu_temp_c)
         << ",\"gpu_c\":" << json_number(snapshot.gpu_temp_c)
         << ",\"shell_c\":" << json_number(snapshot.shell_temp_c) << "}"
         << ",\"statistics\":{\"samples\":" << stats.samples
         << ",\"current_max_a\":" << json_number(stats.samples ? std::optional<double>(stats.current_max) : std::nullopt)
         << ",\"current_min_a\":" << json_number(stats.samples ? std::optional<double>(stats.current_min) : std::nullopt)
         << ",\"current_avg_a\":" << json_number(stats.samples ? std::optional<double>(stats.current_sum / stats.samples) : std::nullopt)
         << ",\"power_max_w\":" << json_number(stats.samples ? std::optional<double>(stats.power_max) : std::nullopt)
         << ",\"power_min_w\":" << json_number(stats.samples ? std::optional<double>(stats.power_min) : std::nullopt)
         << ",\"power_avg_w\":" << json_number(stats.samples ? std::optional<double>(stats.power_sum / stats.samples) : std::nullopt) << "}"
         << ",\"watchdog\":{\"offline_samples\":" << snapshot.offline_samples
         << ",\"capacity_stalled\":" << (snapshot.capacity_stalled ? "true" : "false")
         << ",\"remaining_seconds\":" << snapshot.watchdog_remaining_seconds << "}"
         << ",\"raw\":{";
  write_map(output, snapshot.raw);
  output << "},\"csv\":{";
  write_map(output, snapshot.csv);
  output << "}}";
  return output.str();
}

bool mkdirs_for(const std::string& path) {
  const size_t last_slash = path.find_last_of('/');
  if (last_slash == std::string::npos || last_slash == 0) return true;
  const std::string directory = path.substr(0, last_slash);
  std::string current;
  for (size_t index = 0; index < directory.size(); ++index) {
    current.push_back(directory[index]);
    if (directory[index] == '/' && current.size() > 1) mkdir(current.c_str(), 0755);
  }
  mkdir(directory.c_str(), 0755);
  return true;
}

bool write_state(const std::string& path, const std::string& contents) {
  mkdirs_for(path);
  const std::string temporary = path + ".tmp";
  const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return false;
  size_t offset = 0;
  while (offset < contents.size()) {
    const ssize_t written = write(fd, contents.data() + offset, contents.size() - offset);
    if (written <= 0) {
      close(fd);
      unlink(temporary.c_str());
      return false;
    }
    offset += static_cast<size_t>(written);
  }
  const bool synced = fsync(fd) == 0;
  close(fd);
  if (!synced || rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    return false;
  }
  return true;
}

void handle_signal(int) { running = false; }

void usage() {
  std::cout << "Usage: chg_daemon [--state-file path] [--interval seconds] [--offline-interval seconds] "
               "[--cell-type 0|1] [--input-current-node path] [--ppschg-unit auto|uw|mw] [--capacity-timeout seconds]\n";
}

bool parse_args(int argc, char** argv, Config& config) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument == "--help") {
      usage();
      return false;
    }
    if (index + 1 >= argc) return false;
    const std::string value(argv[++index]);
    if (argument == "--state-file") {
      if (value.empty() || value.front() != '/') return false;
      config.state_file = value;
    } else if (argument == "--interval") {
      const auto parsed = parse_integer(value);
      if (!parsed || *parsed < 1) return false;
      config.interval_seconds = static_cast<int>(*parsed);
    } else if (argument == "--offline-interval") {
      const auto parsed = parse_integer(value);
      if (!parsed || *parsed < 1) return false;
      config.offline_interval_seconds = static_cast<int>(*parsed);
    } else if (argument == "--capacity-timeout") {
      const auto parsed = parse_integer(value);
      if (!parsed || *parsed < 1) return false;
      config.capacity_timeout_seconds = static_cast<int>(*parsed);
    } else if (argument == "--cell-type") {
      const auto parsed = parse_integer(value);
      if (!parsed || (*parsed != 0 && *parsed != 1)) return false;
      config.cell_type = static_cast<int>(*parsed);
    } else if (argument == "--input-current-node") {
      if (value.empty() || value.front() != '/') return false;
      config.input_current_node = value;
    } else if (argument == "--ppschg-unit") {
      if (value != "auto" && value != "uw" && value != "mw") return false;
      config.pps_unit = value;
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
  const ThermalPaths thermal = discover_thermal();
  Statistics stats;
  Watchdog watchdog;
  std::cerr << "O-Pulse native collector started; state=" << config.state_file << "\n";
  auto next_sample = std::chrono::steady_clock::now();
  while (running) {
    if (std::chrono::steady_clock::now() >= next_sample) {
      const Snapshot snapshot = collect(thermal, config, stats, watchdog);
      if (!write_state(config.state_file, state_json(snapshot, stats))) {
        std::cerr << "O-Pulse native collector cannot write state file: " << std::strerror(errno) << "\n";
      }
      const int delay = snapshot.usb_online ? config.interval_seconds : config.offline_interval_seconds;
      next_sample = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
    }
    usleep(100000);
  }
  std::cerr << "O-Pulse native collector stopped\n";
  return 0;
}
