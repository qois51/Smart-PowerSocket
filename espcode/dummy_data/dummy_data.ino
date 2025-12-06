#include <WiFi.h>
#include <PubSubClient.h>

// ================= KONFIGURASI (EDIT DI SINI) =================
const char* ssid = "yaudahgitulah";      // Masukkan nama WiFi
const char* password = "1234567890";     // Masukkan password WiFi
const char* mqtt_server = "172.23.84.12"; // Masukkan IP Address Broker MQTT Anda

// ================= KONFIGURASI TOPIK & PIN ====================
// Topik sesuai kesepakatan arsitektur Control vs State
const char* topic_relay_set = "relay/set";    // Dengar perintah di sini
const char* topic_relay_state = "relay/state"; // Lapor status ke sini
const char* topic_pzem_data = "pzem/data";          // Data energi dummy

// Pin Relay (Menggunakan Built-in LED untuk simulasi)
const int relayPin = 2; 
// ==============================================================

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ============ VARIABEL SIMULASI ENERGI ============
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL_MS = 1000UL;
float simEnergy = 0.0; 

// ------------ MQTT CALLBACK (Menerima Pesan) -------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase(); // Ubah ke huruf besar agar "on", "On", "ON" sama saja

  Serial.print("Perintah masuk [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  // Cek apakah pesan berasal dari topik perintah relay
  if (String(topic) == topic_relay_set) {
    if (msg == "ON") {
      digitalWrite(relayPin, HIGH); // Nyalakan LED (Relay)
      Serial.println("Relay ON");
      
      // KIRIM FEEDBACK KE TOPIK STATE
      mqtt.publish(topic_relay_state, "ON", true); 
      
    } else if (msg == "OFF") {
      digitalWrite(relayPin, LOW);  // Matikan LED (Relay)
      Serial.println("Relay OFF");
      
      // KIRIM FEEDBACK KE TOPIK STATE
      mqtt.publish(topic_relay_state, "OFF", true);
    }
  }
}

// ------------ KONEKSI WIFI -------------
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Terhubung");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ------------ KONEKSI MQTT -------------
void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("Menghubungkan ke MQTT...");
    
    String clientId = "ESP32-RELAY-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    // Coba connect
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("BERHASIL!");
      
      // 1. SUBSCRIBE ke topik perintah (SET)
      mqtt.subscribe(topic_relay_set);
      Serial.print("Subscribed to: ");
      Serial.println(topic_relay_set);

      // 2. SINKRONISASI AWAL (Opsional tapi bagus)
      // Lapor status relay saat ini ke broker agar dashboard sync
      if(digitalRead(relayPin) == HIGH) {
        mqtt.publish(topic_relay_state, "ON", true);
      } else {
        mqtt.publish(topic_relay_state, "OFF", true);
      }

    } else {
      Serial.print("GAGAL, rc=");
      Serial.print(mqtt.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);

  // Setup Pin Relay/LED
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // Default OFF saat nyala pertama

  // Setup WiFi & MQTT
  setup_wifi();
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqttCallback);
}

// ============ LOOP ============
void loop() {
  if (!mqtt.connected()) {
    mqttReconnect();
  }
  mqtt.loop();

  unsigned long now = millis();

  // ----- SIMULASI DATA PZEM (Setiap 1 Detik) -----
  if (now - lastReadMillis >= READ_INTERVAL_MS) {
    lastReadMillis = now;

    // Generate Data Acak
    float v = 220.0 + (random(-50, 50) / 10.0);
    float i = (random(5, 200) / 100.0);
    float pf = 0.90 + (random(0, 9) / 100.0);
    float f = 49.9 + (random(0, 3) / 10.0);
    float p = v * i * pf; 
    
    simEnergy += (p / 3600.0); 

    // Buat JSON Payload
    String payload = "{";
    payload += "\"voltage\":" + String(v, 1) + ",";
    payload += "\"current\":" + String(i, 3) + ",";
    payload += "\"power\":" + String(p, 2) + ",";
    payload += "\"energy_dev\":" + String(simEnergy, 3) + ","; // Hati2 nama field sesuaikan Influx
    payload += "\"freq\":" + String(f, 1) + ",";
    payload += "\"pf\":" + String(pf, 2);
    payload += "}";

    // Publish Data Energi
    mqtt.publish(topic_pzem_data, payload.c_str());
    // Serial.println("Data Energi Terkirim"); // Uncomment jika ingin debug data energi
  }
}