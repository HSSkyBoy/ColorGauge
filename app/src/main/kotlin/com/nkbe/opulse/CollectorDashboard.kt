package com.nkbe.opulse

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.weight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import org.json.JSONObject
import top.yukonga.miuix.kmp.basic.Button
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.basic.TopAppBar

private val Accent = Color(0xFFFF9F43)
private val Positive = Color(0xFF34C759)
private val Warning = Color(0xFFFFB020)
private val Error = Color(0xFFFF5C5C)

@Composable
fun CollectorDashboard(shell: RootShell) {
    var refreshToken by remember { mutableIntStateOf(0) }
    var snapshot by remember { mutableStateOf<CollectorSnapshot?>(null) }
    var error by remember { mutableStateOf<String?>(null) }
    var refreshing by remember { mutableStateOf(false) }

    LaunchedEffect(refreshToken) {
        refreshing = true
        try {
            val result = withContext(Dispatchers.IO) {
                val started = shell.ensureCollector(forceRestart = refreshToken == 0)
                if (!started.isSuccess) {
                    ShellResult(started.exitCode, "Collector setup failed: ${started.output}")
                } else {
                    shell.readState()
                }
            }
            if (result.isSuccess) {
                snapshot = parseSnapshot(result.output)
                error = null
            } else {
                error = result.output.ifBlank { "Unable to read collector state" }
            }
        } catch (cancelled: kotlinx.coroutines.CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            error = failure.message ?: "Unable to read collector state"
        } finally {
            refreshing = false
        }
    }

    LaunchedEffect(Unit) {
        while (isActive) {
            delay(3_000)
            refreshToken += 1
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = "O-Pulse",
                subtitle = snapshot?.deviceSubtitle ?: "Root battery monitor",
                actions = {
                    TextButton(
                        text = if (refreshing) "读取中" else "刷新",
                        enabled = !refreshing,
                        onClick = { refreshToken += 1 },
                    )
                },
            )
        },
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (error != null) {
                item {
                    ErrorCard(
                        message = error!!,
                        onRetry = { refreshToken += 1 },
                        enabled = !refreshing,
                    )
                }
            }

            val current = snapshot
            if (current == null) {
                item { LoadingCard(refreshing) }
            } else {
                item { OverviewCard(current) }
                item { BatteryCard(current) }
                item { ChargingCard(current) }
                item { ThermalCard(current) }
                item { StatisticsCard(current) }
            }
        }
    }
}

@Composable
private fun LoadingCard(refreshing: Boolean) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Text(
            text = if (refreshing) "正在连接 root 并读取 collector 状态..." else "等待 collector 状态...",
            fontSize = 16.sp,
        )
        Text(
            text = "页面已改为原生 Miuix，不再通过 WebView 读取数据。",
            modifier = Modifier.padding(top = 6.dp),
            color = Color.Gray,
            fontSize = 13.sp,
        )
    }
}

@Composable
private fun ErrorCard(message: String, onRetry: () -> Unit, enabled: Boolean) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Text(text = "Collector 读取失败", color = Error, fontSize = 17.sp)
        Text(
            text = message,
            modifier = Modifier.padding(top = 8.dp),
            color = Color.Gray,
            fontSize = 13.sp,
        )
        Button(
            onClick = onRetry,
            enabled = enabled,
            modifier = Modifier.padding(top = 12.dp),
        ) {
            Text("重试")
        }
    }
}

@Composable
private fun OverviewCard(data: CollectorSnapshot) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.weight(1f)) {
                Text(text = "电量", color = Color.Gray, fontSize = 13.sp)
                Text(text = data.levelPercent, color = Accent, fontSize = 34.sp)
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(text = "当前状态", color = Color.Gray, fontSize = 13.sp)
                Text(
                    text = data.status,
                    color = if (data.usbOnline) Positive else Color.Gray,
                    fontSize = 20.sp,
                )
                Text(text = data.updatedAt, color = Color.Gray, fontSize = 12.sp)
            }
        }
        MetricRow(
            label1 = "功率",
            value1 = data.power,
            label2 = "电池温度",
            value2 = data.batteryTemperature,
        )
    }
}

@Composable
private fun BatteryCard(data: CollectorSnapshot) {
    Card(modifier = Modifier.fillMaxWidth()) {
        SectionTitle("电池")
        MetricRow("电压", data.voltage, "电流", data.current)
        MetricRow("剩余容量", data.remainingCapacity, "满充容量", data.fullCapacity)
        MetricRow("健康度", data.health, "循环次数", data.cycles)
    }
}

@Composable
private fun ChargingCard(data: CollectorSnapshot) {
    Card(modifier = Modifier.fillMaxWidth()) {
        SectionTitle("充电")
        MetricRow("USB", if (data.usbOnline) "在线" else "离线", "快充类型", data.fastCharge)
        MetricRow("USB 电压", data.usbVoltage, "USB 电流", data.usbCurrent)
        MetricRow("功率来源", data.powerSource, "预计", data.eta)
    }
}

@Composable
private fun ThermalCard(data: CollectorSnapshot) {
    Card(modifier = Modifier.fillMaxWidth()) {
        SectionTitle("温度")
        MetricRow("电池", data.thermals.battery, "USB", data.thermals.usb)
        MetricRow("CPU", data.thermals.cpu, "GPU", data.thermals.gpu)
        MetricRow("VOOC", data.thermals.vooc, "机身", data.thermals.shell)
    }
}

@Composable
private fun StatisticsCard(data: CollectorSnapshot) {
    Card(modifier = Modifier.fillMaxWidth()) {
        SectionTitle("统计与看门狗")
        MetricRow("样本数", data.samples, "平均功率", data.averagePower)
        MetricRow("功率范围", data.powerRange, "离线样本", data.offlineSamples)
        MetricRow("容量停滞", data.capacityStalled, "剩余计时", data.remainingSeconds)
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text = text, color = Accent, fontSize = 18.sp)
}

@Composable
private fun MetricRow(
    label1: String,
    value1: String,
    label2: String,
    value2: String,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 12.dp),
    ) {
        MetricCell(label1, value1, Modifier.weight(1f))
        MetricCell(label2, value2, Modifier.weight(1f))
    }
}

@Composable
private fun MetricCell(label: String, value: String, modifier: Modifier) {
    Column(modifier = modifier.padding(end = 8.dp)) {
        Text(text = label, color = Color.Gray, fontSize = 12.sp)
        Text(text = value, modifier = Modifier.padding(top = 3.dp), fontSize = 15.sp)
    }
}

private data class CollectorSnapshot(
    val timestamp: String,
    val marketName: String,
    val buildId: String,
    val level: Double?,
    val statusText: String,
    val usbOnline: Boolean,
    val voltageMv: Double?,
    val currentMa: Double?,
    val powerW: Double?,
    val batteryTemperatureC: Double?,
    val remainingMah: Double?,
    val fullChargeMah: Double?,
    val healthPct: Double?,
    val cycles: Double?,
    val fastChargeType: String,
    val usbVoltageMv: Double?,
    val usbCurrentMa: Double?,
    val powerSource: String,
    val eta: String,
    val thermals: ThermalSnapshot,
    val samplesCount: Double?,
    val averagePowerW: Double?,
    val powerMinW: Double?,
    val powerMaxW: Double?,
    val offlineSamples: Double?,
    val capacityStalledFlag: Boolean,
    val remainingSecondsValue: Double?,
) {
    val deviceSubtitle: String get() = listOf(marketName, buildId).filter { it.isNotBlank() }.joinToString(" · ")
    val levelPercent: String get() = formatNumber(level, 0, "%")
    val updatedAt: String get() = timestamp.ifBlank { "未更新时间" }
    val status: String get() = statusText.ifBlank { "未知" }
    val power: String get() = formatNumber(powerW, 2, " W")
    val batteryTemperature: String get() = formatNumber(batteryTemperatureC, 1, " °C")
    val voltage: String get() = formatNumber(voltageMv, 0, " mV")
    val current: String get() = formatNumber(currentMa, 0, " mA")
    val remainingCapacity: String get() = formatNumber(remainingMah, 0, " mAh")
    val fullCapacity: String get() = formatNumber(fullChargeMah, 0, " mAh")
    val health: String get() = formatNumber(healthPct, 1, "%")
    val fastCharge: String get() = fastChargeType.ifBlank { "未知" }
    val usbVoltage: String get() = formatNumber(usbVoltageMv, 0, " mV")
    val usbCurrent: String get() = formatNumber(usbCurrentMa, 0, " mA")
    val averagePower: String get() = formatNumber(averagePowerW, 2, " W")
    val powerRange: String get() = "${formatNumber(powerMinW, 2)} - ${formatNumber(powerMaxW, 2)} W"
    val samples: String get() = formatNumber(samplesCount, 0)
    val offlineSamples: String get() = formatNumber(this.offlineSamples, 0)
    val capacityStalled: String get() = if (capacityStalledFlag) "是" else "否"
    val remainingSeconds: String get() = formatNumber(remainingSecondsValue, 0, " s")
}

private data class ThermalSnapshot(
    val battery: String,
    val usb: String,
    val vooc: String,
    val cpu: String,
    val gpu: String,
    val shell: String,
)

private fun parseSnapshot(raw: String): CollectorSnapshot {
    val root = JSONObject(raw)
    val device = root.optJSONObject("device") ?: JSONObject()
    val battery = root.optJSONObject("battery") ?: JSONObject()
    val charging = root.optJSONObject("charging") ?: JSONObject()
    val thermals = root.optJSONObject("thermals") ?: JSONObject()
    val statistics = root.optJSONObject("statistics") ?: JSONObject()
    val watchdog = root.optJSONObject("watchdog") ?: JSONObject()

    return CollectorSnapshot(
        timestamp = deviceString(root, "timestamp"),
        marketName = deviceString(device, "market_name"),
        buildId = deviceString(device, "build_id"),
        level = number(battery, "level"),
        statusText = deviceString(battery, "status"),
        usbOnline = charging.optBoolean("usb_online", false),
        voltageMv = number(battery, "voltage_mv"),
        currentMa = number(battery, "current_ma"),
        powerW = number(charging, "power_w"),
        batteryTemperatureC = number(battery, "temperature_c"),
        remainingMah = number(battery, "current_mah"),
        fullChargeMah = number(battery, "full_charge_mah"),
        healthPct = number(battery, "health_pct"),
        cycles = number(battery, "cycle_count"),
        fastChargeType = deviceString(charging, "fast_charge_type"),
        usbVoltageMv = number(charging, "usb_voltage_mv"),
        usbCurrentMa = number(charging, "usb_current_ma"),
        powerSource = deviceString(charging, "power_source"),
        eta = deviceString(charging, "eta"),
        thermals = ThermalSnapshot(
            battery = temperature(thermals, "battery_c"),
            usb = temperature(thermals, "usb_c"),
            vooc = temperature(thermals, "vooc_c"),
            cpu = temperature(thermals, "cpu_c"),
            gpu = temperature(thermals, "gpu_c"),
            shell = temperature(thermals, "shell_c"),
        ),
        samplesCount = number(statistics, "samples"),
        averagePowerW = number(statistics, "power_avg_w"),
        powerMinW = number(statistics, "power_min_w"),
        powerMaxW = number(statistics, "power_max_w"),
        offlineSamples = number(watchdog, "offline_samples"),
        capacityStalledFlag = watchdog.optBoolean("capacity_stalled", false),
        remainingSecondsValue = number(watchdog, "remaining_seconds"),
    )
}

private fun deviceString(json: JSONObject, key: String): String =
    if (json.has(key) && !json.isNull(key)) json.optString(key).trim() else ""

private fun number(json: JSONObject, key: String): Double? {
    if (!json.has(key) || json.isNull(key)) return null
    return json.optDouble(key, Double.NaN).takeUnless { it.isNaN() }
}

private fun temperature(json: JSONObject, key: String): String = formatNumber(number(json, key), 1, " °C")

private fun formatNumber(value: Double?, digits: Int, suffix: String = ""): String =
    value?.let { String.format(java.util.Locale.US, "%.${digits}f%s", it, suffix) } ?: "--"
