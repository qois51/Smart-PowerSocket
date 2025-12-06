#include <WiFi.h>
#include <PubSubClient.h>

#define RELAY_PIN 23   // Relay aktif LOW

// ===== WIFI =====
const char* ssid     = "HelloaThere";
const char* password = "0987654321";

// ===== MQTT =====
const char* mqtt_server = "10.144.22.12";
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "pzem/relay";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== TIMER =====
unsigned long lastMQTT = 0;
const unsigned long MQTT_INTERVAL = 1000;   // publish setiap 1 detik

// ==========================================
// WiFi Setup
// ==========================================
void setupWiFi() {
  Serial.println("Connecting WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ==========================================
// MQTT Reconnect
// ==========================================
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("MQTT...");
    if (client.connect("ESP32_RELAY")) {
      Serial.println("connected");
      client.subscribe("pzem/relay");   // listen command
    } else {
      Serial.println("retry in 1s");
      delay(1000);
    }
  }
}

// ==========================================
// MQTT Callback (command from broker)
// ==========================================
void callback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (int i = 0; i < len; i++) msg += (char)payload[i];

  Serial.print("CMD: ");
  Serial.println(msg);

  if (msg == "ON") {
    digitalWrite(RELAY_PIN, LOW);   // relay aktif LOW
  }
  else if (msg == "OFF") {
    digitalWrite(RELAY_PIN, HIGH);
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // default OFF

  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  lastMQTT = millis();
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  unsigned long now = millis();

  // Publish status every 1 second
  if (now - lastMQTT >= MQTT_INTERVAL) {
    lastMQTT = now;

    int relayState = digitalRead(RELAY_PIN) == LOW ? 1 : 0;

    char msg[50];
    snprintf(msg, sizeof(msg), "{\"relay\":%d}", relayState);

    client.publish(mqtt_topic, msg);

    Serial.print("Published: ");
    Serial.println(msg);
  }
}
