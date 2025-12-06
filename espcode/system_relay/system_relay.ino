#define PZEM_004T
#include <PZEMPlus.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>

// ======================
//   PZEM CONFIG
// ======================
#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

// ======================
//   RELAY + PREFS + WEB
// ======================
#define RELAY_PIN 23   // pin relay

const bool RELAY_ACTIVE_LOW = true; // true = aktif saat LOW (ubah kalau modulmu aktif HIGH)

Preferences prefs;
WebServer server(80);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// AP default (untuk config)
const char* apSSID = "ESP32-Setup12";
const char* apPASS = "0987654321";

// config vars (dari prefs "config")
String savedSSID, savedPASS, savedServer;

// ======================
//   ENERGY VARIABLES
// ======================
float localEnergyWh = 0.0; // mulai dari 0 tiap boot (Opsi B)
unsigned long lastReadMillis = 0;
unsigned long lastLoopMillis = 0;

const unsigned long READ_INTERVAL_MS  = 1000UL;   // baca tiap 1 detik
const float POWER_THRESHOLD_W = 0.3;              // abaikan noise kecil di bawah 0.3 W

// ----------------------
// utilities
// ----------------------
void relaySet(bool on) {
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  else digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

// ---------------------- MQTT CALLBACK ----------------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  Serial.print("MQTT [");
  Serial.print(topic);
  Serial.print("]: ");

  String msg = "";
  for (unsigned int i = 0; i < len; i++) {
    Serial.print((char)payload[i]);
    msg += (char)payload[i];
  }
  Serial.println();

  // ---- Relay control via MQTT ----
  if (String(topic) == "control/relay") {
    msg.toUpperCase();
    msg.trim();

    if (msg == "ON") {
      relaySet(true);
      Serial.println("Relay ON (via MQTT)");
    } else if (msg == "OFF") {
      relaySet(false);
      Serial.println("Relay OFF (via MQTT)");
    }
  }
}

void mqttReconnect() {
  if (!mqtt.connected()) {
    Serial.print("Connecting to MQTT: ");
    Serial.println(savedServer);

    mqtt.setServer(savedServer.c_str(), 1883);

    String clientId = "ESP32-Client-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientId.c_str())) {
      Serial.println("MQTT Connected");
      mqtt.subscribe("control/relay");
      Serial.println("Subscribed to control/relay");
    } else {
      Serial.print("Failed to connect to MQTT, rc=");
      Serial.println(mqtt.state());
    }
  }
}

// ---------------------- Web pages for WiFi/MQTT config ----------------------
String pageScan() {
  int n = WiFi.scanNetworks();
  String html = "<h2>Pilih WiFi</h2><ul>";
  for (int i = 0; i < n; i++) {
    String ss = WiFi.SSID(i);
    html += "<li><a href='/connect?ssid=" + ss + "'>";
    html += ss + " (" + String(WiFi.RSSI(i)) + ")</a></li>";
  }
  html += "</ul>";
  return html;
}

String pagePassword(String ssid) {
  String html = "<h2>Koneksi ke: " + ssid + "</h2>";
  html += "<form action='/savewifi'>";
  html += "<input type='hidden' name='ssid' value='" + ssid + "'>";
  html += "Password: <input name='pass' type='password'><br><br>";
  html += "<button type='submit'>Next</button></form>";
  return html;
}

String pageServer() {
  String html = "<h2>Server Setting</h2>";
  html += "<form action='/save'>";
  html += "MQTT Server IP/host: <input name='server' value='" + savedServer + "'><br><br>";
  html += "<button type='submit'>Save & Reboot</button></form>";
  return html;
}

void handleSaveWiFi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  prefs.begin("config", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  savedSSID = ssid;
  savedPASS = pass;

  server.send(200, "text/html", pageServer());
}

void handleSaveAll() {
  String serverIP = server.arg("server");

  prefs.begin("config", false);
  prefs.putString("server", serverIP);
  prefs.end();

  server.send(200, "text/html", "<h2>Saved! Rebooting...</h2>");
  delay(1500);
  ESP.restart();
}

void startAP() {
  Serial.println("Starting AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupServer() {
  server.on("/", []() { server.send(200, "text/html", pageScan()); });
  server.on("/connect", []() { server.send(200, "text/html", pagePassword(server.arg("ssid"))); });
  server.on("/savewifi", handleSaveWiFi);
  server.on("/save", handleSaveAll);
  server.begin();
  Serial.println("HTTP Server Started");
}

// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // RELAY
  pinMode(RELAY_PIN, OUTPUT);
  relaySet(false); // Relay OFF aman saat boot
  Serial.println("Relay OFF at boot.");

  // PZEM
  pzem.begin();

  // PREFS CONFIG
  prefs.begin("config", false);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  savedServer = prefs.getString("server", "");
  prefs.end();

  // WiFi connect or AP
  if (savedSSID != "") {
    Serial.println("Connecting WiFi: " + savedSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      Serial.print(".");
      delay(500);
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected!");
      Serial.print("STA IP: ");
      Serial.println(WiFi.localIP());

      mqtt.setCallback(mqttCallback);
      // mqttReconnect() akan dipanggil di loop
    } else {
      Serial.println("\nWiFi Failed.");
      startAP();
    }
  } else {
    startAP();
  }

  setupServer();

  // ENERGY mulai dari 0 (Opsi B)
  localEnergyWh = 0.0f;
  lastReadMillis = millis();
  lastLoopMillis = millis();

  Serial.printf("LocalEnergy reset to %.3f Wh (start at boot)\n", localEnergyWh);
}

// ------------------ LOOP ------------------
void loop() {
  unsigned long now = millis();

  // handle web server
  server.handleClient();

  // handle wifi/mqtt
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected() && savedServer != "") mqttReconnect();
    if (mqtt.connected()) mqtt.loop();
  }

  // baca PZEM tiap 1 detik
  if (now - lastReadMillis >= READ_INTERVAL_MS) {
    lastReadMillis = now;

    float v, i, p, e, f, pf;
    if (pzem.readAll(&v, &i, &p, &e, &f, &pf)) {

      Serial.println("=== PZEM Measurements ===");
      Serial.printf("Voltage   : %.1f V\n", v);
      Serial.printf("Current   : %.3f A\n", i);
      Serial.printf("Power     : %.2f W\n", p);
      Serial.printf("Energy dev: %.0f Wh\n", e);
      Serial.printf("Freq      : %.1f Hz\n", f);
      Serial.printf("PF        : %.2f\n", pf);
      Serial.println("==========================");

      // Integrasi Wh (runtime only)
      unsigned long dt_ms = now - lastLoopMillis;
      lastLoopMillis = now;
      float dt_s = dt_ms / 1000.0f;

      float powerForIntegration = (p > POWER_THRESHOLD_W) ? p : 0.0f;
      float deltaWh = powerForIntegration * (dt_s / 3600.0f);
      localEnergyWh += deltaWh;

      Serial.printf("DeltaWh: %.6f Wh, LocalEnergy: %.6f Wh\n", deltaWh, localEnergyWh);

    } else {
      Serial.println("⚠️ Failed to read PZEM!");
    }
  }

  // serial commands: '1' ON, '0' OFF, 'D' hapus config prefs
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '1') {
      relaySet(true);
      Serial.println("Relay ON (via Serial)");
    } else if (c == '0') {
      relaySet(false);
      Serial.println("Relay OFF (via Serial)");
    } else if (c == 'D' || c == 'd') {
      prefs.begin("config", false);
      prefs.clear();
      prefs.end();
      Serial.println("Config prefs cleared. Rebooting...");
      delay(500);
      ESP.restart();
    }
  }
}
