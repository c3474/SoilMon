#!/usr/bin/env bash
set -euo pipefail

source "$HOME/esp-idf/export.sh"

idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.lit" \
  -D EXCLUDE_COMPONENTS="esp_modem;esp_rainmaker;espressif__esp_modem;espressif__esp_rainmaker" \
  set-target esp32c6 build
