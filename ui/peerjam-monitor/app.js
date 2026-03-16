const els = {
  startBtn: document.getElementById("startBtn"),
  stopBtn: document.getElementById("stopBtn"),
  monitorBtn: document.getElementById("monitorBtn"),
  eventsUrl: document.getElementById("eventsUrl"),
  connectEventsBtn: document.getElementById("connectEventsBtn"),
  disconnectEventsBtn: document.getElementById("disconnectEventsBtn"),
  inputDeviceSelect: document.getElementById("inputDeviceSelect"),
  outputDeviceSelect: document.getElementById("outputDeviceSelect"),
  refreshDevicesBtn: document.getElementById("refreshDevicesBtn"),
  applyDevicesBtn: document.getElementById("applyDevicesBtn"),
  captureState: document.getElementById("captureState"),
  telemetryStatus: document.getElementById("telemetryStatus"),
  waveCanvas: document.getElementById("waveCanvas"),
  recvWaveCanvas: document.getElementById("recvWaveCanvas"),
  telemetryCanvas: document.getElementById("telemetryCanvas"),
  captureGain: document.getElementById("captureGain"),
  captureGainValue: document.getElementById("captureGainValue"),
  monitorVolume: document.getElementById("monitorVolume"),
  monitorVolumeValue: document.getElementById("monitorVolumeValue"),
  latencyMs: document.getElementById("latencyMs"),
  jitterMs: document.getElementById("jitterMs"),
  bandwidthKbps: document.getElementById("bandwidthKbps"),
  latencyCard: document.getElementById("latencyCard"),
  jitterCard: document.getElementById("jitterCard"),
  bandwidthCard: document.getElementById("bandwidthCard"),
  latencyInput: document.getElementById("latencyInput"),
  jitterInput: document.getElementById("jitterInput"),
  bandwidthInput: document.getElementById("bandwidthInput"),
  applyTelemetryBtn: document.getElementById("applyTelemetryBtn"),
  reportJson: document.getElementById("reportJson"),
  parseReportBtn: document.getElementById("parseReportBtn"),
};

let audioContext = null;
let mediaStream = null;
let mediaSource = null;
let analyser = null;
let captureGainNode = null;
let monitorGainNode = null;
let monitorEnabled = false;
let rafId = 0;
let events = null;
let reconnectTimerId = 0;
let reconnectAttempts = 0;
let reconnectUrl = "";
let manualDisconnect = false;

const telemetryHistory = {
  latency: [],
  jitter: [],
};

function setCaptureState(active) {
  els.captureState.textContent = active ? "Capturing" : "Stopped";
  els.captureState.className = active ? "pill pill-on" : "pill pill-off";
  els.startBtn.disabled = active;
  els.stopBtn.disabled = !active;
  els.monitorBtn.disabled = !active;
  if (!active) {
    els.monitorBtn.textContent = "Monitor Off";
  }
}

function startCaptureDrawLoop() {
  const ctx = els.waveCanvas.getContext("2d");
  const width = els.waveCanvas.width;
  const height = els.waveCanvas.height;
  const data = new Uint8Array(analyser.fftSize);

  const draw = () => {
    analyser.getByteTimeDomainData(data);
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#f9fbfd";
    ctx.fillRect(0, 0, width, height);
    ctx.lineWidth = 2;
    ctx.strokeStyle = "#0f766e";
    ctx.beginPath();

    const step = width / data.length;
    for (let i = 0; i < data.length; i += 1) {
      const y = (data[i] / 255) * height;
      const x = i * step;
      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();

    ctx.strokeStyle = "#cbd5e1";
    ctx.beginPath();
    ctx.moveTo(0, height / 2);
    ctx.lineTo(width, height / 2);
    ctx.stroke();

    rafId = requestAnimationFrame(draw);
  };
  draw();
}

function stopCaptureDrawLoop() {
  if (rafId) {
    cancelAnimationFrame(rafId);
    rafId = 0;
  }
  const ctx = els.waveCanvas.getContext("2d");
  ctx.clearRect(0, 0, els.waveCanvas.width, els.waveCanvas.height);
}

function drawExternalWave(wave) {
  const ctx = els.waveCanvas.getContext("2d");
  const width = els.waveCanvas.width;
  const height = els.waveCanvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#f9fbfd";
  ctx.fillRect(0, 0, width, height);
  if (!Array.isArray(wave) || wave.length < 2) {
    return;
  }
  ctx.lineWidth = 2;
  ctx.strokeStyle = "#0f766e";
  ctx.beginPath();
  const step = width / (wave.length - 1);
  for (let i = 0; i < wave.length; i += 1) {
    const value = Number(wave[i]) || 0;
    const v = Math.max(-1, Math.min(1, value));
    const x = i * step;
    const y = height / 2 - v * (height * 0.42);
    if (i === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.stroke();
}

function drawReceivedWave(wave) {
  const ctx = els.recvWaveCanvas.getContext("2d");
  const width = els.recvWaveCanvas.width;
  const height = els.recvWaveCanvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#f9fbfd";
  ctx.fillRect(0, 0, width, height);
  if (!Array.isArray(wave) || wave.length < 2) {
    return;
  }
  ctx.lineWidth = 2;
  ctx.strokeStyle = "#1d4ed8";
  ctx.beginPath();
  const step = width / (wave.length - 1);
  for (let i = 0; i < wave.length; i += 1) {
    const value = Number(wave[i]) || 0;
    const v = Math.max(-1, Math.min(1, value));
    const x = i * step;
    const y = height / 2 - v * (height * 0.42);
    if (i === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.stroke();
}

function baseUrlFromEventsUrl() {
  const url = (els.eventsUrl.value || "").trim();
  return url.endsWith("/events") ? url.slice(0, -7) : url;
}

function buildDeviceOption(label, value) {
  const opt = document.createElement("option");
  opt.value = String(value);
  opt.textContent = label;
  return opt;
}

function fillDeviceSelects(devices) {
  els.inputDeviceSelect.innerHTML = "";
  els.outputDeviceSelect.innerHTML = "";
  els.inputDeviceSelect.appendChild(buildDeviceOption("System Default", -1));
  els.outputDeviceSelect.appendChild(buildDeviceOption("System Default", -1));
  for (const d of devices) {
    const idx = Number(d.index);
    const name = String(d.name || `Device ${idx}`);
    const inCh = Number(d.max_input_channels || 0);
    const outCh = Number(d.max_output_channels || 0);
    if (inCh > 0) {
      els.inputDeviceSelect.appendChild(buildDeviceOption(`#${idx} ${name} (in:${inCh})`, idx));
    }
    if (outCh > 0) {
      els.outputDeviceSelect.appendChild(buildDeviceOption(`#${idx} ${name} (out:${outCh})`, idx));
    }
  }
}

async function refreshDevices() {
  const base = baseUrlFromEventsUrl();
  if (!base) {
    return;
  }
  const response = await fetch(`${base}/devices`);
  if (!response.ok) {
    throw new Error(`device request failed: ${response.status}`);
  }
  const devices = await response.json();
  fillDeviceSelects(Array.isArray(devices) ? devices : []);
}

async function applyDevices() {
  const base = baseUrlFromEventsUrl();
  if (!base) {
    return;
  }
  const payload = {
    input_device_index: Number(els.inputDeviceSelect.value),
    output_device_index: Number(els.outputDeviceSelect.value),
  };
  const response = await fetch(`${base}/devices`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    throw new Error(`apply devices failed: ${response.status}`);
  }
  els.telemetryStatus.textContent = "Device selection sent";
  els.telemetryStatus.className = "pill pill-good";
}

async function startCapture() {
  if (audioContext) {
    return;
  }
  mediaStream = await navigator.mediaDevices.getUserMedia({
    audio: {
      echoCancellation: false,
      noiseSuppression: false,
      autoGainControl: false,
    },
    video: false,
  });

  audioContext = new AudioContext();
  mediaSource = audioContext.createMediaStreamSource(mediaStream);
  analyser = audioContext.createAnalyser();
  analyser.fftSize = 2048;

  captureGainNode = audioContext.createGain();
  captureGainNode.gain.value = Number(els.captureGain.value);

  monitorGainNode = audioContext.createGain();
  monitorGainNode.gain.value = 0.0;

  mediaSource.connect(captureGainNode);
  captureGainNode.connect(analyser);
  captureGainNode.connect(monitorGainNode);
  monitorGainNode.connect(audioContext.destination);

  setCaptureState(true);
  startCaptureDrawLoop();
}

function stopCapture() {
  stopCaptureDrawLoop();
  if (mediaStream) {
    for (const track of mediaStream.getTracks()) {
      track.stop();
    }
  }
  if (audioContext) {
    audioContext.close();
  }

  audioContext = null;
  mediaStream = null;
  mediaSource = null;
  analyser = null;
  captureGainNode = null;
  monitorGainNode = null;
  monitorEnabled = false;
  setCaptureState(false);
}

function connectRealtimeEvents() {
  const url = (els.eventsUrl.value || "").trim();
  if (!url) {
    return;
  }
  manualDisconnect = false;
  reconnectUrl = url;
  if (reconnectTimerId) {
    clearTimeout(reconnectTimerId);
    reconnectTimerId = 0;
  }
  if (events) {
    events.close();
    events = null;
  }
  events = new EventSource(url);
  els.connectEventsBtn.disabled = true;
  els.disconnectEventsBtn.disabled = false;
  els.telemetryStatus.textContent = "Connecting realtime...";
  els.telemetryStatus.className = "pill pill-warn";

  events.onopen = () => {
    reconnectAttempts = 0;
    els.telemetryStatus.textContent = "Realtime from C++";
    els.telemetryStatus.className = "pill pill-good";
  };

  events.onmessage = (evt) => {
    try {
      const payload = JSON.parse(evt.data);
      setTelemetry(
        payload?.network?.latency_ms,
        payload?.network?.jitter_ms,
        payload?.network?.bandwidth_kbps,
        "Realtime from C++"
      );
      if (!audioContext) {
        drawExternalWave(payload?.audio?.wave);
      }
      drawReceivedWave(payload?.audio?.recv_wave);
    } catch (err) {
      console.error(err);
    }
  };

  events.onerror = () => {
    if (events) {
      events.close();
      events = null;
    }
    if (!manualDisconnect && reconnectUrl) {
      const delayMs = Math.min(5000, 500 * (2 ** reconnectAttempts));
      reconnectAttempts += 1;
      els.telemetryStatus.textContent = `Realtime disconnected (reconnect in ${Math.round(delayMs / 1000)}s)`;
      els.telemetryStatus.className = "pill pill-warn";
      reconnectTimerId = setTimeout(() => {
        connectRealtimeEvents();
      }, delayMs);
      return;
    }
    els.telemetryStatus.textContent = "Realtime disconnected";
    els.telemetryStatus.className = "pill pill-warn";
  };
  refreshDevices().catch((err) => {
    console.error(err);
  });
}

function disconnectRealtimeEvents() {
  manualDisconnect = true;
  if (reconnectTimerId) {
    clearTimeout(reconnectTimerId);
    reconnectTimerId = 0;
  }
  if (events) {
    events.close();
    events = null;
  }
  els.connectEventsBtn.disabled = false;
  els.disconnectEventsBtn.disabled = true;
}

function styleMetric(card, value, warmLimit, hotLimit) {
  card.classList.remove("metric-warm", "metric-hot");
  if (!Number.isFinite(value)) {
    return;
  }
  if (value >= hotLimit) {
    card.classList.add("metric-hot");
    return;
  }
  if (value >= warmLimit) {
    card.classList.add("metric-warm");
  }
}

function styleBandwidthMetric(card, value, warmLowerBound, hotLowerBound) {
  card.classList.remove("metric-warm", "metric-hot");
  if (!Number.isFinite(value)) {
    return;
  }
  if (value <= hotLowerBound) {
    card.classList.add("metric-hot");
    return;
  }
  if (value <= warmLowerBound) {
    card.classList.add("metric-warm");
  }
}

function pushTelemetryTrend(latencyMs, jitterMs) {
  if (Number.isFinite(latencyMs)) {
    telemetryHistory.latency.push(latencyMs);
  }
  if (Number.isFinite(jitterMs)) {
    telemetryHistory.jitter.push(jitterMs);
  }
  const maxLen = 90;
  if (telemetryHistory.latency.length > maxLen) {
    telemetryHistory.latency.shift();
  }
  if (telemetryHistory.jitter.length > maxLen) {
    telemetryHistory.jitter.shift();
  }
}

function drawTelemetryTrend() {
  const ctx = els.telemetryCanvas.getContext("2d");
  const width = els.telemetryCanvas.width;
  const height = els.telemetryCanvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#f9fbfd";
  ctx.fillRect(0, 0, width, height);

  const allValues = telemetryHistory.latency.concat(telemetryHistory.jitter);
  if (allValues.length === 0) {
    return;
  }

  const maxVal = Math.max(5, ...allValues);
  const minVal = 0;
  const toY = (v) => height - ((v - minVal) / (maxVal - minVal)) * (height - 12) - 6;

  const drawLine = (arr, color) => {
    if (arr.length < 2) {
      return;
    }
    const step = width / (arr.length - 1);
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < arr.length; i += 1) {
      const x = i * step;
      const y = toY(arr[i]);
      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();
  };

  drawLine(telemetryHistory.latency, "#1d4ed8");
  drawLine(telemetryHistory.jitter, "#d97706");
}

function setTelemetry(latencyMs, jitterMs, bandwidthKbps, sourceLabel = "Telemetry updated") {
  const rawLatency = Number(latencyMs);
  const latency = Number.isFinite(rawLatency) && rawLatency > 0 ? rawLatency : Number.NaN;
  const jitter = Number(jitterMs);
  const bandwidth = Number(bandwidthKbps);

  els.latencyMs.textContent = Number.isFinite(latency) ? latency.toFixed(2) : "--";
  els.jitterMs.textContent = Number.isFinite(jitter) ? jitter.toFixed(2) : "--";
  els.bandwidthKbps.textContent = Number.isFinite(bandwidth) ? Math.round(bandwidth).toString() : "--";

  styleMetric(els.latencyCard, latency, 12, 30);
  styleMetric(els.jitterCard, jitter, 2.5, 5);
  styleBandwidthMetric(els.bandwidthCard, bandwidth, 64, 32);

  pushTelemetryTrend(latency, jitter);
  drawTelemetryTrend();

  if (Number.isFinite(latency) && latency > 30) {
    els.telemetryStatus.textContent = `${sourceLabel} (Latency budget breached > 30ms)`;
    els.telemetryStatus.className = "pill pill-warn";
  } else {
    els.telemetryStatus.textContent = sourceLabel;
    els.telemetryStatus.className = "pill pill-good";
  }
}

function estimateBandwidthFromCodec(codecName) {
  const codec = String(codecName || "").toLowerCase();
  if (codec.includes("opus")) {
    return 96;
  }
  return 3072;
}

function parsePeerProbeReport(rawJson) {
  const report = JSON.parse(rawJson);
  const latency = report?.network?.selected_score?.latency_ms;
  const jitter = report?.network?.selected_score?.jitter_ms;
  const codec = report?.audio?.codec || report?.audio?.codec_name || "pcm";
  const bandwidth = estimateBandwidthFromCodec(codec);
  setTelemetry(latency, jitter, bandwidth, `From report (codec ${codec})`);
}

els.startBtn.addEventListener("click", async () => {
  try {
    await startCapture();
  } catch (err) {
    console.error(err);
    alert("Failed to access microphone. Check browser permission and input device.");
  }
});

els.stopBtn.addEventListener("click", () => {
  stopCapture();
});

els.monitorBtn.addEventListener("click", () => {
  if (!monitorGainNode) {
    return;
  }
  monitorEnabled = !monitorEnabled;
  const volume = Number(els.monitorVolume.value);
  monitorGainNode.gain.value = monitorEnabled ? volume : 0.0;
  els.monitorBtn.textContent = monitorEnabled ? "Monitor On" : "Monitor Off";
});

els.connectEventsBtn.addEventListener("click", () => {
  connectRealtimeEvents();
});

els.disconnectEventsBtn.addEventListener("click", () => {
  disconnectRealtimeEvents();
});

els.refreshDevicesBtn.addEventListener("click", async () => {
  try {
    await refreshDevices();
  } catch (err) {
    console.error(err);
    alert("Failed to fetch devices from C++ endpoint.");
  }
});

els.applyDevicesBtn.addEventListener("click", async () => {
  try {
    await applyDevices();
  } catch (err) {
    console.error(err);
    alert("Failed to apply device selection.");
  }
});

els.captureGain.addEventListener("input", () => {
  const value = Number(els.captureGain.value);
  els.captureGainValue.textContent = `${value.toFixed(2)}x`;
  if (captureGainNode) {
    captureGainNode.gain.value = value;
  }
});

els.monitorVolume.addEventListener("input", () => {
  const value = Number(els.monitorVolume.value);
  els.monitorVolumeValue.textContent = `${Math.round(value * 100)}%`;
  if (monitorGainNode && monitorEnabled) {
    monitorGainNode.gain.value = value;
  }
});

els.applyTelemetryBtn.addEventListener("click", () => {
  setTelemetry(els.latencyInput.value, els.jitterInput.value, els.bandwidthInput.value, "Manual telemetry");
});

els.parseReportBtn.addEventListener("click", () => {
  try {
    parsePeerProbeReport(els.reportJson.value);
  } catch (err) {
    console.error(err);
    alert("Invalid JSON report. Paste full output JSON from pj-peer-probe.");
  }
});

setCaptureState(false);
drawTelemetryTrend();
drawReceivedWave([]);
