(() => {
  const history = [];
  const maxHistory = 150;
  const maxStateAgeMs = 25000;
  const moduleRoot = "/data/adb/opulse";
  const statePath = `${moduleRoot}/state.json`;
  const logPath = `${moduleRoot}/run/collector.log`;
  let activePayload = null;
  let requestInFlight = false;

  const el = (id) => document.getElementById(id);
  const number = (value, fallback = null) => {
    if (value === null || value === undefined || value === "" || value === "N/A" || value === "--") return fallback;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
  };
  const fmt = (value, digits = 1, suffix = "") => value === null ? "N/A" : `${value.toFixed(digits)}${suffix}`;
  const shellQuote = (value) => `'${String(value).replace(/'/g, "'\\''")}'`;

  function setConnection(label, live) {
    el("connection").classList.toggle("live", live);
    el("connection-text").textContent = label;
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (char) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    })[char]);
  }

  function renderThermals(thermals = {}) {
    const labels = {
      battery_c: "Battery",
      usb_c: "USB",
      vooc_c: "VOOC",
      cpu_c: "CPU",
      gpu_c: "GPU",
      shell_c: "Shell",
    };
    el("thermal-grid").innerHTML = Object.entries(labels).map(([key, label]) => {
      const temperature = number(thermals[key]);
      const hot = temperature !== null && temperature >= 45 ? " hot" : "";
      return `<div class="thermal-card${hot}"><span>${label}</span><strong>${fmt(temperature)} C</strong></div>`;
    }).join("");
  }

  function renderRaw(raw = {}) {
    const query = el("raw-search").value.trim().toLowerCase();
    const rows = Object.entries(raw)
      .filter(([key, value]) => `${key} ${value}`.toLowerCase().includes(query))
      .sort(([left], [right]) => left.localeCompare(right));
    el("raw-list").innerHTML = rows.length
      ? rows.map(([key, value]) => `<div class="raw-row"><span title="${escapeHtml(key)}">${escapeHtml(key)}</span><strong>${escapeHtml(String(value ?? "N/A"))}</strong></div>`).join("")
      : '<div class="raw-row"><span>No matching data</span></div>';
  }

  function plot(ctx, values, color, width, height, padding) {
    const available = values.filter((value) => value !== null);
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
    values.forEach((value, index) => {
      const point = value === null ? min : value;
      const x = values.length === 1 ? 0 : index / (values.length - 1) * width;
      const y = height - ((point - min) / (max - min)) * height;
      if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
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
    ctx.clearRect(0, 0, rect.width, rect.height);
    ctx.strokeStyle = "#42392c";
    ctx.lineWidth = 1;
    for (let line = 1; line < 4; line += 1) {
      const y = rect.height * line / 4;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(rect.width, y);
      ctx.stroke();
    }
    plot(ctx, history.map((point) => point.power), "#f8b84e", rect.width, rect.height, 0.12);
    plot(ctx, history.map((point) => point.temperature), "#e86835", rect.width, rect.height, 0.18);
  }

  function payloadRaw(payload) {
    const battery = payload.battery || {};
    const charging = payload.charging || {};
    const source = payload.source || {};
    return {
      ...(payload.raw || {}),
      "battery.level": battery.level,
      "battery.health_pct": battery.health_pct,
      "battery.soh_pct": battery.soh_pct,
      "charging.usb_online": charging.usb_online,
      "charging.eta": charging.eta,
      "source.mode": source.mode,
      "source.csv_path": source.csv_path,
      "watchdog.remaining_seconds": payload.watchdog?.remaining_seconds,
      "watchdog.capacity_stalled": payload.watchdog?.capacity_stalled,
    };
  }

  function render(payload) {
    activePayload = payload;
    const battery = payload.battery || {};
    const charging = payload.charging || {};
    const thermals = payload.thermals || {};
    const source = payload.source || {};
    const power = number(charging.power_w, number(payload.power_w));
    const current = number(battery.current_ma, number(payload.current_ma));
    const voltage = number(battery.voltage_mv, number(payload.voltage_mv));
    const temperature = number(thermals.battery_c, number(battery.temperature_c, number(payload.battery_temp_c)));

    el("charge-status").textContent = battery.status || (charging.usb_online ? "Charging" : "Not charging");
    el("battery-level").textContent = fmt(number(battery.level), 0, "%");
    el("fast-charge").textContent = charging.fast_charge_type || charging.charge_type || "N/A";
    el("updated-at").textContent = new Date(payload.timestamp || Date.now()).toLocaleTimeString();
    el("power").textContent = fmt(power);
    el("current").textContent = fmt(current === null ? null : current / 1000);
    el("voltage").textContent = fmt(voltage === null ? null : voltage / 1000, 3);
    el("battery-temp").textContent = fmt(temperature);
    el("power-note").textContent = charging.power_source ? `Source: ${charging.power_source}` : "Waiting for sample";
    const usbVoltage = number(charging.usb_voltage_mv);
    el("usb-voltage").textContent = `USB ${fmt(usbVoltage === null ? null : usbVoltage / 1000, 2)} V`;
    el("thermal-state").textContent = temperature !== null && temperature >= 42 ? "Temperature is elevated" : "Temperature is normal";

    renderRaw(payloadRaw(payload));
    renderThermals(thermals);
    history.push({ power, temperature });
    if (history.length > maxHistory) history.shift();
    drawChart();

    if (source.mode === "live" || source.mode === "live-shell") setConnection("Collector connected", true);
    else setConnection(`Collector status: ${source.mode || "unknown"}`, false);
  }

  function renderUnavailable(reason) {
    el("charge-status").textContent = "Collector offline";
    el("battery-level").textContent = "--%";
    el("fast-charge").textContent = "N/A";
    el("updated-at").textContent = "--:--:--";
    el("power").textContent = "N/A";
    el("current").textContent = "N/A";
    el("voltage").textContent = "N/A";
    el("battery-temp").textContent = "N/A";
    el("power-note").textContent = reason || "state file is not available";
    el("usb-voltage").textContent = "USB N/A V";
    el("thermal-state").textContent = "No live sample";
    renderThermals({});
    renderRaw({
      "service.error": reason || "state file is not available",
      "service.state_file": statePath,
      "service.log_file": logPath,
    });
    drawChart();
  }

  function kernelSuExec(command) {
    if (window.opulse?.exec) {
      const result = JSON.parse(window.opulse.exec(command));
      if (result.errno && result.errno !== 0) throw new Error(result.stderr || "Root command failed");
      return Promise.resolve(result.stdout || "");
    }
    return Promise.reject(new Error("App root bridge unavailable"));
  }

  async function tryReadLog() {
    try {
      const result = await kernelSuExec(`tail -n 20 ${shellQuote(logPath)}`);
      return result.trim().split("\n").filter(Boolean).slice(-1)[0] || "";
    } catch {
      return "";
    }
  }

  async function pollState() {
    if (requestInFlight) return;
    requestInFlight = true;
    try {
      const raw = await kernelSuExec(`cat ${shellQuote(statePath)}`);
      const payload = JSON.parse(raw.trim());
      const timestamp = Date.parse(payload.timestamp);
      const live = payload.source?.mode === "live" || payload.source?.mode === "live-shell";
      const allowedAge = live ? maxStateAgeMs : 10 * 60 * 1000;
      if (!Number.isFinite(timestamp) || Date.now() - timestamp > allowedAge) throw new Error("state file is stale");
      render(payload);
    } catch (error) {
      const detail = await tryReadLog();
      const reason = detail || error?.message || "state file is not available";
      setConnection(`Collector offline: ${reason}`, false);
      renderUnavailable(reason);
    } finally {
      requestInFlight = false;
    }
  }

  el("raw-search").addEventListener("input", () => {
    if (!activePayload) return;
    renderRaw(payloadRaw(activePayload));
  });
  window.addEventListener("resize", drawChart);
  setConnection("Reading collector state", false);
  renderUnavailable("waiting for first sample");
  pollState();
  setInterval(pollState, 4000);
})();
