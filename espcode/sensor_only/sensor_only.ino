#define PZEM_004T
#include <PZEMPlus.h>
#include <Preferences.h>

#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEM_SERIAL(2);
PZEMPlus pzem(PZEM_SERIAL, PZEM_RX, PZEM_TX);

Preferences prefs;

float localEnergyWh = 0.0;
unsigned long lastSaveMillis = 0;
unsigned long lastReadMillis = 0;
unsigned long lastLoopMillis = 0;
unsigned long lastResetMillis = 0;

const unsigned long SAVE_INTERVAL_MS  = 60UL * 1000UL; // 60 detik
const unsigned long READ_INTERVAL_MS  = 1000UL;        // 1 detik baca PZEM
const unsigned long RESET_INTERVAL_MS = 5000UL;        // 2 detik reset energy
const float POWER_THRESHOLD_W = 0.3;

void setup() {
  Serial.begin(115200);
  delay(100);

  pzem.begin();

  prefs.begin("pzem", false);
  localEnergyWh = prefs.getFloat("localEnergy", NAN);

  if (isnan(localEnergyWh)) {
    float devEnergy = pzem.readEnergy();
    if (!isnan(devEnergy)) localEnergyWh = devEnergy;
    else localEnergyWh = 0.0;
  }

  lastSaveMillis = millis();
  lastLoopMillis = millis();
  lastResetMillis = millis();

  Serial.printf("Starting localEnergyWh = %.3f Wh\n", localEnergyWh);
}

void loop() {
  unsigned long now = millis();

  // =======================
  //   BACA PZEM SETIAP 1s
  // =======================
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

      // Integrasi Wh
      unsigned long dt_ms = now - lastLoopMillis;
      lastLoopMillis = now;
      float dt_s = dt_ms / 1000.0f;

      float powerForIntegration = (p > POWER_THRESHOLD_W) ? p : 0.0f;

      float deltaWh = powerForIntegration * (dt_s / 3600.0f);
      localEnergyWh += deltaWh;

      Serial.printf("DeltaWh: %.6f Wh, LocalEnergy: %.6f Wh\n",
                    deltaWh, localEnergyWh);

    } else {
      Serial.println("⚠️ Failed to read PZEM!");
    }
  }

  // =======================
  //  RESET ENERGY SETIAP 2s
  // =======================
  if (now - lastResetMillis >= RESET_INTERVAL_MS) {
    lastResetMillis = now;

    Serial.println("⚠️ Reset localEnergyWh (interval 2 detik)!");

    localEnergyWh = 0.0;

    // Hapus dari Preferences juga
    prefs.putFloat("localEnergy", 0.0);

    Serial.println("⚠️ localEnergyWh berhasil di-reset ke 0");
  }

  // =======================
  //  SIMPAN PERIODIK (60s)
  // =======================
  if (now - lastSaveMillis >= SAVE_INTERVAL_MS) {
    lastSaveMillis = now;
    prefs.putFloat("localEnergy", localEnergyWh);
    Serial.printf("Saved localEnergy = %.6f Wh\n", localEnergyWh);
  }
}
