// ================= KONFIGURASI MQTT =================
const mqttBroker = "wss://mqtt.qoisfirosi.dpdns.org";
const mqttTopicAddQuota = "admin/add_quota"; // Topic untuk tambah kuota
const mqttTopicSetQuota = "admin/set_quota"; // Topic untuk set kuota langsung
const mqttTopicEnergyRemain = "data/energy_remain"; // Topic untuk sisa kuota

// Koneksi MQTT
const client = mqtt.connect(mqttBroker);

// Elemen DOM
const statusDiv = document.getElementById("status");
const currentQuotaEl = document.getElementById("currentQuota");
const quotaAmountInput = document.getElementById("quotaAmount");
const btnSubmit = document.getElementById("btnSubmit");
const toast = document.getElementById("toast");

// Collapsible section elements
const collapseHeader = document.getElementById("collapseHeader");
const collapseContent = document.getElementById("collapseContent");
const setCurrentQuota = document.getElementById("setCurrentQuota");
const setQuotaAmountInput = document.getElementById("setQuotaAmount");
const btnSetQuota = document.getElementById("btnSetQuota");

// Current quota value
let currentQuotaValue = 0;
let collapseHideTimeoutId = null;
const COLLAPSE_TRANSITION_MS = 300;

// Init ARIA for collapsible
if (collapseHeader && collapseContent) {
  collapseHeader.setAttribute("aria-controls", "collapseContent");
  collapseHeader.setAttribute("aria-expanded", "false");
  collapseHeader.setAttribute("role", "button");
  collapseHeader.setAttribute("tabindex", "0");
  collapseHeader.setAttribute("aria-label", "Buka Set Kuota Langsung");
  collapseContent.setAttribute("aria-hidden", "true");
}

// Keyboard support for collapse toggle
if (collapseHeader) {
  collapseHeader.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      toggleSetQuota();
    }
  });
}

// ================= MQTT EVENTS =================
client.on("connect", () => {
  console.log("Admin connected to MQTT");
  statusDiv.textContent = "Connected";
  statusDiv.className = "status connected";

  // Subscribe untuk mendapatkan kuota saat ini
  client.subscribe(mqttTopicEnergyRemain);
});

client.on("offline", () => {
  statusDiv.textContent = "Disconnected";
  statusDiv.className = "status disconnected";
});

client.on("message", (topic, message) => {
  try {
    if (topic === mqttTopicEnergyRemain) {
      const data = JSON.parse(message.toString());
      let remaining = data.remaining_quota;
      if (remaining === undefined && typeof data === "number") remaining = data;

      if (typeof remaining === "number") {
        currentQuotaValue = remaining;
        currentQuotaEl.textContent = remaining.toFixed(2);
        if (setCurrentQuota) {
          setCurrentQuota.textContent = remaining.toFixed(2);
        }
      }
    }
  } catch (e) {
    console.error("Error parsing message:", e);
  }
});

// ================= FUNCTIONS =================
function setQuotaAmount(amount) {
  quotaAmountInput.value = amount;
  quotaAmountInput.focus();
}

function submitQuota() {
  const amount = parseFloat(quotaAmountInput.value);

  if (isNaN(amount) || amount <= 0) {
    showToast("Masukkan jumlah kuota yang valid", "error");
    return;
  }

  if (!client || !client.connected) {
    showToast("Tidak terhubung ke server", "error");
    return;
  }

  // Disable button sementara
  btnSubmit.disabled = true;
  btnSubmit.innerHTML = "<span>Mengirim...</span>";

  // Kirim ke MQTT
  const payload = JSON.stringify({ add_quota: amount });
  client.publish(mqttTopicAddQuota, payload, { qos: 1 }, (err) => {
    btnSubmit.disabled = false;
    btnSubmit.innerHTML = `
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M12 2v20M2 12h20"/>
            </svg>
            Tambah Kuota
        `;

    if (err) {
      console.error("Publish error:", err);
      showToast("Gagal mengirim kuota", "error");
    } else {
      console.log("Kuota dikirim:", amount);
      showToast(`+${amount} kWh berhasil ditambahkan`, "success");

      // Clear input
      quotaAmountInput.value = "";
    }
  });
}

// ================= COLLAPSIBLE SECTION FUNCTIONS =================
function toggleSetQuota() {
  const isOpen = collapseContent.classList.contains("show");

  if (isOpen) {
    // Start collapse animation
    collapseContent.classList.remove("show");
    collapseHeader.classList.remove("active");
    collapseHeader.setAttribute("aria-expanded", "false");
    collapseContent.setAttribute("aria-hidden", "true");

    // Clear any previous pending hide timeout
    if (collapseHideTimeoutId) {
      clearTimeout(collapseHideTimeoutId);
      collapseHideTimeoutId = null;
    }

    // After transition ends, hide from layout (with reliable fallback)
    const onTransitionEnd = (e) => {
      if (e.target !== collapseContent) return;
      collapseContent.hidden = true;
    };
    collapseContent.addEventListener("transitionend", onTransitionEnd, { once: true });
    collapseHideTimeoutId = setTimeout(() => {
      collapseContent.hidden = true;
      collapseHideTimeoutId = null;
    }, COLLAPSE_TRANSITION_MS + 50);
    collapseHeader.setAttribute("aria-label", "Buka Set Kuota Langsung");
  } else {
    // Make it participate in layout first
    collapseContent.hidden = false;

    // Force reflow so transition will run when adding .show
    // eslint-disable-next-line no-unused-expressions
    collapseContent.offsetHeight;

    // Open state
    collapseContent.classList.add("show");
    collapseHeader.classList.add("active");
    collapseHeader.setAttribute("aria-expanded", "true");
    collapseContent.setAttribute("aria-hidden", "false");
    collapseHeader.setAttribute("aria-label", "Tutup Set Kuota Langsung");

    // If there was a pending hide timeout, cancel it
    if (collapseHideTimeoutId) {
      clearTimeout(collapseHideTimeoutId);
      collapseHideTimeoutId = null;
    }
    // Update current quota
    if (setCurrentQuota) {
      setCurrentQuota.textContent = currentQuotaValue.toFixed(2);
    }
  }
}

function setQuotaValue(value) {
  if (setQuotaAmountInput) {
    setQuotaAmountInput.value = value;
    setQuotaAmountInput.focus();
  }
}

function submitSetQuota() {
  const amount = parseFloat(setQuotaAmountInput.value);

  if (isNaN(amount) || amount < 0) {
    showToast("Masukkan nilai kuota yang valid", "error");
    return;
  }

  if (!client || !client.connected) {
    showToast("Tidak terhubung ke server", "error");
    return;
  }

  // Disable button sementara
  btnSetQuota.disabled = true;
  btnSetQuota.innerHTML = "<span>Mengirim...</span>";

  // Kirim ke MQTT
  const payload = JSON.stringify({ set_quota: amount });
  client.publish(mqttTopicSetQuota, payload, { qos: 1 }, (err) => {
    btnSetQuota.disabled = false;
    btnSetQuota.innerHTML = `
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <path d="M20 6L9 17l-5-5"/>
      </svg>
      Set Kuota
    `;

    if (err) {
      console.error("Publish error:", err);
      showToast("Gagal mengatur kuota", "error");
    } else {
      console.log("Kuota diset:", amount);
      showToast(`Kuota berhasil diatur ke ${amount} kWh`, "success");

      // Clear input
      setQuotaAmountInput.value = "";
    }
  });
}

function showToast(message, type = "success") {
  toast.textContent = message;
  toast.className = `toast ${type} show`;

  setTimeout(() => {
    toast.className = "toast";
  }, 3000);
}

// Enter key to submit
quotaAmountInput.addEventListener("keypress", (e) => {
  if (e.key === "Enter") {
    submitQuota();
  }
});
