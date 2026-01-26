# TinyENV Sensor – Matter over Thread (ESP32‑C6)

A headless, battery‑powered temperature & humidity sensor built on **Seeed XIAO ESP32‑C6**, using **Matter over Thread**. Works with **Home Assistant** and **Apple Home**.

This project is intentionally minimal: no screen, no buttons beyond BOOT for decommissioning, and no OTA (for now). The goal is a stable, low‑power environmental sensor using a modern Matter/Thread stack.

---

## Hardware

- **MCU**: Seeed XIAO ESP32‑C6
- **Sensor**: Sensirion SHT41 (I²C)
- **Power**: Supports Battery or USB
- **Battery sense**: A0 via 1:2 divider (220,000Ω, offset calibrated in firmware to 1.0123)

Xiao ESP32-C6 board-specific pin definitions:

```cpp
#define SDA_PIN 22
#define SCL_PIN 23
#define VBAT_ADC_PIN A0
```

---

## Software Stack

- **ESP‑IDF**: v5.5.x
- **Arduino Core**: arduino‑esp32 3.3.x (as ESP‑IDF component)
- **Matter**: esp‑matter + Arduino Matter wrappers
- **Transport**: Thread (IEEE 802.15.4)
- **Sensor driver**: `SHT4xMinimal` (default) or Adafruit SHT4x via `USE_MINIMAL_SHT4X=0`

---

## Project Structure (important bits)

```text
TinyENV_Sensor-Thread/
├── CMakeLists.txt              # Top-level CMake (ESP-IDF project entry)
├── sdkconfig
├── sdkconfig.defaults
├── sdkconfig.defaults.debug
├── sdkconfig.defaults.lit.old
├── sdkconfig.old
├── dependencies.lock
├── partitions.csv
├── README.md
├── components/
│   ├── Adafruit_SHT4X/         # SHT4x temperature/humidity sensor driver
│   ├── Adafruit_BusIO/         # Adafruit BusIO dependency
│   ├── Adafruit_Sensor/        # Adafruit unified sensor base
│   ├── SHT4xMinimal/           # Minimal SHT4x driver (default)
│   └── MatterEndpoints/
│       ├── include/
│       │   └── MatterEndpoints/
│       │       └── MatterTemperatureSensorBattery.h
│       ├── MatterTemperatureSensorBattery.cpp
│       └── CMakeLists.txt
└── main/
    ├── TinyENV_Thread.cpp      # Main application entry point
    ├── CMakeLists.txt
    └── idf_component.yml
```

`sdkconfig.defaults` is the LIT (long idle time) release default. `sdkconfig.defaults.debug` enables verbose logging.

### Custom Matter Endpoint

A custom endpoint (`MatterTemperatureSensorBattery`) extends the standard Matter temperature sensor to also expose **battery voltage and percentage**. This is a local shim, not a published library.

---

## What Changed From the Original Example

- Replaced example **Color Light** with:
  - Temperature Sensor cluster
  - Humidity Sensor cluster
  - Battery reporting
- Switched transport to **Matter over Thread** (no Wi‑Fi required at runtime)
- Explicit USB CDC configuration so `Serial.print()` works reliably
- Explicit I²C pin configuration for XIAO ESP32‑C6
- Headless commissioning (QR + manual code via serial)
- Added power‑saving features (CPU scaling + light sleep)

---

## One‑Time ESP‑IDF Setup

Clone ESP‑IDF and install tools (once per machine):

```bash
git clone -b v5.5 https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh
```

Add a shell alias (recommended):

```bash
alias espidf-init='. $HOME/esp-idf/export.sh'
```

---

## Build & Flash Instructions

From the project root:

```bash
espidf-init
idf.py set-target esp32c6
idf.py fullclean
idf.py build
idf.py -p /dev/cu.usbmodemXXXX erase-flash flash monitor
```

Expected idle draw in LIT mode: ~4 mA (measured after commissioning).

Debug build (verbose logs):

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.debug" set-target esp32c6 build
```

Flashing without erase keeps commissioning state:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

> ⚠️ Use `/dev/cu.*`, **not** `/dev/tty.*` on macOS.

---

## Commissioning

On first boot (or after erase/decommission), the device prints:

- Manual pairing code
- QR code URL

Use either **Home Assistant** or **Apple Home** to commission. The same firmware works in both ecosystems.

BOOT button held for **>5 seconds** will decommission the node.

---

## Runtime Behavior

- Sensor polling interval: **120 seconds**.
- Updates temperature, humidity, and battery over Matter over Thread every 2 minutes.
- Runs as a Thread **ICD sleepy end device**; timing is driven by `sdkconfig.defaults` (LIT).
- Designed to run unattended on battery.
- Serial output and behavior tied to `VERBOSE_PRINTS` (boot button toggle).

Example serial output:

```text
Updated: 72.8 F, 55 %RH, VBAT: 4.12V (93%)
```

---


## Status

✅ Builds cleanly on ESP‑IDF 5.5.x.
✅ Works with Home Assistant.
✅ Works with Apple Home.
✅ Matter over Thread confirmed working.

This is a stable baseline. Future work will focus on power optimization and optional sensor expansion.

## To-Do

- [x] Build current monitor jig.
- [x] Reduce power consumption to <10mA average.
- [x] A/B test SHT4xMinimal vs Adafruit driver power draw.
- [ ] Verify low power/sleep states and ICD timing behavior.
- [ ] Increase ICD slow-poll/idle intervals and measure impact.
- [ ] Disable BLE after commissioning and confirm stable Thread operation.
- [ ] Reduce sensor sampling cadence and measure average draw.
- [ ] Minimize advertising/mDNS activity after join and retest commissioning.
- [x] Enable `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP` and re-measure.
- [ ] Enable `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH` and re-measure.
- [ ] Drive unused/external GPIOs to a defined state (or hold) during sleep and re-measure.
- [ ] Evaluate GPIO pull-ups/downs on unused pins to reduce leakage.
- [ ] Tune `CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP` for faster entry to light sleep.
- [ ] Reduce PHY/BLE TX power (`CONFIG_ESP_PHY_REDUCE_TX_POWER`, `CONFIG_BT_LE_DFT_TX_POWER_LEVEL_DBM`).

## Device Identity

Device strings are defined in `main/chip_project_config.h` (vendor/product name, software/hardware version strings). Numeric versions are set in `sdkconfig.defaults`.
