#define PZEM_004T
#include <PZEMPlus.h>

#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

unsigned long lastRead = 0;
unsigned long lastCalc = 0;

const unsigned long READ_INTERVAL_MS = 1000;      // baca per detik
const float POWER_THRESHOLD_W = 0.3;              // filter noise (data empiris)

void setup() {
  Serial.begin(115200);
  delay(100);
  pzem.begin();

  lastCalc = millis();
}

void loop() {
  unsigned long now = millis();

  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;

    float v, i, p, e, f, pf;

    // Baca Sensor
    if (pzem.readAll(&v, &i, &p, &e, &f, &pf)) {

      // HITUNG DELTAKWh
      unsigned long dt_ms = now - lastCalc;
      lastCalc = now;
      float dt_s = dt_ms / 1000.0;

      float P_use = (p > POWER_THRESHOLD_W) ? p : 0.0; // HILANGKAN NOISE

      float deltaWh = P_use * (dt_s / 3600.0); // Wh
      float deltaKWh = deltaWh / 1000.0;       // kWh

      // kirim hanya data penting + delta kWh
      Serial.println("=== Data ===");
      Serial.printf("Voltage  : %.1f V\n", v);
      Serial.printf("Current  : %.3f A\n", i);
      Serial.printf("Power    : %.3f W\n", p);
      Serial.printf("delta kWh: %.6f kWh\n", deltaKWh);
      Serial.println("===========");
    }
    else {
      Serial.println("⚠️ Failed to read PZEM!");
    }
  }
}
