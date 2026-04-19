#!/bin/bash
set -e

# Fetch managed components (downloads esp_matter, arduino-esp32, etc.)
idf.py reconfigure

# Patch arduino-esp32 3.3.5 for IDF 5.4.1 compatibility.
# i2c_ll_slave_set_fifo_mode was removed in IDF 5.4.1 but the version guard
# in arduino-esp32 3.3.5 only switches to i2c_ll_enable_fifo_mode at >= 5.4.2.
SLAVE_FILE="managed_components/espressif__arduino-esp32/cores/esp32/esp32-hal-i2c-slave.c"
if [ ! -f "$SLAVE_FILE" ]; then
  echo "ERROR: $SLAVE_FILE not found after reconfigure"
  exit 1
fi

# Fix the fifo-mode version guard: arduino-esp32 3.3.5 checks >= 5.4.2 but
# i2c_ll_slave_set_fifo_mode was already removed in IDF 5.4.1. Lower the guard
# to 5.4.1 so IDF 5.4.1 uses i2c_ll_enable_fifo_mode instead.
# Do NOT touch the outer block condition at line ~342 (>= 5.4.2 && < 5.5.0) —
# IDF 5.4.1 must stay in the #else branch (i2c_ll_slave_init path) because
# the TRUE branch uses i2c_ll_set_mode / I2C_BUS_MODE_SLAVE which don't exist
# in IDF 5.4.1.
sed -i 's/ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)/ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 1)/g' "$SLAVE_FILE"
sed -i 's/i2c_ll_slave_set_fifo_mode(i2c->dev, true)/i2c_ll_enable_fifo_mode(i2c->dev, true)/g' "$SLAVE_FILE"

echo "Patched $SLAVE_FILE:"
grep -n "fifo_mode\|4, 1\|4, 2" "$SLAVE_FILE" | head -20

# Full build
idf.py build
