#define PZEM_004T
#include <PZEMPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

// ===== WIFI =====
const char* ssid = "HelloaThere";
const char* password = "0987654321";

// ===== MQTT =====
const char* mqtt_server = "10.171.74.12";
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "pzem/data";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== TIMER =====
unsigned long lastLoop = 0;
const unsigned long LOOP_INTERVAL = 1000;    // kirim tiap 1 detik

const float POWER_THRESHOLD_W = 0.3;

void setupWiFi() {
  Serial.println("Connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("MQTT...");
    if (client.connect("ESP32_PZEM")) {
      Serial.println("connected");
    } else {
      Serial.println("retry in 1s");
      delay(1000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pzem.begin();
  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);

  lastLoop = millis();
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  unsigned long now = millis();

  // ============================
  //   EXECUTE ONLY EVERY 1 SEC
  // ============================
  if (now - lastLoop >= LOOP_INTERVAL) {
    lastLoop = now;

    float v, i, p, e, f, pf;

    if (pzem.readAll(&v, &i, &p, &e, &f, &pf)) {

      // Hitung delta KWh selama 1 detik
      float P_use = (p > POWER_THRESHOLD_W) ? p : 0.0;
      float deltaWh  = P_use * (1.0 / 3600.0);  // karena interval 1 detik
      float deltaKWh = deltaWh / 1000.0;

      // Print Serial
      Serial.printf("[MQTT] V=%.1f I=%.3f P=%.3f dKWh=%.6f\n",
                    v, i, p, deltaKWh);

      // ============================
      //   MQTT JSON PAYLOAD
      // ============================
      char payload[200];
      snprintf(payload, sizeof(payload),
               "{\"voltage\":%.1f,\"current\":%.3f,\"power\":%.3f,\"deltakwh\":%.6f}",
               v, i, p, deltaKWh);

      client.publish(mqtt_topic, payload);
    }
    else {
      Serial.println("⚠️ Failed to read PZEM!");
    }
  }
}
