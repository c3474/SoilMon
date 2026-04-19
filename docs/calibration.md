# Soil Moisture Calibration Guide

## Overview

The capacitive soil sensor outputs an analog voltage that varies inversely with moisture — drier soil = higher ADC reading. Two constants in the firmware define the dry and wet endpoints of the 0–100% scale:

```cpp
// main/TinyENV_Thread.cpp — top of file
static const int SOIL_DRY_RAW = 2800;  // sensor in dry air
static const int SOIL_WET_RAW = 1200;  // sensor tip submerged in water
```

These defaults are reasonable starting points for the DIYables v1.2 sensor on the ESP32-C6 ADC at 3.3V, but your specific unit may differ by ±200 counts. Calibrating takes about 5 minutes.

---

## Before You Start

**You will need:**
- Flashed and commissioned device (or just flashed — calibration doesn't require Matter)
- USB cable connected to your Mac
- A cup of tap water
- Serial monitor open (`idf.py -p /dev/cu.usbmodem* monitor` or Arduino IDE)
- Verbose logging enabled (short-press the Boot button — 2 LED blinks = on)

**What to expect in the serial output:**
```
[SENSOR] Soil ADC raw avg: 2763   moisture: 2%
[SENSOR] Soil ADC raw avg: 1418   moisture: 71%
```
The firmware averages 8 ADC reads per sample to reduce noise. Raw values above ~3800 indicate a wiring problem; values that don't change at all suggest the sensor isn't getting power.

---

## Step 1 — Dry Calibration

1. Hold the soil sensor completely in **dry open air** — don't let it touch anything.
2. Watch the serial output for the `Soil ADC raw avg:` line.
3. Wait for 3–4 consecutive readings to stabilize (they'll vary by ±20 counts — that's normal).
4. Note the stable value. This is your `SOIL_DRY_RAW`.

**Typical range for this sensor:** 2600–3100

> If you read above 3800, check that GPIO4 (MOSFET gate) is outputting HIGH during the measurement window and that the sensor VCC line is actually powered.

---

## Step 2 — Wet Calibration

1. Fill a cup with tap water.
2. Submerge **only the tip of the sensor** (the capacitive portion, roughly the bottom 3–4 cm). Don't submerge the electronics or connectors.
3. Watch `Soil ADC raw avg:` and wait for readings to stabilize — takes about 10–15 seconds as the sensor equilibrates.
4. Note the stable value. This is your `SOIL_WET_RAW`.

**Typical range for this sensor:** 1100–1500

> Don't use distilled water — its very low conductivity produces readings that don't reflect real soil conditions. Tap water or a slightly damp paper towel are both fine.

---

## Step 3 — Update the Firmware

Open [main/TinyENV_Thread.cpp](../main/TinyENV_Thread.cpp) and update the two constants near the top:

```cpp
static const int SOIL_DRY_RAW = 2763;  // ← your dry reading
static const int SOIL_WET_RAW = 1418;  // ← your wet reading
```

Then reflash:

```bash
cd /Users/Shared/SoilMon
export PATH="/opt/homebrew/bin:$PATH"
source esp-idf/export.sh
source esp-matter/export.sh
cd garden-sensor
idf.py -p /dev/cu.usbmodem* flash monitor
```

---

## Step 4 — Verify in Apple Home

After reflashing and re-commissioning (if needed):

1. Open **Apple Home** and find the Garden Sensor tile.
2. Hold the sensor in dry air — the **Soil Moisture** reading should settle near **0%** (within 5% is fine).
3. Submerge the tip in water — it should read **90–100%**.
4. Push the sensor into moist potting soil — expect somewhere in the **30–60%** range depending on actual moisture.

If the reading is backwards (wet reads lower than dry), your `SOIL_DRY_RAW` and `SOIL_WET_RAW` are swapped — swap them in the code.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Moisture always 0% | `SOIL_DRY_RAW` too low or sensor not powered | Check wiring; re-run dry calibration |
| Moisture always 100% | `SOIL_WET_RAW` too high | Re-run wet calibration |
| Reading stuck, doesn't change | MOSFET not switching, sensor never powered | Check GPIO4 → MOSFET gate wiring |
| Raw ADC jumping ±500 counts | Loose connection or poor solder joint | Reseat connectors; reflow joints |
| Soil reads 100% when dry | Constants swapped | Swap `SOIL_DRY_RAW` and `SOIL_WET_RAW` |
| Apple Home shows "No Response" | Matter/Thread issue, not calibration | Check Thread connectivity via HomePod |

---

## Understanding the Math

The firmware maps raw ADC to percentage with a simple linear interpolation and clamps the result to 0–100:

```cpp
int pct = map(raw, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100);
pct = constrain(pct, 0, 100);
SoilMoiSensor.setHumidity(pct);  // Matter Humidity cluster, 0–100
```

Because the sensor reads *higher* when dry, `SOIL_DRY_RAW > SOIL_WET_RAW`. The `map()` function handles the inversion naturally — no sign flipping needed.

---

## Field Notes

Once installed in the garden, the calibration will hold indefinitely — the sensor capacitance doesn't drift meaningfully with temperature or age. You may want to re-verify after the first season if readings feel off, but in practice a single calibration is usually good for the life of the unit.

The sensor should be buried with the capacitive strip fully in the root zone (typically 5–15 cm depth depending on what you're growing). Avoid burying the connector; use the waterproof probe body as the depth stop.
