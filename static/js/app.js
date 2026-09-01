const GAUGE_MIN = 900;
const GAUGE_MAX = 1100;
const CENTER = { x: 160, y: 170 };
const RADIUS = 125;

function angleForValue(value) {
  const clamped = Math.max(GAUGE_MIN, Math.min(GAUGE_MAX, value));
  // 180deg (izquierda) en GAUGE_MIN -> 0deg (derecha) en GAUGE_MAX
  return 180 * (GAUGE_MAX - clamped) / (GAUGE_MAX - GAUGE_MIN);
}

function drawTicks() {
  const group = document.getElementById("ticks");
  const values = [900, 950, 1000, 1050, 1100];
  values.forEach((v) => {
    const angleDeg = angleForValue(v);
    const angleRad = (angleDeg * Math.PI) / 180;
    const rInner = RADIUS - 6;
    const rOuter = RADIUS + 6;
    const x1 = CENTER.x + rInner * Math.cos(angleRad);
    const y1 = CENTER.y - rInner * Math.sin(angleRad);
    const x2 = CENTER.x + rOuter * Math.cos(angleRad);
    const y2 = CENTER.y - rOuter * Math.sin(angleRad);
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", x1);
    line.setAttribute("y1", y1);
    line.setAttribute("x2", x2);
    line.setAttribute("y2", y2);
    line.setAttribute("class", "tick");
    group.appendChild(line);
  });
}

function setNeedle(value) {
  const needle = document.getElementById("needle");
  if (value === null || value === undefined) return;
  const angleDeg = angleForValue(value);
  const rotation = 90 - angleDeg;
  needle.style.transform = `rotate(${rotation}deg)`;
}

function fmt(value, decimals = 1) {
  if (value === null || value === undefined) return "—";
  return Number(value).toFixed(decimals);
}

function setStatus(level) {
  const badge = document.getElementById("statusBadge");
  const text = document.getElementById("statusText");
  const labels = {
    "estable": "Clima estable",
    "precaucion": "Precaución",
    "alerta": "Alerta climática",
    "sin-datos": "Sin datos",
  };
  const key = level || "sin-datos";
  badge.dataset.level = key;
  text.textContent = labels[key] || "Sin datos";

  // Update LED indicators
  const ledRojo = document.getElementById("ledRojo");
  const ledAmarillo = document.getElementById("ledAmarillo");
  const ledVerde = document.getElementById("ledVerde");

  // Remove all active states
  ledRojo.classList.remove("active");
  ledAmarillo.classList.remove("active");
  ledVerde.classList.remove("active");

  // Activate appropriate LED based on alert level
  if (key === "alerta") {
    ledRojo.classList.add("active");
  } else if (key === "precaucion") {
    ledAmarillo.classList.add("active");
  } else if (key === "estable") {
    ledVerde.classList.add("active");
  }
}

async function refreshLatest() {
  try {
    const res = await fetch("/api/latest");
    const data = await res.json();
    if (!data) {
      setStatus("sin-datos");
      return;
    }
    document.getElementById("pressureValue").textContent = fmt(data.pressure_hpa);
    document.getElementById("temperatureValue").textContent = fmt(data.temperature_c);
    document.getElementById("humidityValue").textContent = fmt(data.humidity_pct, 0);

    if (data.pressure_hpa !== null && data.pressure_hpa !== undefined) {
      setNeedle(data.pressure_hpa);
    }
    setStatus(data.alert_level);

    if (data.timestamp) {
      const d = new Date(data.timestamp + "Z");
      document.getElementById("lastUpdate").textContent = d.toLocaleTimeString("es-CR", { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    }
  } catch (err) {
    console.error("Error al obtener la última lectura:", err);
  }
}

let chart;
let chartRows = [];

function buildChart() {
  const canvas = document.getElementById("historyChart");
  chart = { canvas, context: canvas.getContext("2d") };
  window.addEventListener("resize", renderChart);
  renderChart();
}

function renderChart() {
  if (!chart) return;

  const { canvas, context } = chart;
  const bounds = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(bounds.width * ratio));
  canvas.height = Math.max(1, Math.round(bounds.height * ratio));
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, bounds.width, bounds.height);

  if (!chartRows.length) return;

  const margin = { top: 42, right: 58, bottom: 34, left: 72 };
  const width = bounds.width - margin.left - margin.right;
  const height = bounds.height - margin.top - margin.bottom;
  if (width <= 0 || height <= 0) return;

  const pressureValues = chartRows.map((row) => Number(row.pressure_hpa)).filter(Number.isFinite);
  const pressurePadding = Math.max((Math.max(...pressureValues) - Math.min(...pressureValues)) * 0.15, 1);
  const pressureMin = Math.floor((Math.min(...pressureValues) - pressurePadding) * 10) / 10;
  const pressureMax = Math.ceil((Math.max(...pressureValues) + pressurePadding) * 10) / 10;
  const series = [
    { key: "pressure_hpa", label: "Presión (hPa)", color: "#4FD6C4", min: pressureMin, max: pressureMax },
    { key: "temperature_c", label: "Temperatura (°C)", color: "#F2B84B", min: 0, max: 50 },
    { key: "humidity_pct", label: "Humedad (%)", color: "#7FB3E8", min: 0, max: 100, dashed: true },
  ];

  context.strokeStyle = "#1A2A38";
  context.fillStyle = "#7E93A7";
  context.lineWidth = 1;
  context.font = "10px 'IBM Plex Mono', monospace";
  for (let step = 0; step <= 4; step++) {
    const y = margin.top + (height * step) / 4;
    context.beginPath();
    context.moveTo(margin.left, y);
    context.lineTo(margin.left + width, y);
    context.stroke();
    const pressureValue = pressureMax - ((pressureMax - pressureMin) * step) / 4;
    const humidityValue = 100 - step * 25;
    context.fillText(`${pressureValue.toFixed(1)}`, 8, y + 3);
    context.fillText(`${humidityValue}%`, bounds.width - 42, y + 3);
  }

  series.forEach((item, seriesIndex) => {
    const values = chartRows.map((row) => Number(row[item.key])).filter(Number.isFinite);
    if (!values.length) return;
    const minimum = item.min;
    const maximum = item.max;
    const span = maximum - minimum;
    const points = chartRows
      .map((row, index) => {
        const value = Number(row[item.key]);
        if (!Number.isFinite(value)) return null;
        const x = margin.left + (chartRows.length === 1 ? width / 2 : (width * index) / (chartRows.length - 1));
        const y = margin.top + height - ((value - minimum) / span) * height;
        return { x, y };
      })
      .filter(Boolean);

    context.strokeStyle = item.color;
    context.lineWidth = 2;
    context.setLineDash(item.dashed ? [5, 4] : []);
    context.beginPath();
    points.forEach((point, index) => {
      if (index === 0) context.moveTo(point.x, point.y);
      else context.lineTo(point.x, point.y);
    });
    context.stroke();
    context.setLineDash([]);

    const legendX = margin.left + seriesIndex * 145;
    context.fillStyle = item.color;
    context.fillRect(legendX, 8, 20, 2);
    context.fillText(item.label, legendX + 26, 12);
  });

  context.fillStyle = "#7E93A7";
  context.fillText("hPa", 8, margin.top - 12);
  context.fillText("%", bounds.width - 42, margin.top - 12);
  context.fillText(formatLabel(chartRows[0].timestamp, currentRange()), margin.left, bounds.height - 10);
  if (chartRows.length > 1) {
    const lastLabel = formatLabel(chartRows[chartRows.length - 1].timestamp, currentRange());
    const lastWidth = context.measureText(lastLabel).width;
    context.fillText(lastLabel, bounds.width - margin.right - lastWidth, bounds.height - 10);
  }
}

function formatLabel(isoTimestamp, range) {
  const d = new Date(isoTimestamp + "Z");
  if (range === "1w" || range === "1d") {
    return d.toLocaleString("es-CR", { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" });
  }
  return d.toLocaleTimeString("es-CR", { hour: "2-digit", minute: "2-digit" });
}

async function loadHistory(range) {
  try {
    const res = await fetch(`/api/history?range=${range}`);
    const rows = await res.json();
    const empty = document.getElementById("chartEmpty");

    if (!rows.length) {
      chartRows = [];
      renderChart();
      empty.style.display = "block";
      return;
    }
    empty.style.display = "none";

    chartRows = rows;
    renderChart();
  } catch (err) {
    console.error("Error al obtener el historial:", err);
  }
}

function setupRangeTabs() {
  const tabs = document.querySelectorAll(".range-tab");
  tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      tabs.forEach((t) => t.classList.remove("is-active"));
      tab.classList.add("is-active");
      loadHistory(tab.dataset.range);
    });
  });
}

function currentRange() {
  const active = document.querySelector(".range-tab.is-active");
  return active ? active.dataset.range : "5m";
}

document.addEventListener("DOMContentLoaded", () => {
  drawTicks();
  refreshLatest();
  buildChart();
  setupRangeTabs();
  loadHistory(currentRange());

  setInterval(refreshLatest, 60000); // Refresh every 1 minute
  setInterval(() => loadHistory(currentRange()), 60000); // Update history every 1 minute
});
