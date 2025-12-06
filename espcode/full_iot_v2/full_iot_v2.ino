#define PZEM_004T
#include <PZEMPlus.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============ LCD ============
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============ PZEM CONFIG ============
#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

// ============ RELAY + PREFS + WEB ============
#define RELAY_PIN 23
const bool RELAY_ACTIVE_LOW = true;

Preferences prefs;
WebServer server(80);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// AP fallback
const char* apSSID = "ESP32-Setup12";
const char* apPASS = "0987654321";

// Stored config
String savedSSID, savedPASS, savedServer;

// ============ ENERGY VARIABLES ============
float localEnergyWh = 0.0;
unsigned long lastReadMillis = 0;
unsigned long lastLoopMillis = 0;

const unsigned long READ_INTERVAL_MS = 1000UL;
const float POWER_THRESHOLD_W = 0.3;

// ------------ RELAY UTILITY -------------
void relaySet(bool on) {
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  else digitalWrite(RELAY_PIN, on ? HIGH : LOW);

  // LCD update
  lcd.setCursor(0, 1);
  lcd.print("Relay: ");
  lcd.print(on ? "ON " : "OFF");
}

// ------------ MQTT CALLBACK -------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (String(topic) == "control/relay") {
    relaySet(msg == "ON");
  }
}

void mqttReconnect() {
  if (!mqtt.connected()) {
    mqtt.setServer(savedServer.c_str(), 1883);
    String clientId = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientId.c_str())) {
      mqtt.subscribe("control/relay");
    }
  }
}

// ============ WEB PAGES ============
String pageScan() {
  int n = WiFi.scanNetworks();
  String html = "<h2>Pilih WiFi</h2><ul>";
  for (int i = 0; i < n; i++) {
    html += "<li><a href='/connect?ssid=" + WiFi.SSID(i) + "'>";
    html += WiFi.SSID(i) + "</a></li>";
  }
  html += "</ul>";
  return html;
}

String pagePassword(String ssid) {
  return "<form action='/savewifi'>"
         "<input type='hidden' name='ssid' value='" + ssid + "'>"
         "Password: <input name='pass' type='password'><br><br>"
         "<button type='submit'>Next</button>"
         "</form>";
}

String pageServer() {
  return "<form action='/save'>"
         "MQTT server: <input name='server' value='" + savedServer + "'>"
         "<button type='submit'>Save & Reboot</button>"
         "</form>";
}

void handleSaveWiFi() {
  prefs.begin("config", false);
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("pass", server.arg("pass"));
  prefs.end();

  savedSSID = server.arg("ssid");
  savedPASS = server.arg("pass");
  server.send(200, "text/html", pageServer());
}

void handleSaveAll() {
  prefs.begin("config", false);
  prefs.putString("server", server.arg("server"));
  prefs.end();

  server.send(200, "text/html", "Saved! Rebooting...");
  delay(1500);
  ESP.restart();
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPASS);
}

void setupServer() {
  server.on("/", []() { server.send(200, "text/html", pageScan()); });
  server.on("/connect", []() { server.send(200, "text/html", pagePassword(server.arg("ssid"))); });
  server.on("/savewifi", handleSaveWiFi);
  server.on("/save", handleSaveAll);
  server.begin();
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(200);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  relaySet(false);

  // PZEM init
  pzem.begin();

  // Load config
  prefs.begin("config", false);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  savedServer = prefs.getString("server", "");
  prefs.end();

  if (savedSSID != "") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      lcd.setCursor(0, 0);
      lcd.print("WiFi Connected  ");
      delay(1000); // ⬅ EXTRA 1 SECOND DELAY

      mqtt.setCallback(mqttCallback);
    } else {
      lcd.setCursor(0, 0);
      lcd.print("WiFi Failed     ");
      startAP();
    }

  } else {
    startAP();
    lcd.setCursor(0, 0);
    lcd.print("AP Mode Active  ");
  }

  setupServer();
  lastReadMillis = millis();
  lastLoopMillis = millis();
}

// ============ LOOP ============
void loop() {
  unsigned long now = millis();

  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected() && savedServer != "") mqttReconnect();
    if (mqtt.connected()) mqtt.loop();
  }

  // ----- READ PZEM EVERY 1s -----
  if (now - lastReadMillis >= READ_INTERVAL_MS) {
    lastReadMillis = now;

    float v, i, p, e, f, pf;

    if (pzem.readAll(&v, &i, &p, &e, &f, &pf)) {

      // ----- MQTT Publish -----
      if (mqtt.connected()) {
        String payload = "{";
        payload += "\"voltage\":" + String(v, 1) + ",";
        payload += "\"current\":" + String(i, 3) + ",";
        payload += "\"power\":" + String(p, 2) + ",";
        payload += "\"energy_dev\":" + String(e, 0) + ",";
        payload += "\"freq\":" + String(f, 1) + ",";
        payload += "\"pf\":" + String(pf, 2);
        payload += "}";

        bool ok = mqtt.publish("pzem/data", payload.c_str());

        lcd.setCursor(0, 0);
        lcd.print(ok ? "MQTT: SENT      " : "MQTT: FAIL      ");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("MQTT: NO LINK   ");
      }
    }
  }

  // SERIAL control
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '1') relaySet(true);
    if (c == '0') relaySet(false);
  }
}
