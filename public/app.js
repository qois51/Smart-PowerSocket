// ================= KONFIGURASI MQTT =================
// Ganti localhost hardcoded dengan hostname dinamis
const mqttBroker = "wss://mqtt.qoisfirosi.dpdns.org";
const mqttTopicPzem = "pzem/data"; // Data Energi
const mqttTopicEnergyRemain = "data/energy_remain"; // Sisa Kuota

// --- BAGIAN PENTING: PEMISAHAN TOPIK RELAY ---
// 1. Topik SET: Dashboard kirim perintah ke sini (Tanpa Retain)
const mqttTopicRelaySet = "relay/set";

// 2. Topik STATE: Dashboard dengar status asli dari sini (Retained di Broker)
// Pastikan ini SAMA dengan yang dipublish oleh ESP32/Node-RED (Retained)
const mqttTopicRelayState = "relay/state";

// 3. Topik Graph Request/Response
const mqttTopicGraphRequest = "graph/request";
const mqttTopicGraphResponse = "graph/response";
// ====================================================

// Setup History Chart (Line)
const ctxHistory = document.getElementById("historyChart").getContext("2d");
const chartInner = document.getElementById("chartInner");
const chartScrollWrapper = document.getElementById("chartScrollWrapper");
const POINTS_TO_SHOW = 7; // Default visible points
const POINT_WIDTH = 80; // Width per data point in pixels

// Helper function for legend
const getOrCreateLegendList = (chart, id) => {
  const legendContainer = document.getElementById(id);
  if (!legendContainer) return null;

  let listContainer = legendContainer.querySelector('ul');

  if (!listContainer) {
    listContainer = document.createElement('ul');
    listContainer.style.display = 'flex';
    listContainer.style.flexDirection = 'row';
    listContainer.style.justifyContent = 'center';
    listContainer.style.margin = '0';
    listContainer.style.padding = '0';
    listContainer.style.listStyle = 'none';
    listContainer.style.gap = '16px';

    legendContainer.appendChild(listContainer);
  }

  return listContainer;
};

// Custom HTML Legend Plugin (must be defined BEFORE Chart)
const htmlLegendPlugin = {
  id: 'htmlLegend',
  afterUpdate(chart, args, options) {
    const ul = getOrCreateLegendList(chart, options.containerID);

    // Remove old legend items
    while (ul.firstChild) {
      ul.firstChild.remove();
    }

    // Reuse the built-in legendItems generator
    const items = chart.options.plugins.legend.labels.generateLabels(chart);

    items.forEach(item => {
      const li = document.createElement('li');
      li.style.alignItems = 'center';
      li.style.cursor = 'pointer';
      li.style.display = 'flex';
      li.style.gap = '8px';

      li.onclick = () => {
        const {type} = chart.config;
        if (type === 'pie' || type === 'doughnut') {
          chart.toggleDataVisibility(item.index);
        } else {
          chart.setDatasetVisibility(item.datasetIndex, !chart.isDatasetVisible(item.datasetIndex));
        }
        chart.update();
      };

      // Color box
      const boxSpan = document.createElement('span');
      boxSpan.style.background = item.fillStyle;
      boxSpan.style.borderColor = item.strokeStyle;
      boxSpan.style.borderWidth = item.lineWidth + 'px';
      boxSpan.style.display = 'inline-block';
      boxSpan.style.height = '12px';
      boxSpan.style.width = '12px';
      boxSpan.style.borderRadius = '2px';

      // Text
      const textContainer = document.createElement('span');
      textContainer.style.color = '#fff';
      textContainer.style.fontSize = '0.9rem';
      textContainer.style.textDecoration = item.hidden ? 'line-through' : '';

      const text = document.createTextNode(item.text);
      textContainer.appendChild(text);

      li.appendChild(boxSpan);
      li.appendChild(textContainer);
      ul.appendChild(li);
    });
  }
};

// Create the chart with the plugin
const historyChart = new Chart(ctxHistory, {
  type: "line",
  data: {
    labels: [],
    datasets: [
      {
        label: "Penggunaan (kWh)",
        data: [],
        borderColor: "#4caf50",
        backgroundColor: "rgba(76, 175, 80, 0.2)",
        borderWidth: 2,
        tension: 0.4,
        fill: true,
      },
    ],
  },
  options: {
    responsive: false,
    maintainAspectRatio: false,
    scales: {
      x: { ticks: { color: "#aaa" }, grid: { color: "#333" } },
      y: {
        beginAtZero: true,
        ticks: { color: "#aaa" },
        grid: { color: "#333" },
      },
    },
    plugins: {
      legend: {
        display: false,
      },
      htmlLegend: {
        containerID: 'chartLegendContainer',
      },
    },
  },
  plugins: [htmlLegendPlugin],
});

// Helper: Update chart width and scroll to show last points
function updateChartSize(dataLength) {
  const minWidth = chartScrollWrapper ? chartScrollWrapper.clientWidth : 400;
  const calculatedWidth = Math.max(minWidth, dataLength * POINT_WIDTH);

  if (chartInner) {
    chartInner.style.width = calculatedWidth + "px";
  }

  // Resize canvas
  historyChart.canvas.style.width = calculatedWidth + "px";
  historyChart.canvas.style.height = "300px";
  historyChart.resize();

  // Scroll to the end to show latest data
  if (chartScrollWrapper) {
    setTimeout(() => {
      chartScrollWrapper.scrollLeft = chartScrollWrapper.scrollWidth;
    }, 50);
  }
}

// Koneksi ke MQTT
const client = mqtt.connect(mqttBroker);

// Update debug info
const mqttDebugEl = document.getElementById("mqttDebug");
if (mqttDebugEl) {
  mqttDebugEl.textContent = `MQTT: ${mqttBroker}`;
}

// Elemen DOM
const statusDiv = document.getElementById("status");
const quotaRemainingEl = document.getElementById("quota-remaining");
const heroEl = document.getElementById("heroQuota");
const relayToggle = document.getElementById("relayToggle");
const relayStatus = document.getElementById("relayStatus");
const relayCard = document.getElementById("relayCard");
const relayLast = document.getElementById("relayLast");

// Konstanta Warning
const QUOTA_WARN_YELLOW = 500;
const QUOTA_WARN_RED = 100;

// Track kuota dan status relay disabled
let currentQuota = null;
let isRelayDisabled = false;

if (quotaRemainingEl) quotaRemainingEl.innerText = "--";
if (relayLast) relayLast.innerText = "--";

// ================= EVENT KONEKSI =================
client.on("connect", () => {
  console.log("Connected to MQTT via WebSockets");
  statusDiv.textContent = "Connected";
  statusDiv.className = "status connected";

  // Subscribe Topic
  client.subscribe(mqttTopicPzem);
  client.subscribe(mqttTopicEnergyRemain);

  // PENTING: Hanya subscribe ke STATE untuk update tampilan
  // Jangan subscribe ke SET agar tidak bingung sendiri
  client.subscribe(mqttTopicRelayState);
  console.log("Subscribed to state:", mqttTopicRelayState);

  // Subscribe graph response
  client.subscribe(mqttTopicGraphResponse);

  // Request default graph (hourly)
  requestGraph("hourly");
});

client.on("offline", () => {
  statusDiv.textContent = "Disconnected";
  statusDiv.className = "status disconnected";
});

// ================= LOGIKA TAMPILAN (HELPER) =================
let isProgrammaticUpdate = false; // <--- FLAG PENTING ANTI JITTER

function updateRelayVisuals(isOn) {
  // Aktifkan Lock: Beritahu sistem bahwa perubahan ini berasal dari CODE, bukan USER
  isProgrammaticUpdate = true;

  // 1. Update Teks Status
  if (relayStatus) {
    if (isRelayDisabled) {
      relayStatus.innerText = "DISABLED";
    } else {
      relayStatus.innerText = isOn ? "ON" : "OFF";
    }
  }

  // 2. Update Warna Kartu
  if (relayCard) {
    relayCard.classList.remove("relay-on", "relay-off", "relay-disabled");
    if (isRelayDisabled) {
      relayCard.classList.add("relay-disabled");
    } else if (isOn) {
      relayCard.classList.add("relay-on");
    } else {
      relayCard.classList.add("relay-off");
    }
  }

  // 3. Update Posisi Toggle Switch
  if (relayToggle) {
    if (relayToggle.checked !== isOn) {
      relayToggle.checked = isOn;
    }
    // Disable toggle jika kuota habis
    relayToggle.disabled = isRelayDisabled;
  }

  // Matikan Lock setelah update selesai (beri sedikit jeda agar aman)
  setTimeout(() => {
    isProgrammaticUpdate = false;
  }, 50);
}

// Helper: Update Hero Quota
function updateQuotaHero(remaining) {
  if (!quotaRemainingEl || !heroEl) return;

  currentQuota = remaining;
  const displayValue = remaining < 0 ? 0 : remaining;
  quotaRemainingEl.innerText = displayValue.toFixed(2);

  heroEl.classList.remove("hero-blue", "hero-yellow", "hero-red");
  if (remaining <= QUOTA_WARN_RED) {
    heroEl.classList.add("hero-red");
  } else if (remaining <= QUOTA_WARN_YELLOW) {
    heroEl.classList.add("hero-yellow");
  } else {
    heroEl.classList.add("hero-blue");
  }

  // Cek jika kuota habis (<= 0), matikan relay dan disable kontrol
  const wasDisabled = isRelayDisabled;
  isRelayDisabled = remaining <= 0;

  if (isRelayDisabled && !wasDisabled) {
    // Kuota baru saja habis - kirim perintah OFF ke relay
    console.log("Kuota habis! Mematikan relay...");
    if (client && client.connected) {
      client.publish(mqttTopicRelaySet, "OFF", { qos: 1, retain: false });
    }
    updateRelayVisuals(false);
  } else if (!isRelayDisabled && wasDisabled) {
    // Kuota baru diisi ulang - enable kembali kontrol
    console.log("Kuota tersedia kembali!");
    updateRelayVisuals(relayToggle ? relayToggle.checked : false);
  } else if (isRelayDisabled) {
    // Masih disabled, pastikan UI tetap disabled
    updateRelayVisuals(false);
  }
}

// ================= HANDLE PESAN MASUK =================
client.on("message", (topic, message) => {
  try {
    const msgString = message.toString();

    // --- 1. HANDLE RELAY STATE (Laporan Kebenaran) ---
    if (topic === mqttTopicRelayState) {
      const state = msgString.toUpperCase();
      const isOn = state === "ON";

      console.log("Status Relay Baru diterima:", state);
      updateRelayVisuals(isOn); // Update UI sesuai fakta dari ESP32
      return;
    }

    // --- 2. HANDLE GRAPH RESPONSE ---
    if (topic === mqttTopicGraphResponse) {
      try {
        const graphData = JSON.parse(msgString);
        // Update History Chart
        historyChart.data.labels = graphData.labels || [];
        historyChart.data.datasets[0].data = graphData.data || [];

        // Update subtitle with title from response
        const subtitleEl = document.getElementById("chartSubtitle");
        if (subtitleEl && graphData.title) {
          subtitleEl.textContent = graphData.title;
          subtitleEl.style.display = "block";
        } else if (subtitleEl) {
          subtitleEl.textContent = "";
          subtitleEl.style.display = "none";
        }

        // Update chart size and scroll to end
        updateChartSize(historyChart.data.labels.length);
        historyChart.update();

        console.log("Grafik diperbarui!", graphData);
      } catch (e) {
        console.error("Gagal parsing data grafik", e);
      }
      return;
    }

    // --- 3. HANDLE DATA LAIN (JSON) ---
    const data = JSON.parse(msgString);

    if (topic === mqttTopicEnergyRemain) {
      // Handle energy remain format: {"remaining_quota": 123.45}
      // atau format angka langsung jika Node-RED kirim angka
      let remaining = data.remaining_quota;
      if (remaining === undefined && typeof data === "number") remaining = data;

      if (typeof remaining === "number") {
        updateQuotaHero(remaining);
      }
      return;
    }

    // Default: pzem/data topic
    document.getElementById("val-volt").innerText = data.voltage;
    document.getElementById("val-amp").innerText = data.current;
    document.getElementById("val-watt").innerText = data.power;

    const energyKwh = data.energy_dev || 0; // Asumsi data masuk sudah kWh atau sesuaikan
    document.getElementById("val-kwh").innerText = energyKwh.toFixed(3);
    document.getElementById("val-freq").innerText = data.freq;
    document.getElementById("val-pf").innerText = data.pf;
  } catch (e) {
    console.error("Error processing message:", e);
  }
});

// ================= HANDLE TOMBOL (PUBLISH COMMAND) =================
if (relayToggle) {
  relayToggle.addEventListener("change", () => {
    // CEK LOCK: Jika perubahan ini disebabkan oleh updateRelayVisuals(), JANGAN kirim MQTT
    if (isProgrammaticUpdate) {
      console.log("Perubahan visual dari MQTT diabaikan oleh trigger.");
      return;
    }

    // Logika Normal User Click
    const command = relayToggle.checked ? "ON" : "OFF";

    // Update teks status sementara (untuk feedback instan ke user)
    if (relayStatus) relayStatus.innerText = "Waiting...";

    if (client && client.connected) {
      // Retain: False (Perintah jangan pernah diretain)
      client.publish(
        mqttTopicRelaySet,
        command,
        { qos: 1, retain: false },
        (err) => {
          if (err) {
            console.error("Publish error:", err);
            // Jika gagal, kembalikan posisi toggle
            isProgrammaticUpdate = true;
            relayToggle.checked = !relayToggle.checked;
            setTimeout(() => (isProgrammaticUpdate = false), 50);
          } else {
            console.log("Perintah dikirim:", command);
          }
        },
      );
    } else {
      console.warn("MQTT offline");
      // Balikkan toggle jika offline
      isProgrammaticUpdate = true;
      relayToggle.checked = !relayToggle.checked;
      setTimeout(() => (isProgrammaticUpdate = false), 50);
    }
  });
}

// ================= FUNGSI GRAPH REQUEST =================
let currentGraphMode = "hourly";

function requestGraph(mode) {
  currentGraphMode = mode;

  // Update tombol aktif
  const btnHourly = document.getElementById("btnHourly");
  const btnDaily = document.getElementById("btnDaily");
  if (btnHourly && btnDaily) {
    btnHourly.classList.toggle("active", mode === "hourly");
    btnDaily.classList.toggle("active", mode === "daily");
  }

  // Kirim request ke Node-RED
  console.log("Meminta grafik:", mode);
  if (client && client.connected) {
    client.publish(mqttTopicGraphRequest, mode);
  } else {
    console.warn("MQTT not connected, cannot request graph");
  }
}
