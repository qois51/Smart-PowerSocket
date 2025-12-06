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

// ============ KONFIGURASI TOPIK MQTT ============
const char* topic_relay_set   = "relay/set";    
const char* topic_relay_state = "relay/state";  
const char* topic_pzem_data   = "pzem/data";          

// ============ PZEM CONFIG ============
#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

// Variabel Global untuk Trip Meter
float startKwh = 0.0;
bool isFirstRun = true;

// Variabel Data
float v, i, p, e_total, f, pf;
float e_session = 0.0; // Ini yang akan ditampilkan ke User

// ============ RELAY + PREFS + WEB ============
#define RELAY_PIN 23
const bool RELAY_ACTIVE_LOW = true; 

Preferences prefs;
WebServer server(80);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// AP fallback 
const char* apSSID = "ESP32-Setup-PZEM";
const char* apPASS = "12345678";

// Stored config
String savedSSID, savedPASS, savedServer;

// ============ ENERGY VARIABLES ============
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL_MS = 1000UL;

// ============ CSS STYLING ============
const char* htmlStyle = 
"<style>"
"body { font-family: Arial; text-align: center; margin-top: 20px; }"
"button { background-color: #008CBA; color: white; padding: 14px 20px; margin: 8px 0; border: none; cursor: pointer; width: 80%; font-size: 16px; border-radius: 5px; }"
"button.save { background-color: #4CAF50; }"
"button.reboot { background-color: #f44336; }"
"input { padding: 10px; width: 80%; margin-bottom: 10px; font-size: 16px; }"
"h2 { color: #333; }"
"a { text-decoration: none; }"
"li { text-align: left; margin-left: 30%; }"
"</style>";

// ------------ FUNGSI UPDATE STATUS RELAY DI LCD -------------
// Fungsi kecil untuk mengembalikan tampilan status Relay di baris bawah
// Dipanggil setelah pesan error selesai ditampilkan
void updateLcdRelayState() {
    bool isOn = false;
    if (RELAY_ACTIVE_LOW) isOn = (digitalRead(RELAY_PIN) == LOW);
    else isOn = (digitalRead(RELAY_PIN) == HIGH);

    lcd.setCursor(0, 1);
    lcd.print("Relay: ");
    lcd.print(isOn ? "ON " : "OFF");
}

// ------------ FUNGSI KONTROL RELAY -------------
void relaySet(bool on) {
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  else digitalWrite(RELAY_PIN, on ? HIGH : LOW);

  updateLcdRelayState();

  if (mqtt.connected()) {
    mqtt.publish(topic_relay_state, on ? "ON" : "OFF", true); 
  }
}

// ------------ FUNGSI RESET ENERGY SESSION -------------
void resetEnergySession() {
    // Kita tidak mereset PZEM-nya, tapi kita geser titik start-nya
    // ke posisi kWh saat ini. Jadi selisihnya jadi 0 lagi.
    startKwh = e_total; 
    lcd.setCursor(0, 1);
    lcd.print("Energy Reset!   ");
    delay(1000);
}

// Update Callback MQTT untuk dukung perintah Reset
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (String(topic) == topic_relay_set) {
    if (msg == "ON") relaySet(true);
    else if (msg == "OFF") relaySet(false);
    // Tambahan: Perintah Reset via MQTT
    else if (msg == "RESET") resetEnergySession();
  }
}

// ------------ KONEKSI ULANG MQTT (DENGAN LCD REPORT) -------------
void mqttReconnect() {
  if (!mqtt.connected()) {
    if (savedServer == "") {
        lcd.setCursor(0, 0);
        lcd.print("No MQTT IP Set! ");
        return; 
    }
    
    // Info di LCD: Sedang mencoba connect
    lcd.setCursor(0, 0);
    lcd.print("MQTT: Mencoba...");
    
    mqtt.setServer(savedServer.c_str(), 1883);
    String clientId = "ESP32-PZEM-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientId.c_str())) {
      // BERHASIL CONNECT
      lcd.setCursor(0, 0);
      lcd.print("MQTT: Connected!");
      delay(1000); // Tahan sebentar biar terbaca

      mqtt.subscribe(topic_relay_set);
      
      // Sinkronisasi status relay
      int currentState = digitalRead(RELAY_PIN);
      bool isOn = false;
      if (RELAY_ACTIVE_LOW) isOn = (currentState == LOW);
      else isOn = (currentState == HIGH);

      mqtt.publish(topic_relay_state, isOn ? "ON" : "OFF", true);
      
      // Kembalikan tampilan LCD ke normal (Baris bawah status relay)
      updateLcdRelayState();

    } else {
      // GAGAL CONNECT
      lcd.setCursor(0, 0);
      lcd.print("MQTT: GAGAL!    "); // Spasi untuk menimpa tulisan lama
      lcd.setCursor(0, 1);
      lcd.print("RC=" + String(mqtt.state()) + " Wait 2s"); // Tampilkan kode error
      
      Serial.print("MQTT Connect Failed. RC=");
      Serial.println(mqtt.state());
      
      delay(2000); // Tunggu 2 detik sebelum loop ulang (biar kebaca user)
      
      // Setelah delay, kita kembalikan baris 2 ke status relay agar tidak stuck di pesan error selamanya
      updateLcdRelayState();
    }
  }
}

// ============ WEB PAGES ============
// ... (Bagian Web Page sama persis dengan kode sebelumnya) ...

String pageMenu() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + String(htmlStyle) + "</head><body>";
  html += "<h2>ESP32 Config Menu</h2>";
  html += "<p>Status: " + String(WiFi.status() == WL_CONNECTED ? "Online" : "Offline") + "</p>";
  html += "<a href='/wifi'><button>Konfigurasi WiFi</button></a>";
  html += "<a href='/mqtt'><button>Konfigurasi MQTT IP</button></a>";
  html += "<br><br><hr>";
  html += "<a href='/reboot'><button class='reboot'>Reboot System</button></a>";
  html += "</body></html>";
  return html;
}

String pageScan() {
  int n = WiFi.scanNetworks();
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + String(htmlStyle) + "</head><body>";
  html += "<h2>Pilih Jaringan</h2><ul>";
  for (int i = 0; i < n; i++) {
    html += "<li><a href='/connect?ssid=" + WiFi.SSID(i) + "'>";
    html += WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</a></li>";
  }
  html += "</ul>";
  html += "<br><a href='/'><button>Kembali ke Menu</button></a>";
  html += "</body></html>";
  return html;
}

String pagePassword(String ssid) {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + String(htmlStyle) + "</head><body>";
  html += "<h2>Connect to " + ssid + "</h2>";
  html += "<form action='/savewifi'>";
  html += "<input type='hidden' name='ssid' value='" + ssid + "'>";
  html += "Password: <input name='pass' type='password' placeholder='WiFi Password'><br>";
  html += "<button type='submit' class='save'>Simpan & Connect</button>";
  html += "</form>";
  html += "<a href='/wifi'><button>Batal</button></a>";
  html += "</body></html>";
  return html;
}

String pageMqtt() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + String(htmlStyle) + "</head><body>";
  html += "<h2>Konfigurasi MQTT</h2>";
  html += "<form action='/savemqtt'>";
  html += "Broker IP: <input name='server' value='" + savedServer + "' placeholder='Ex: 192.168.1.5'><br>";
  html += "<button type='submit' class='save'>Simpan MQTT</button>";
  html += "</form>";
  html += "<a href='/'><button>Kembali ke Menu</button></a>";
  html += "</body></html>";
  return html;
}

// ============ HANDLERS ============

void handleRoot() { server.send(200, "text/html", pageMenu()); }
void handleWifiScan() { server.send(200, "text/html", pageScan()); }
void handleWifiConnect() { server.send(200, "text/html", pagePassword(server.arg("ssid"))); }
void handleMqttConfig() { server.send(200, "text/html", pageMqtt()); }

void handleSaveWiFi() {
  String s = server.arg("ssid");
  String p = server.arg("pass");
  if(s != "") {
    prefs.begin("config", false);
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    prefs.end();
    savedSSID = s; savedPASS = p;
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + String(htmlStyle) + "</head><body>";
    html += "<h2>WiFi Tersimpan!</h2><p>Silakan Reboot.</p><a href='/reboot'><button class='reboot'>Reboot Sekarang</button></a></body></html>";
    server.send(200, "text/html", html);
  } else {
    server.send(200, "text/html", "Error: SSID kosong! <a href='/'>Back</a>");
  }
}

void handleSaveMqtt() {
  String srv = server.arg("server");
  prefs.begin("config", false);
  prefs.putString("server", srv);
  prefs.end();
  savedServer = srv;
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + String(htmlStyle) + "</head><body>";
  html += "<h2>MQTT IP Tersimpan!</h2><p>IP: " + srv + "</p><a href='/'><button>Kembali ke Menu</button></a></body></html>";
  server.send(200, "text/html", html);
}

void handleReboot() {
  server.send(200, "text/html", "Rebooting...");
  delay(1000);
  ESP.restart();
}

void setupServer() {
  server.on("/", handleRoot);
  server.on("/wifi", handleWifiScan);
  server.on("/connect", handleWifiConnect);
  server.on("/savewifi", handleSaveWiFi);
  server.on("/mqtt", handleMqttConfig);
  server.on("/savemqtt", handleSaveMqtt);
  server.on("/reboot", handleReboot);
  server.begin();
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(200);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting System..");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW); 
  
  updateLcdRelayState();

  pzem.begin();

  prefs.begin("config", false);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  savedServer = prefs.getString("server", "");
  prefs.end();

  // WiFi Connection Logic
  bool wifiConnected = false;
  if (savedSSID != "") {
    lcd.setCursor(0, 0);
    lcd.print("WiFi: Connect...  ");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 15) { 
      delay(500);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      lcd.setCursor(0, 0);
      lcd.print("WiFi: OK!       "); // Kabar Sukses
      delay(1500); 
      
      Serial.println("\nWiFi Connected!");
      mqtt.setCallback(mqttCallback);
    } else {
      // --- KABAR GAGAL WIFI ---
      lcd.setCursor(0, 0);
      lcd.print("WiFi: GAGAL!    ");
      delay(2000); // Tahan biar user baca
    }
  }

  // Jika WiFi gagal, masuk AP Mode
  if (!wifiConnected) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Mode: AP Config "); // Kabar masuk mode config
    lcd.setCursor(0, 1);
    lcd.print("IP: 192.168.4.1 ");
    
    Serial.println("Starting AP Mode...");
    WiFi.mode(WIFI_AP_STA); 
    WiFi.softAP(apSSID, apPASS);
  }

  setupServer();
  lastReadMillis = millis();
}

// ============ LOOP ============
void loop() {
  unsigned long now = millis();
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
     if (!mqtt.connected() && savedServer != "") mqttReconnect();
     if (mqtt.connected()) mqtt.loop();
  }

  // ----- BACA SENSOR -----
  if (now - lastReadMillis >= READ_INTERVAL_MS) {
    lastReadMillis = now;

    if (!mqtt.connected() && savedServer != "") return;

    // Baca Semua Data
    if (pzem.readAll(&v, &i, &p, &e_total, &f, &pf)) {
      
      // Filter NaN
      if(isnan(v)) v = 0;
      if(isnan(p)) p = 0;
      if(isnan(e_total)) e_total = 0;

      // --- LOGIKA TRIP METER ---
      // Saat pertama kali loop berjalan, catat posisi odometer PZEM
      if (isFirstRun) {
         if (e_total > 0) startKwh = e_total;
         isFirstRun = false;
      }

      // Hitung Penggunaan Sesi Ini (Trip Meter)
      // Rumus: Total Hardware dikurang Posisi Awal
      e_session = e_total - startKwh;
      
      // Safety biar gak minus (kalau PZEM tiba2 direset hardware)
      if (e_session < 0) {
         startKwh = 0; 
         e_session = e_total;
      }
      // -------------------------

      // TAMPILAN LCD
      // Baris 1: Power (Realtime) & Session Energy (Akumulasi User)
      lcd.setCursor(0, 0);
      lcd.printf("P:%-4.0fW E:%-5.3f", p, e_session); 
      // Contoh hasil: "P:200 W E:0.150" (kWh sesi ini)

      if (mqtt.connected()) {
        String payload = "{";
        payload += "\"voltage\":" + String(v, 1) + ",";
        payload += "\"current\":" + String(i, 3) + ",";
        payload += "\"power\":" + String(p, 2) + ",";
        payload += "\"energy_dev\":" + String(e_session, 0) + ","; 
        payload += "\"freq\":" + String(f, 1) + ",";
        payload += "\"pf\":" + String(pf, 2);
        payload += "}";

        bool ok = mqtt.publish(topic_pzem_data, payload.c_str());
        // Tanda kirim sukses/gagal di ujung kanan atas
        lcd.setCursor(15, 0); 
        lcd.print(ok ? "*" : "X"); 
      }
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Sensor Error!   ");
    }
  }
  
  // Tombol Fisik / Serial untuk Reset Manual (Opsional)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') resetEnergySession(); // Ketik 'r' di serial monitor untuk reset
  }
}