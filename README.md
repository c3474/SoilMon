# Garden Sensor

Battery-powered outdoor soil and air sensor using **Matter over Thread**. Appears natively in **Apple Home** via a HomePod 2 Thread Border Router. No Wi-Fi required.

## Sensors

| Measurement | Source | Apple Home label |
|---|---|---|
| Air temperature | BME280 (I²C) | Temperature |
| Air humidity | BME280 (I²C) | Humidity |
| Air pressure | BME280 (I²C) | — |
| Soil temperature | DS18B20 (1-Wire) | Soil Temperature |
| Soil moisture | Capacitive probe (ADC) | Soil Moisture |
| Battery level | 18650 via divider (ADC) | Battery |

## Hardware

| Part | Notes |
|---|---|
| Seeed XIAO ESP32-C6 | MCU + 802.15.4 Thread radio |
| GY-BME280 3.3V breakout | Must be 3.3V variant |
| DIYables capacitive soil sensor v1.2 | Analog output |
| DS18B20 waterproof probe | 1-Wire, 4.7kΩ pull-up to 3.3V |
| 2N7000 / BS170 N-channel MOSFET | Cuts sensor power during sleep |
| 18650 cell + TP4056 with protection | Solar-chargeable power |
| 6V 1–2W solar panel + 1N5819 diode | Oregon-sized for spring–fall |

### Wiring

```
XIAO ESP32-C6
├── GPIO0  → BME280 SDA
├── GPIO1  → BME280 SCL
├── GPIO2  → DS18B20 data + 4.7kΩ pull-up to 3.3V
├── GPIO3  ← Soil sensor AOUT
├── GPIO4  → MOSFET gate (powers soil sensor + DS18B20)
├── A0     ← Battery voltage (1:2 divider)
├── 3.3V   → BME280 VCC, MOSFET drain rail
└── GND    → all sensor GNDs
```

## Flashing

### Web flasher (Chrome/Edge only)

Visit the GitHub Pages URL for this repo and click **Install**.

### USB (development)

```bash
cd /Users/Shared/SoilMon
export PATH="/opt/homebrew/bin:$PATH"
source esp-idf/export.sh
source esp-matter/export.sh
cd garden-sensor
idf.py -p /dev/cu.usbmodem* flash monitor
```

> Use `/dev/cu.*` not `/dev/tty.*` on macOS.

## Commissioning

On first boot the device prints a pairing code and QR URL to the serial monitor:

1. Open **Apple Home → Add Accessory → More Options**
2. Scan the QR code or enter the manual pairing code
3. HomePod 2 commissions the device onto the Thread network

**Boot button:**
- Short press → toggle verbose logging (2 blinks = on, 1 blink = off)
- Hold 5 s → decommission

## Soil Moisture Calibration

After hardware arrives, calibrate the two constants at the top of [main/TinyENV_Thread.cpp](main/TinyENV_Thread.cpp):

```cpp
static const int SOIL_DRY_RAW = 2800;  // ADC reading with sensor in dry air
static const int SOIL_WET_RAW = 1200;  // ADC reading with sensor submerged
```

1. Power on, open serial monitor, enable verbose mode (short press boot button)
2. Hold the sensor in dry air — note the `Soil ADC raw avg:` value → set `SOIL_DRY_RAW`
3. Submerge the sensor tip in water — note the value → set `SOIL_WET_RAW`
4. Reflash

## Power Budget

| State | Current |
|---|---|
| ICD LIT idle (Thread keepalive) | ~4 mA |
| Active read + transmit | ~60 mA |
| **Effective average** | **~4.5 mA** |

18650 @ 2500 mAh ÷ 4.5 mA ≈ 23 days on battery alone. A 2W panel in Oregon offsets the draw spring through fall.

## Toolchain

| Component | Version |
|---|---|
| ESP-IDF | v5.4.1 |
| esp-matter | latest main |
| arduino-esp32 | 3.3.5 (managed component) |

See [garden-sensor-handoff.md](../garden-sensor-handoff.md) for full toolchain setup instructions.

## CI

GitHub Actions builds the firmware on every push to `main` and deploys the web flasher to GitHub Pages. First build takes 30–60 min (connectedhomeip cold compile); subsequent builds with cache are ~10–15 min.
