#define PZEM_004T
#include <PZEMPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============ LCD ============
LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* fixedSSID = "esp32-pzem";
const char* fixedPASS = "12345678";

// Ganti dengan IP Broker MQTT Anda (Laptop/Raspberry Pi)
const char* fixedServer = "............";

const char* topic_relay_set   = "relay/set";
const char* topic_relay_state = "relay/state";
const char* topic_pzem_data   = "pzem/data";

#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

float startKwh = 0.0;
bool isFirstRun = true;

float v, i, p, e_total, f, pf;
float e_session = 0.0;

#define RELAY_PIN 23
const bool RELAY_ACTIVE_LOW = true;

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL_MS = 1000UL;

void updateLcdRelayState() {
  bool isOn = false;
  if (RELAY_ACTIVE_LOW) isOn = (digitalRead(RELAY_PIN) == LOW);
  else isOn = (digitalRead(RELAY_PIN) == HIGH);

  lcd.setCursor(0, 1);
  lcd.print("Relay: ");
  lcd.print(isOn ? "ON " : "OFF");
}

void relaySet(bool on) {
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  else digitalWrite(RELAY_PIN, on ? HIGH : LOW);

  updateLcdRelayState();

  if (mqtt.connected()) {
    mqtt.publish(topic_relay_state, on ? "ON" : "OFF", true);
  }
}

void resetEnergySession() {
  startKwh = e_total;
  lcd.setCursor(0, 1);
  lcd.print("Energy Reset!   ");
  delay(1000);
  updateLcdRelayState();
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (String(topic) == topic_relay_set) {
    if (msg == "ON") relaySet(true);
    else if (msg == "OFF") relaySet(false);
    else if (msg == "RESET") resetEnergySession();
  }
}

void mqttReconnect() {
  if (!mqtt.connected()) {
    if (String(fixedServer) == "") {
        lcd.setCursor(0, 0);
        lcd.print("No MQTT IP Set! ");
        return;
    }

    lcd.setCursor(0, 0);
    lcd.print("MQTT: Mencoba...");

    mqtt.setServer(fixedServer, 1883);
    String clientId = "ESP32-PZEM-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientId.c_str())) {
      lcd.setCursor(0, 0);
      lcd.print("MQTT: Connected!");
      delay(1000);

      mqtt.subscribe(topic_relay_set);

      int currentState = digitalRead(RELAY_PIN);
      bool isOn = false;
      if (RELAY_ACTIVE_LOW) isOn = (currentState == LOW);
      else isOn = (currentState == HIGH);

      mqtt.publish(topic_relay_state, isOn ? "ON" : "OFF", true);

      updateLcdRelayState();

    } else {
      lcd.setCursor(0, 0);
      lcd.print("MQTT: GAGAL!    ");
      lcd.setCursor(0, 1);
      lcd.print("RC=" + String(mqtt.state()) + " Wait 2s");

      Serial.print("MQTT Connect Failed. RC=");
      Serial.println(mqtt.state());

      delay(2000);
      updateLcdRelayState();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting System..");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);

  pzem.begin();

  lcd.setCursor(0, 0);
  lcd.print("WiFi: Connect...  ");

  WiFi.mode(WIFI_STA);
  WiFi.begin(fixedSSID, fixedPASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 10) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi: OK!       ");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    delay(1500);

    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    mqtt.setCallback(mqttCallback);

  } else {
    Serial.println("\nWiFi Gagal! Menunggu Hotspot...");

    unsigned long previousLcdMillis = 0;
    int displayState = 0;

    while (WiFi.status() != WL_CONNECTED) {
        unsigned long currentMillis = millis();

        if (currentMillis - previousLcdMillis >= 2000) {
            previousLcdMillis = currentMillis;
            lcd.clear();

            if (displayState == 0) {
                // Tampilan 1: Instruksi
                lcd.setCursor(0, 0);
                lcd.print("WiFi GAGAL!     ");
                lcd.setCursor(0, 1);
                lcd.print("Buat Hotspot HP!");
                displayState = 1;
                delay(2500);
            } else {
                lcd.setCursor(0, 0);
                lcd.print("SSID:");
                lcd.print(fixedSSID);
                lcd.setCursor(0, 1);
                lcd.print("Pass:");
                lcd.print(fixedPASS);
                displayState = 0;
                delay(2500);
            }
        }

        delay(100);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hotspot Found!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());

    Serial.println("\nAkhirnya Terhubung!");
    delay(2000);

    mqtt.setCallback(mqttCallback);
  }

  lastReadMillis = millis();
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
     if (!mqtt.connected() && String(fixedServer) != "") mqttReconnect();
     if (mqtt.connected()) mqtt.loop();
  }

  if (now - lastReadMillis >= READ_INTERVAL_MS) {
    lastReadMillis = now;

    if (WiFi.status() != WL_CONNECTED) {
      lcd.setCursor(0, 0);
      lcd.print("WiFi DISCONNECT ");
      updateLcdRelayState();
      return;
    }

    if (pzem.readAll(&v, &i, &p, &e_total, &f, &pf)) {

      if(isnan(v)) v = 0;
      if(isnan(p)) p = 0;
      if(isnan(e_total)) e_total = 0;

      if (isFirstRun) {
         if (e_total > 0) startKwh = e_total;
         isFirstRun = false;
      }

      e_session = e_total - startKwh;

      if (e_session < 0) {
         startKwh = 0;
         e_session = e_total;
      }

      lcd.setCursor(0, 0);
      lcd.printf("%4.0fW | %.3fkWh", p, e_session);

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
        lcd.setCursor(15, 0);
        lcd.print(ok ? "*" : "X");
      }
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Sensor Error!   ");
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') resetEnergySession();
  }
}
