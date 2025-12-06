#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>

Preferences prefs;
WebServer server(80);

WiFiClient espClient;
PubSubClient mqtt(espClient);

const char* apSSID = "ESP32-Setup12";
const char* apPASS = "0987654321";

String savedSSID, savedPASS, savedServer;

// ---------------------- MQTT CALLBACK ----------------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  Serial.print("MQTT Message [");
  Serial.print(topic);
  Serial.print("]: ");

  for (int i = 0; i < len; i++) Serial.print((char)payload[i]);
  Serial.println();
}

// Try mqtt connect
void mqttReconnect() {
  if (!mqtt.connected()) {
    Serial.print("Connecting MQTT to ");
    Serial.println(savedServer);

    mqtt.setServer(savedServer.c_str(), 1883);

    if (mqtt.connect("ESP32-Client")) {
      Serial.println("MQTT Connected!");
      mqtt.subscribe("test/topic");   // ----------- IMPORTANT
      Serial.println("Subscribed to test/topic");
    } else {
      Serial.print("Failed, rc=");
      Serial.println(mqtt.state());
    }
  }
}


// ---------------------------------------
// HTML PAGE: WiFi scan
// ---------------------------------------
String pageScan() {
  int n = WiFi.scanNetworks();
  String html = "<h2>Pilih WiFi</h2><ul>";

  for (int i = 0; i < n; i++) {
    html += "<li><a href='/connect?ssid=" + WiFi.SSID(i) + "'>";
    html += WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + ")</a></li>";
  }
  html += "</ul>";
  return html;
}

// ---------------------------------------
// HTML PAGE: WiFi password input
// ---------------------------------------
String pagePassword(String ssid) {
  String html = "<h2>Koneksi ke: " + ssid + "</h2>";
  html += "<form action='/savewifi'>";
  html += "<input type='hidden' name='ssid' value='" + ssid + "'>";
  html += "Password: <input name='pass' type='password'><br><br>";
  html += "<button type='submit'>Next</button></form>";
  return html;
}

// ---------------------------------------
// HTML PAGE: Server IP input
// ---------------------------------------
String pageServer() {
  String html = "<h2>Server Setting</h2>";
  html += "<form action='/save'>";
  html += "MQTT Server IP: <input name='server' value='" + savedServer + "'><br><br>";
  html += "<button type='submit'>Save & Reboot</button></form>";
  return html;
}

// SAVE WiFi → next page (server)
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

// SAVE server → reboot
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
  Serial.println("HTTP server started");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  prefs.begin("config", false);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  savedServer = prefs.getString("server", "");
  prefs.end();

  // If credentials exist → try WiFi
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

      // Setup MQTT
      mqtt.setCallback(mqttCallback);
      mqttReconnect();
    } else {
      Serial.println("\nWiFi Failed.");
      startAP();
    }
  } else {
    startAP();
  }

  setupServer();
}

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) mqttReconnect();
    mqtt.loop();
  }
}
