#include <WiFi.h>
// #include <PubSubClient.h>

// --- WiFi credentials ---
const char* ssid = "HelloaThere";
const char* password = "0987654321";

// --- MQTT broker (your PC IP where Docker Mosquitto runs) ---
// const char* mqtt_server = "10.31.56.12";  // change to your host IP
// const int mqtt_port = 1883;

// --- MQTT topic to subscribe ---
// const char* sub_topic = "esp32/led";

// --- Global objects ---
WiFiClient espClient;
// PubSubClient client(espClient);

// --- Define LED pin ---
// const int ledPin = 2;

// --- Function declarations ---
void setup_wifi();
// void reconnect();
// void callback(char* topic, byte* message, unsigned int length);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // pinMode(ledPin, OUTPUT);
  // digitalWrite(ledPin, LOW); // start off

  setup_wifi();

  // client.setServer(mqtt_server, mqtt_port);
  // client.setCallback(callback);
}

void loop() {
  // if (!client.connected()) {
  //   reconnect();
  // }
  // client.loop();
}

// --- Connect to Wi-Fi ---
void setup_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// --- Handle messages received from broker ---
// void callback(char* topic, byte* message, unsigned int length) {
//   Serial.print("Message arrived [");
//   Serial.print(topic);
//   Serial.print("]: ");

//   String msg;
//   for (unsigned int i = 0; i < length; i++) {
//     msg += (char)message[i];
//   }
//   Serial.println(msg);

//   // Control LED
//   if (msg == "1") {
//     digitalWrite(ledPin, HIGH);
//     Serial.println("LED turned ON");
//   } else if (msg == "0") {
//     digitalWrite(ledPin, LOW);
//     Serial.println("LED turned OFF");
//   }
// }

// --- Reconnect to broker if disconnected ---
// void reconnect() {
//   while (!client.connected()) {
//     Serial.print("Attempting MQTT connection...");
//     if (client.connect("ESP32_LED_Client")) {
//       Serial.println("connected");
//       client.subscribe(sub_topic);
//       Serial.print("Subscribed to topic: ");
//       Serial.println(sub_topic);
//     } else {
//       Serial.print("failed, rc=");
//       Serial.print(client.state());
//       Serial.println(" — retrying in 5 seconds");
//       delay(5000);
//     }
//   }
// }