// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// ====================================================================== //
// * - Tiny Room Sensor (XIAO ESP32-C6) - Headless
// * - Matter over Thread (Temp + Humidity via SHT41)
// * - Commissioning info via Serial
// * - Hold Boot Button for ~5s to decommission
// * - Optimized for battery power and low current draw
// ====================================================================== // 

#include <Matter.h>
#include <MatterEndPoint.h>
#include "esp_pm.h"
#include <Wire.h>
#include <SHT4xMinimal.h>
#include <MatterEndpoints/MatterTemperatureSensorBattery.h>
#include "esp_openthread.h"
#include <openthread/link.h>
#include <Preferences.h>

#if !CONFIG_ENABLE_CHIPOBLE
  #include <WiFi.h>
#endif

// ------------ Board Pin Defs -----------------
#define SDA_PIN 22
#define SCL_PIN 23
#define VBAT_ADC_PIN A0   // GPIO0 (XIAO ESP32-C6 A0)
#define LED_PIN 15
const uint8_t BUTTON_PIN = BOOT_PIN;
//Forward Declarations
static void blink(uint8_t n);

/* ========= ESP Sleep Helpers ========= */
static esp_pm_lock_handle_t s_no_ls_lock = nullptr;

static constexpr uint32_t SLEEP_SECONDS = 120;           // Normal refresh interval
static constexpr uint32_t DEBUG_UPDATE_MS = 5000;        // Debug refresh interval
static constexpr uint32_t COMMISSION_GRACE_MS = 15000;   // Allow time for initial networking
static constexpr uint32_t ICD_POLL_PERIOD_MS = 60000;    // Thread SED poll interval
static constexpr uint32_t THREAD_RX_ON_POLL_MS = 1000;   // Fast poll when not sleepy

/* =========== Debug Mode Switch ======== */
Preferences prefs;
bool DEBUG_MODE = false;

static bool pressed = false;
static bool longpress_fired = false;
static uint32_t press_ts = 0;

static constexpr uint32_t DECOMMISSION_HOLD_MS = 5000;
static constexpr uint32_t DEBOUNCE_MS = 60;
static uint32_t last_toggle_ms = 0;
static constexpr uint32_t TOGGLE_COOLDOWN_MS = 300;

#define VPRINT(...)   do { if (DEBUG_MODE) Serial.print(__VA_ARGS__); } while (0)
#define VPRINTLN(...) do { if (DEBUG_MODE) Serial.println(__VA_ARGS__); } while (0)
#define VPRINTF(...) do { if (DEBUG_MODE) Serial.printf(__VA_ARGS__); } while (0)

// ----------- Matter Endpoints -------------
MatterTemperatureSensorBattery TempSensor;      // °C (Matter spec)
MatterHumiditySensor    HumiditySensor;  // %RH

// CONFIG_ENABLE_CHIPOBLE is enabled when BLE is used to commission the Matter Network
#if !CONFIG_ENABLE_CHIPOBLE
// WiFi is manually set and started
const char *ssid = "your-ssid";          // Change this to your WiFi SSID
const char *password = "your-password";  // Change this to your WiFi password
#endif

// ---------- SHT41 Init ----------
SHT4xMinimal sht4;
bool sht_ready = false;

// ---------- Cached readings ----------
float g_lastTempC = 50.0f;
float g_lastRH    = 99.0f;

// ---------- Unit Conversion ----------
static inline float C_to_F(float c) { return (c * 9.0f / 5.0f) + 32.0f; }

// ---------- LED Blink Helper ---------
static inline void ledOn()  { digitalWrite(LED_PIN, LOW); }
static inline void ledOff() { digitalWrite(LED_PIN, HIGH);  }

static void blink(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    ledOn(); 
    delay(120);
    ledOff();
    delay(120);
  }
}

static void configureThreadIcdMode(bool debug) {
  otInstance *ot = esp_openthread_get_instance();
  if (!ot) {
    VPRINTLN("OpenThread instance not ready; ICD config skipped.");
    return;
  }

  const bool rx_on_when_idle = debug;
  otLinkSetRxOnWhenIdle(ot, rx_on_when_idle);

  const uint32_t poll_ms = debug ? THREAD_RX_ON_POLL_MS : ICD_POLL_PERIOD_MS;
  otLinkSetPollPeriod(ot, poll_ms);

  VPRINTF("Thread ICD: rx_on_when_idle=%s, poll=%lu ms\r\n",
          rx_on_when_idle ? "true" : "false",
          static_cast<unsigned long>(poll_ms));
}

static void applyPowerPolicy(bool commissioned) {
  const bool allow_sleep = commissioned && !DEBUG_MODE;
  if (s_no_ls_lock) {
    if (allow_sleep) {
      esp_pm_lock_release(s_no_ls_lock);
    } else {
      esp_pm_lock_acquire(s_no_ls_lock);
    }
  }
  configureThreadIcdMode(!allow_sleep);
}
// ---------- DEBUG MODE HELPER --------
static void loadDebugMode() {
  prefs.begin("tinyenv", false);
  DEBUG_MODE = prefs.getBool("debug", false);
  prefs.end();
}

static void saveDebugMode() {
  prefs.begin("tinyenv", false);
  prefs.putBool("debug", DEBUG_MODE);
  prefs.end();
}

// ----------- BOOT BUTTON HANDLER -----------
static void handleBootButton() {
  const uint32_t now = millis();
  const bool btn_low = (digitalRead(BUTTON_PIN) == LOW);

  // Press edge
  if (btn_low && !pressed) {
    pressed = true;
    press_ts = now;
    longpress_fired = false;
  }

  // Long press action (fire once)
  if (pressed && btn_low && !longpress_fired && (now - press_ts >= DECOMMISSION_HOLD_MS)) {
    longpress_fired = true;

    Serial.println("Decommissioning Matter node...");
    Matter.decommission();

    blink(3);
  }

  // Release edge
  if (pressed && !btn_low) {
    const uint32_t held_ms = now - press_ts;
    pressed = false;

    // Short press action (only if long press didn't fire)
    if (!longpress_fired &&
        held_ms >= DEBOUNCE_MS &&
        (now - last_toggle_ms) > TOGGLE_COOLDOWN_MS) {

      last_toggle_ms = now;
      DEBUG_MODE = !DEBUG_MODE;
      saveDebugMode();
      applyPowerPolicy(Matter.isDeviceCommissioned());

      blink(DEBUG_MODE ? 2 : 1);
      Serial.print("DEBUG_MODE: ");
      Serial.println(DEBUG_MODE ? "ON (no sleep + verbose)" : "OFF (sleep + quiet)");
    }
  }
}
// ---------- Battery (A0 via 1:2 divider) ----------
static constexpr float VBAT_GAIN = 1.0123f;  // <-- Battery sense conversion factor (1.0123 for my XIAO ESP32-C6 w/ 220,000u resistors)

static inline float readBatteryVoltsA0() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) {
    uint32_t s = analogReadMilliVolts(VBAT_ADC_PIN);
    mv += s;
    delay(2);
  }
  float pin_mv = (mv / 16.0f);
  float vA0    = pin_mv / 1000.0f;            // volts at VBAT_ADC_PIN
  float vbat   = (vA0 * 2.0f) * VBAT_GAIN;    // undo 1:2 divider, then apply calibration
  VPRINTF("VBAT_ADC pin avg: %.1f mV | v_pin=%.3f V | vbat=%.3f V\r\n",
          pin_mv, vA0, vbat);  return vbat;
}

static inline uint8_t voltsToPct(float v) {
  if (v >= 4.10f) return 100;   // Values above 4.10V registers as 100%
  if (v <= 3.00f) return 0;     // Values below 3.0 register as 0%

  // 3.90–4.10 -> 80–100  (ΔV=0.20, Δ%=20, slope=100 %/V)
  if (v >= 3.90f) return (uint8_t)lroundf(80.0f + (v - 3.90f) * 100.0f);

  // 3.60–3.90 -> 40–80   (ΔV=0.30, Δ%=40, slope=133.333... %/V)
  if (v >= 3.60f) return (uint8_t)lroundf(40.0f + (v - 3.60f) * (40.0f / 0.30f));

  // 3.30–3.60 -> 10–40   (ΔV=0.30, Δ%=30, slope=100 %/V)
  if (v >= 3.30f) return (uint8_t)lroundf(10.0f + (v - 3.30f) * (30.0f / 0.30f));

  // 3.00–3.30 -> 0–10    (ΔV=0.30, Δ%=10, slope=33.333... %/V)
  return (uint8_t)lroundf((v - 3.00f) * (10.0f / 0.30f));
}

// ---------- SHT41 ----------
void sensors_init() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // conservative for bring-up

  if (sht4.begin(Wire)) {
    sht_ready = true;
    Serial.println("SHT4x detected");
  } else {
    Serial.println("SHT4x not found on I2C!");
  }
}

bool read_sht41(float &tempC, float &rh) {
  if (!sht_ready) return false;

  return sht4.read(tempC, rh);
}

// -------- Sensor Update Function---------
static uint32_t g_update_count = 0;
static uint32_t g_fail_count = 0;
static uint32_t g_last_update_ms = 0;
static uint32_t g_last_update_duration_ms = 0;

static void sensorUpdate() {
  const int64_t start_us = esp_timer_get_time();
  ledOn();
  float tC, rh;

  if (read_sht41(tC, rh)) {
    g_update_count++;
    g_lastTempC = tC;
    g_lastRH    = rh;
    float vbat   = readBatteryVoltsA0();
    uint8_t bpct = voltsToPct(vbat);
    TempSensor.setTemperature(tC);
    uint32_t mv_to_matter = (uint32_t) lroundf(vbat * 1000.0f);
    TempSensor.setBatteryVoltageMv(mv_to_matter);
    TempSensor.setBatteryPercent(bpct);
    HumiditySensor.setHumidity(rh);

    ledOff();

    VPRINT("Sensor update: ");
    VPRINT(C_to_F(tC), 1);
    VPRINT(" F, ");
    VPRINT(rh, 0);
    VPRINT(" %RH, ");
    VPRINT(" VBAT: ");
    VPRINT(vbat, 3);
    VPRINT("V (");
    VPRINT(bpct);
    VPRINTLN("%)");
  } else {
    g_fail_count++;
    VPRINTLN("SHT41 sensor read failed.");
  }

  g_last_update_ms = millis();
  g_last_update_duration_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
  VPRINTF("Update #%lu, duration=%lums, fail=%lu\r\n",
          (unsigned long)g_update_count,
          (unsigned long)g_last_update_duration_ms,
          (unsigned long)g_fail_count);
}

// ==================================================
// ==================== SETUP =======================
// ==================================================

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  ledOff();

  Serial.begin(115200);
  Serial.println("\nTiny Room Sensor (headless, Matter + SHT41)");
  delay(200);

  loadDebugMode();
  blink(DEBUG_MODE ? 2 : 1);   // 2 blinks = debug on, 1 blink = normal

  //Battery reading init
  pinMode(VBAT_ADC_PIN, ANALOG);
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);

  sensors_init();

  // Matter endpoints
  TempSensor.begin(g_lastTempC);   // °C
  HumiditySensor.begin(g_lastRH);
  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_ls", &s_no_ls_lock);

  Matter.begin();
  applyPowerPolicy(Matter.isDeviceCommissioned());
  
  // Commission if needed (info via Serial)
  if (!Matter.isDeviceCommissioned()) {
    String manualCode = Matter.getManualPairingCode().c_str();
    String qrUrl      = Matter.getOnboardingQRCodeUrl().c_str();

    Serial.println("--------------------------------------------------");
    Serial.println("Device not commissioned yet.");
    Serial.print("Manual pairing code: ");
    Serial.println(manualCode);
    Serial.print("QR code URL: ");
    Serial.println(qrUrl);
    Serial.println("Use your Matter controller to commission this device.");
    Serial.println("--------------------------------------------------");
  } else {
    Serial.println("Device already commissioned.");
  }

  /* ==== Power Saving Features Enable ==== */
  setCpuFrequencyMhz(80);     // Reduce CPU freq (optional but usually helpful)

  esp_pm_config_t pm = {      // Enable dynamic freq scaling + automatic light sleep when idle
    .max_freq_mhz = 80,
    .min_freq_mhz = 40,
    .light_sleep_enable = true
  };
  esp_pm_configure(&pm);

  /* ======== Initial Power-on sensor update =====*/
  sensorUpdate(); // 

}

// ===================================================
// ===================== LOOP ========================
// ===================================================

void loop() {

  handleBootButton();

  static bool s_commissioned = false;
  static uint32_t s_commissioned_at_ms = 0;
  static uint32_t s_next_sample_ms = 0;

  const uint32_t now = millis();
  const bool commissioned = Matter.isDeviceCommissioned();

  if (commissioned != s_commissioned) {
    s_commissioned = commissioned;
    s_commissioned_at_ms = commissioned ? now : 0;
    s_next_sample_ms = 0;
    applyPowerPolicy(commissioned);
  }

  if (!commissioned) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  // Commissioning grace window
  if (now - s_commissioned_at_ms < COMMISSION_GRACE_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  const uint32_t period_ms = DEBUG_MODE ? DEBUG_UPDATE_MS : (SLEEP_SECONDS * 1000);
  if (s_next_sample_ms == 0 || (int32_t)(now - s_next_sample_ms) >= 0) {
    sensorUpdate();
    s_next_sample_ms = now + period_ms;
  }

  const uint32_t sleep_ms = (s_next_sample_ms > now) ? (s_next_sample_ms - now) : period_ms;
  vTaskDelay(pdMS_TO_TICKS(sleep_ms));
}
