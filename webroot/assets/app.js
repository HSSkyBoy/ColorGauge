(() => {
  const history = [];
  const maxHistory = 150;
  const maxStateAgeMs = 25000;
  let mockTimer = null;
  let pollTimer = null;
  let requestInFlight = false;
  let activePayload = null;

  const el = (id) => document.getElementById(id);
  const number = (value, fallback = null) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const fmt = (value, digits = 1, suffix = "") => value === null ? "N/A" : `${value.toFixed(digits)}${suffix}`;

  const mockPayload = () => {
    const now = new Date();
    const phase = now.getTime() / 28000;
    const power = 61 + Math.sin(phase) * 7 + Math.sin(phase * 3) * 2;
    const current = power / 8.78;
    const temperature = 35.2 + Math.sin(phase / 1.8) * 1.1;
    return {
      timestamp: now.toISOString(),
      source: { mode: "mock", csv_path: "/data/vendor/battery/battery-log-demo.csv" },
      battery: {
        level: 72,
        voltage_mv: 4388,
        current_ma: Math.round(current * 1000),
        temperature_c: temperature,
        health_pct: 98,
        soh_pct: 98,
        status: "正在充电",
      },
      charging: {
        usb_online: true,
        usb_voltage_mv: 9990,
        usb_current_ma: Math.round(power * 1000 / 9.99),
        power_w: power,
        power_source: "usb_voltage_current",
        fast_charge_type: "SUPERVOOC",
      },
      thermals: {
        battery_c: temperature,
        usb_c: 33.4 + Math.sin(phase) * 0.8,
        vooc_c: 37.8 + Math.sin(phase) * 0.7,
        cpu_c: 43.6 + Math.sin(phase / 2) * 2,
        gpu_c: 41.3 + Math.sin(phase / 2.4) * 1.7,
        shell_c: 34.7 + Math.sin(phase / 1.5) * 0.9,
      },
      raw: {
        battery_rm: "3512",
        battery_fcc: "4500",
        design_capacity: "4880",
        battery_soh: "98",
        chip_soc: "72",
        gauge_soc: "72",
        fast_chg_type: "SUPERVOOC",
        svooc_flag: "1",
        usb_online: "1",
        csv_sample: "mock fallback",
      },
    };
  };

  function setConnection(label, live) {
    const box = el("connection");
    box.classList.toggle("live", live);
    el("connection-text").textContent = label;
  }

  function renderThermals(thermals) {
    const labels = {
      battery_c: "电池",
      usb_c: "USB",
      vooc_c: "VOOC",
      cpu_c: "CPU",
      gpu_c: "GPU",
      shell_c: "Shell",
    };
    el("thermal-grid").innerHTML = Object.entries(labels).map(([key, label]) => {
      const temp = number(thermals[key]);
      return `<div class="thermal-card ${temp !== null && temp >= 45 ? "hot" : ""}"><span>${label}</span><strong>${fmt(temp)} C</strong></div>`;
    }).join("");
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[char]);
  }

  function renderRaw(raw) {
    const query = el("raw-search").value.trim().toLowerCase();
    const rows = Object.entries(raw || {})
      .filter(([key, value]) => `${key} ${value}`.toLowerCase().includes(query))
      .sort(([a], [b]) => a.localeCompare(b));
    el("raw-list").innerHTML = rows.length
      ? rows.map(([key, value]) => `<div class="raw-row"><span title="${escapeHtml(key)}">${escapeHtml(key)}</span><strong>${escapeHtml(String(value ?? "N/A"))}</strong></div>`).join("")
      : '<div class="raw-row"><span>没有匹配的数据</span></div>';
  }

  function drawChart() {
    const canvas = el("pulse-chart");
    const rect = canvas.getBoundingClientRect();
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.round(rect.width * ratio));
    canvas.height = Math.max(1, Math.round(rect.height * ratio));
    const ctx = canvas.getContext("2d");
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.scale(ratio, ratio);

    const width = rect.width;
    const height = rect.height;
    ctx.clearRect(0, 0, width, height);
    ctx.strokeStyle = "#42392c";
    ctx.lineWidth = 1;
    for (let line = 1; line < 4; line += 1) {
      const y = height * line / 4;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }

    plot(ctx, history.map((point) => point.power), "#f8b84e", width, height, 0.12);
    plot(ctx, history.map((point) => point.temperature), "#e86835", width, height, 0.18);
  }

  function plot(ctx, values, color, width, height, padding) {
    const available = values.filter((item) => item !== null);
    if (available.length < 2) return;
    let min = Math.min(...available);
    let max = Math.max(...available);
    const span = Math.max(max - min, 1);
    min -= span * padding;
    max += span * padding;
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.lineJoin = "round";
    ctx.beginPath();
    values.forEach((item, index) => {
      const point = item === null ? min : item;
      const x = values.length === 1 ? 0 : index / (values.length - 1) * width;
      const y = height - ((point - min) / (max - min)) * height;
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }

  function render(payload) {
    activePayload = payload;
    const battery = payload.battery || {};
    const charging = payload.charging || {};
    const thermals = payload.thermals || {};
    const power = number(charging.power_w, number(payload.power_w));
    const current = number(battery.current_ma, number(payload.current_ma));
    const voltage = number(battery.voltage_mv, number(payload.voltage_mv));
    const temperature = number(thermals.battery_c, number(battery.temperature_c, number(payload.battery_temp_c)));

    el("charge-status").textContent = battery.status || (charging.usb_online ? "正在充电" : "未充电");
    el("battery-level").textContent = fmt(number(battery.level), 0, "%");
    el("fast-charge").textContent = charging.fast_charge_type || charging.charge_type || "普通充电";
    el("updated-at").textContent = new Date(payload.timestamp || Date.now()).toLocaleTimeString();
    el("power").textContent = fmt(power);
    el("current").textContent = fmt(current === null ? null : current / 1000);
    el("voltage").textContent = fmt(voltage === null ? null : voltage / 1000, 3);
    el("battery-temp").textContent = fmt(temperature);
    el("power-note").textContent = charging.power_source ? `来源: ${charging.power_source}` : "正在等待采集器";
    el("usb-voltage").textContent = `USB ${fmt(number(charging.usb_voltage_mv) === null ? null : number(charging.usb_voltage_mv) / 1000, 2)} V`;
    el("thermal-state").textContent = temperature !== null && temperature >= 42 ? "温度偏高，请留意充电" : "温度处于正常范围";

    const raw = {
      ...payload.raw,
      "battery.level": battery.level,
      "battery.health_pct": battery.health_pct,
      "battery.soh_pct": battery.soh_pct,
      "charging.usb_online": charging.usb_online,
      "charging.eta": charging.eta,
      "source.csv_path": payload.source?.csv_path,
      "source.mode": payload.source?.mode,
      "watchdog.remaining_seconds": payload.watchdog?.remaining_seconds,
      "watchdog.capacity_stalled": payload.watchdog?.capacity_stalled,
    };

    renderRaw(raw);
    renderThermals(thermals);
    history.push({ power, temperature });
    if (history.length > maxHistory) history.shift();
    drawChart();
  }

  function startMock(reason = "采集器离线") {
    setConnection(`模拟数据 - ${reason}`, false);
    if (mockTimer) return;
    render(mockPayload());
    mockTimer = setInterval(() => render(mockPayload()), 2000);
  }

  function stopMock() {
    clearInterval(mockTimer);
    mockTimer = null;
  }

  function kernelSuExec(command) {
    const bridgeOwner = window.ksu?.exec ? window.ksu : window.kernelsu?.exec ? window.kernelsu : null;
    if (!bridgeOwner) return Promise.reject(new Error("KernelSU Shell bridge 不可用"));
    return Promise.resolve(bridgeOwner.exec(command)).then((result) => {
      if (typeof result === "string") return result;
      if (result?.errno && result.errno !== 0) throw new Error(result.stderr || "状态文件不可读");
      return result?.stdout ?? result?.output ?? "";
    });
  }

  async function pollState() {
    if (requestInFlight) return;
    requestInFlight = true;
    try {
      const raw = await kernelSuExec("cat /data/adb/modules/o_pulse/run/state.json");
      const payload = JSON.parse(raw.trim());
      const timestamp = Date.parse(payload.timestamp);
      if (!Number.isFinite(timestamp) || Date.now() - timestamp > maxStateAgeMs) {
        throw new Error("采集器状态已过期");
      }
      stopMock();
      setConnection("实时采集器已连接", true);
      render(payload);
    } catch (error) {
      startMock(error?.message || "采集器离线");
    } finally {
      requestInFlight = false;
    }
  }

  function startPolling() {
    pollState();
    pollTimer = setInterval(pollState, 4000);
  }

  el("raw-search").addEventListener("input", () => renderRaw({
    ...activePayload?.raw,
    "battery.level": activePayload?.battery?.level,
    "battery.health_pct": activePayload?.battery?.health_pct,
    "battery.soh_pct": activePayload?.battery?.soh_pct,
    "charging.usb_online": activePayload?.charging?.usb_online,
    "charging.eta": activePayload?.charging?.eta,
    "source.csv_path": activePayload?.source?.csv_path,
    "source.mode": activePayload?.source?.mode,
    "watchdog.remaining_seconds": activePayload?.watchdog?.remaining_seconds,
    "watchdog.capacity_stalled": activePayload?.watchdog?.capacity_stalled,
  }));

  window.addEventListener("resize", drawChart);
  startMock();
  startPolling();
})();
