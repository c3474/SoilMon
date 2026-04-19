// ====================================================================== //
// Garden Sensor (XIAO ESP32-C6) — Matter over Thread
//
// Sensors:
//   - BME280:           Air temperature, humidity, pressure (I2C)
//   - DS18B20:          Soil temperature (1-Wire, GPIO2)
//   - Capacitive probe: Soil moisture (ADC, GPIO3)
//
// Power:
//   - 18650 + solar via TP4056
//   - MOSFET gate (GPIO4) cuts power to soil sensor + DS18B20 between reads
//   - ICD LIT sleepy end device; 120s poll interval
//
// Matter endpoints:
//   1 - Temperature Measurement  (air,  BME280)
//   2 - Relative Humidity        (air,  BME280)
//   3 - Pressure Measurement     (air,  BME280)
//   4 - Temperature Measurement  (soil, DS18B20)   "Soil Temperature"
//   5 - Relative Humidity        (soil moisture, capacitive) "Soil Moisture"
//   + Power Source cluster (battery) on endpoint 1
//
// Commissioning: BLE for 15 min after power-on; pairing code via Serial.
// Boot button:   short press = toggle verbose, long press (5s) = decommission.
// ====================================================================== //

#include <Matter.h>
#include <MatterEndPoint.h>
#include <MatterEndpoints/MatterPressureSensor.h>
#include <MatterEndpoints/MatterTemperatureSensor.h>
#include <MatterEndpoints/MatterHumiditySensor.h>
#include "MatterEndpoints/MatterTemperatureSensorBattery.h"
#include "esp_pm.h"
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include "esp_openthread.h"
#include <openthread/link.h>
#include <openthread/thread.h>
#include <Preferences.h>
#include <cstring>
#include <esp_matter_attribute_utils.h>
#include <lib/support/logging/CHIPLogging.h>
#include "esp_adc/adc_oneshot.h"

#if !CONFIG_ENABLE_CHIPOBLE
  #include <WiFi.h>
#endif

// ==================== Pin Definitions ====================
#define SDA_PIN          0      // BME280 SDA
#define SCL_PIN          1      // BME280 SCL
#define ONE_WIRE_PIN     2      // DS18B20 data + 4.7kΩ pull-up to 3.3V
#define SOIL_ADC_PIN     3      // Capacitive soil sensor AOUT
#define SENSOR_PWR_PIN   4      // MOSFET gate: HIGH = sensors powered
#define VBAT_ADC_PIN     A0     // Battery voltage (1:2 divider)
#define LED_PIN          15
const uint8_t BUTTON_PIN = BOOT_PIN;

// ==================== Soil Moisture Calibration ====================
// Adjust after calibration with your specific sensor and soil.
static const int SOIL_DRY_RAW = 2800;   // ADC reading in dry air
static const int SOIL_WET_RAW = 1200;   // ADC reading fully submerged

// ==================== Timing ====================
static constexpr uint32_t SLEEP_SECONDS        = 120;
static constexpr uint32_t DEBUG_UPDATE_MS      = 5000;
static constexpr uint32_t COMMISSION_GRACE_MS  = 60000;
static constexpr uint32_t ICD_POLL_PERIOD_MS   = 60000;
static constexpr uint32_t THREAD_RX_ON_POLL_MS = 1000;
static constexpr uint32_t MOSFET_STABILISE_MS  = 50;    // wait after powering sensors
static constexpr uint32_t DS18B20_CONVERT_MS   = 750;   // 12-bit conversion time

// ==================== ICD cluster IDs ====================
static constexpr uint32_t ICD_MGMT_CLUSTER_ID  = 0x00000046;
static constexpr uint32_t ICD_ATTR_UAT_HINT_ID = 0x00000006;
static constexpr uint32_t ICD_ATTR_UAT_INSTR_ID= 0x00000007;

// ==================== Matter Endpoints ====================
MatterTemperatureSensorBattery AirTempSensor;   // air temp + battery cluster
MatterHumiditySensor           AirHumSensor;    // air humidity
MatterPressureSensor           AirPresSensor;   // air pressure
MatterTemperatureSensor        SoilTempSensor;  // soil temperature (DS18B20)
MatterHumiditySensor           SoilMoiSensor;   // soil moisture (capacitive)

// ==================== Sensors ====================
Adafruit_BME280 bme;
bool bme_ready = false;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
bool ds_ready = false;

// ==================== Cached readings ====================
float g_airTempC  = 25.0f;
float g_airRH     = 50.0f;
float g_airHPa    = 1013.0f;
float g_soilTempC = 20.0f;
float g_soilMoi   = 50.0f;   // 0–100 %

// ==================== Verbose / power state ====================
Preferences prefs;
bool VERBOSE_PRINTS = false;
static bool s_commissioned    = false;
static uint32_t s_commissioned_at_ms = 0;

static bool     pressed         = false;
static bool     longpress_fired = false;
static uint32_t press_ts        = 0;
static uint32_t last_toggle_ms  = 0;

static constexpr uint32_t DECOMMISSION_HOLD_MS = 5000;
static constexpr uint32_t DEBOUNCE_MS          = 60;
static constexpr uint32_t TOGGLE_COOLDOWN_MS   = 300;

#define VPRINT(...)   do { if (VERBOSE_PRINTS) Serial.print(__VA_ARGS__); } while (0)
#define VPRINTLN(...) do { if (VERBOSE_PRINTS) Serial.println(__VA_ARGS__); } while (0)
#define VPRINTF(...) do { if (VERBOSE_PRINTS) Serial.printf(__VA_ARGS__); } while (0)

// ==================== LED ====================
static inline void ledOn()  { digitalWrite(LED_PIN, LOW); }
static inline void ledOff() { digitalWrite(LED_PIN, HIGH); }
static void blink(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    ledOn();  delay(120);
    ledOff(); delay(120);
  }
}

// ==================== ICD / Thread helpers ====================
static esp_pm_lock_handle_t s_no_ls_lock = nullptr;

static void configureThreadIcdMode(bool debug) {
  otInstance *ot = esp_openthread_get_instance();
  if (!ot) { VPRINTLN("OpenThread instance not ready; ICD config skipped."); return; }
  if (debug) {
    otLinkSetRxOnWhenIdle(ot, true);
    otLinkSetPollPeriod(ot, THREAD_RX_ON_POLL_MS);
  } else {
    otLinkSetRxOnWhenIdle(ot, false);
#ifdef CONFIG_ICD_SLOW_POLL_INTERVAL_MS
    otLinkSetPollPeriod(ot, CONFIG_ICD_SLOW_POLL_INTERVAL_MS);
#endif
  }
}

static void configureChipLogging(bool verbose) {
  chip::Logging::SetLogFilter(verbose ? chip::Logging::kLogCategory_Detail
                                      : chip::Logging::kLogCategory_Error);
}

static void configureIcdManagementAttributes() {
#if CONFIG_ENABLE_ICD_SERVER && CONFIG_ENABLE_ICD_USER_ACTIVE_MODE_TRIGGER
  const uint32_t uat_hint = (1u << 0) | (1u << 8);
  esp_matter_attr_val_t hint = esp_matter_bitmap32(uat_hint);
  esp_matter::attribute::update(0, ICD_MGMT_CLUSTER_ID, ICD_ATTR_UAT_HINT_ID, &hint);
  static char instruction[] = "Press BOOT to wake";
  esp_matter_attr_val_t instr = esp_matter_char_str(
      instruction, static_cast<uint16_t>(strlen(instruction)));
  esp_matter::attribute::update(0, ICD_MGMT_CLUSTER_ID, ICD_ATTR_UAT_INSTR_ID, &instr);
#endif
}

static void applyPowerPolicy(bool commissioned) {
  const uint32_t now = millis();
  const bool past_grace  = commissioned && (now - s_commissioned_at_ms >= COMMISSION_GRACE_MS);
  const bool allow_sleep = commissioned && !VERBOSE_PRINTS && past_grace;
  if (s_no_ls_lock) {
    if (allow_sleep) esp_pm_lock_release(s_no_ls_lock);
    else             esp_pm_lock_acquire(s_no_ls_lock);
  }
  configureThreadIcdMode(!allow_sleep);
  configureChipLogging(VERBOSE_PRINTS);
}

// ==================== Verbose / Prefs ====================
static void loadDebugMode() {
  prefs.begin("garden", false);
  VERBOSE_PRINTS = prefs.getBool("debug", false);
  prefs.end();
}
static void saveDebugMode() {
  prefs.begin("garden", false);
  prefs.putBool("debug", VERBOSE_PRINTS);
  prefs.end();
}

// ==================== Boot button ====================
static void handleBootButton() {
  const uint32_t now = millis();
  const bool btn_low = (digitalRead(BUTTON_PIN) == LOW);

  if (btn_low && !pressed) {
    pressed = true; press_ts = now; longpress_fired = false;
  }
  if (pressed && btn_low && !longpress_fired && (now - press_ts >= DECOMMISSION_HOLD_MS)) {
    longpress_fired = true;
    Serial.println("Decommissioning Matter node...");
    Matter.decommission();
    blink(3);
  }
  if (pressed && !btn_low) {
    const uint32_t held_ms = now - press_ts;
    pressed = false;
    if (!longpress_fired && held_ms >= DEBOUNCE_MS && (now - last_toggle_ms) > TOGGLE_COOLDOWN_MS) {
      last_toggle_ms = now;
      VERBOSE_PRINTS = !VERBOSE_PRINTS;
      saveDebugMode();
      applyPowerPolicy(Matter.isDeviceCommissioned());
      blink(VERBOSE_PRINTS ? 2 : 1);
    }
  }
}

// ==================== Battery ====================
static constexpr float VBAT_GAIN = 1.0123f;

static float readBatteryVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) { mv += analogReadMilliVolts(VBAT_ADC_PIN); delay(2); }
  float pin_mv = mv / 16.0f;
  float vbat   = (pin_mv / 1000.0f * 2.0f) * VBAT_GAIN;
  VPRINTF("VBAT: %.1f mV pin → %.3f V\r\n", pin_mv, vbat);
  return vbat;
}

static uint8_t voltsToPct(float v) {
  if (v >= 4.14f) return 100;
  if (v <= 3.00f) return 0;
  struct Vp { float v; uint8_t p; };
  static const Vp kCurve[] = {
    {4.14f,100},{4.10f,95},{4.00f,85},{3.90f,75},{3.80f,65},
    {3.70f,50}, {3.60f,35},{3.50f,20},{3.40f,10},{3.30f, 5},
    {3.20f, 2}, {3.00f, 0},
  };
  for (size_t i = 0; i + 1 < sizeof(kCurve)/sizeof(kCurve[0]); ++i) {
    if (v <= kCurve[i].v && v >= kCurve[i+1].v) {
      float t = (v - kCurve[i+1].v) / (kCurve[i].v - kCurve[i+1].v);
      return (uint8_t)lroundf(kCurve[i+1].p + t * (kCurve[i].p - kCurve[i+1].p));
    }
  }
  return 0;
}

// ==================== MOSFET power gate ====================
static void sensorPowerOn() {
  digitalWrite(SENSOR_PWR_PIN, HIGH);
  delay(MOSFET_STABILISE_MS);
}
static void sensorPowerOff() {
  digitalWrite(SENSOR_PWR_PIN, LOW);
}

// ==================== Sensor init ====================
static void sensors_init() {
  // BME280 — always powered via 3.3V rail, not gated
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  if (bme.begin(0x76, &Wire)) {
    bme_ready = true;
    Serial.println("BME280 detected (0x76)");
  } else if (bme.begin(0x77, &Wire)) {
    bme_ready = true;
    Serial.println("BME280 detected (0x77)");
  } else {
    Serial.println("BME280 not found on I2C! Check wiring and 3.3V variant.");
  }

  // DS18B20 — power via MOSFET; just configure the library here
  ds18b20.begin();
  ds18b20.setResolution(12);      // 12-bit = 0.0625°C resolution, ~750ms conversion
  ds18b20.setWaitForConversion(false);  // we manage timing manually
  if (ds18b20.getDeviceCount() > 0) {
    ds_ready = true;
    Serial.printf("DS18B20: %d device(s) found\n", ds18b20.getDeviceCount());
  } else {
    Serial.println("DS18B20 not found on 1-Wire! Check wiring and pull-up.");
  }
}

// ==================== Soil moisture ADC ====================
// Returns 0–100 %, where 100 = fully wet and 0 = dry air.
// The capacitive sensor outputs HIGH voltage when dry, LOW when wet.
static float readSoilMoisturePct() {
  int32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(SOIL_ADC_PIN);
    delay(5);
  }
  int raw = (int)(sum / 8);
  VPRINTF("Soil ADC raw avg: %d\r\n", raw);

  // Clamp to calibration range, then invert (dry = high ADC = 0%)
  raw = constrain(raw, SOIL_WET_RAW, SOIL_DRY_RAW);
  float pct = 100.0f * (float)(SOIL_DRY_RAW - raw) / (float)(SOIL_DRY_RAW - SOIL_WET_RAW);
  return pct;
}

// ==================== Sensor read + Matter update ====================
static uint32_t g_update_count = 0;
static uint32_t g_fail_count   = 0;

static void sensorUpdate() {
  ledOn();

  // --- BME280 (always powered) ---
  if (bme_ready) {
    g_airTempC = bme.readTemperature();
    g_airRH    = bme.readHumidity();
    g_airHPa   = bme.readPressure() / 100.0f;   // Pa → hPa
    VPRINTF("BME280: %.2f°C  %.1f%%RH  %.1f hPa\r\n", g_airTempC, g_airRH, g_airHPa);
  }

  // --- Gate power to DS18B20 + soil sensor ---
  sensorPowerOn();

  // Kick off DS18B20 conversion immediately after power-on stabilisation
  if (ds_ready) {
    ds18b20.requestTemperatures();
  }

  // Read soil moisture ADC while DS18B20 is converting (~750ms)
  g_soilMoi = readSoilMoisturePct();
  VPRINTF("Soil moisture: %.1f%%\r\n", g_soilMoi);

  // Wait for DS18B20 conversion to complete (requestTemperatures is async)
  delay(DS18B20_CONVERT_MS);

  if (ds_ready) {
    float t = ds18b20.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) {
      g_soilTempC = t;
      VPRINTF("Soil temp: %.2f°C\r\n", g_soilTempC);
    } else {
      g_fail_count++;
      VPRINTLN("DS18B20 read failed (DEVICE_DISCONNECTED)");
    }
  }

  sensorPowerOff();

  // --- Battery ---
  float   vbat = readBatteryVolts();
  uint8_t bpct = voltsToPct(vbat);

  // --- Push to Matter ---
  AirTempSensor.setTemperature(g_airTempC);
  AirTempSensor.setBatteryVoltageMv((uint32_t)lroundf(vbat * 1000.0f));
  AirTempSensor.setBatteryPercent(bpct);
  AirHumSensor.setHumidity(g_airRH);
  AirPresSensor.setPressure(g_airHPa);
  SoilTempSensor.setTemperature(g_soilTempC);
  // Matter humidity units: 0–10000 (hundredths of %)
  SoilMoiSensor.setHumidity(g_soilMoi);

  ledOff();
  g_update_count++;

  VPRINTF("Update #%lu  air=%.1f°C %.0f%%RH %.0fhPa  soil=%.1f°C %.0f%%  bat=%.3fV(%u%%)\r\n",
          (unsigned long)g_update_count,
          g_airTempC, g_airRH, g_airHPa,
          g_soilTempC, g_soilMoi,
          vbat, bpct);
}

// ==================================================
// ==================== SETUP =======================
// ==================================================

void setup() {
  pinMode(BUTTON_PIN,     INPUT_PULLUP);
  pinMode(LED_PIN,        OUTPUT);
  pinMode(SENSOR_PWR_PIN, OUTPUT);
  ledOff();
  sensorPowerOff();  // sensors off until first read

  Serial.begin(115200);
  Serial.println("\nGarden Sensor (BME280 + DS18B20 + Soil Moisture — Matter/Thread)");
  delay(200);

  loadDebugMode();
  blink(VERBOSE_PRINTS ? 2 : 1);

  // Battery ADC init
  pinMode(VBAT_ADC_PIN, ANALOG);
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);
  (void)analogReadMilliVolts(VBAT_ADC_PIN);

  // Soil ADC — GPIO3 is ADC1_CH3 on ESP32-C6
  pinMode(SOIL_ADC_PIN, ANALOG);
  analogSetPinAttenuation(SOIL_ADC_PIN, ADC_11db);

  sensors_init();

  // Matter endpoints
  AirTempSensor.begin(g_airTempC);
  AirHumSensor.begin(g_airRH);
  AirPresSensor.begin(g_airHPa);
  SoilTempSensor.begin(g_soilTempC);
  SoilMoiSensor.begin(g_soilMoi);

  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_ls", &s_no_ls_lock);

  Matter.begin();
  const bool commissioned = Matter.isDeviceCommissioned();
  s_commissioned        = commissioned;
  s_commissioned_at_ms  = commissioned ? millis() : 0;
  configureChipLogging(VERBOSE_PRINTS);
  configureIcdManagementAttributes();
  applyPowerPolicy(commissioned);

  if (!commissioned) {
    Serial.println("--------------------------------------------------");
    Serial.println("Device not commissioned yet.");
    Serial.print("Manual pairing code: ");
    Serial.println(Matter.getManualPairingCode().c_str());
    Serial.print("QR code URL: ");
    Serial.println(Matter.getOnboardingQRCodeUrl().c_str());
    Serial.println("Use Apple Home → Add Accessory → More Options.");
    Serial.println("--------------------------------------------------");
  } else {
    Serial.println("Device already commissioned.");
  }

  setCpuFrequencyMhz(80);
  esp_pm_config_t pm = {
    .max_freq_mhz      = 80,
    .min_freq_mhz      = 40,
    .light_sleep_enable = true,
  };
  esp_pm_configure(&pm);

  sensorUpdate();   // initial reading on boot
}

// ===================================================
// ===================== LOOP ========================
// ===================================================

void loop() {
  handleBootButton();

  static uint32_t s_next_sample_ms = 0;
  static bool     s_grace_done     = false;
  const  uint32_t now              = millis();
  const  bool     commissioned     = Matter.isDeviceCommissioned();

  if (commissioned != s_commissioned) {
    s_commissioned       = commissioned;
    s_commissioned_at_ms = commissioned ? now : 0;
    s_next_sample_ms     = 0;
    s_grace_done         = false;
    applyPowerPolicy(commissioned);
  }

  if (commissioned && !s_grace_done &&
      (now - s_commissioned_at_ms >= COMMISSION_GRACE_MS)) {
    s_grace_done = true;
    applyPowerPolicy(commissioned);
  }

  if (!commissioned) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  if (now - s_commissioned_at_ms < COMMISSION_GRACE_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  const uint32_t period_ms = VERBOSE_PRINTS ? DEBUG_UPDATE_MS : (SLEEP_SECONDS * 1000UL);
  if (s_next_sample_ms == 0 || (int32_t)(now - s_next_sample_ms) >= 0) {
    sensorUpdate();
    s_next_sample_ms = now + period_ms;
  }

  const uint32_t sleep_ms = (s_next_sample_ms > now) ? (s_next_sample_ms - now) : period_ms;
  vTaskDelay(pdMS_TO_TICKS(sleep_ms));
}
