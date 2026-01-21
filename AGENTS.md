# Repository Guidelines

## Project Structure & Module Organization
- `main/` contains the application entry (`main/TinyENV_Thread.cpp`) and project-specific headers.
- `components/` holds local components (e.g., `MatterEndpoints/`, `SHT4xMinimal/`, Adafruit drivers).
- `sdkconfig.defaults` is the minimal **SIT** ICD defaults; `sdkconfig.defaults.lit` is the **LIT** ICD variant.
- `partitions.csv` defines the custom partition layout (offset `0xC000`).
- `codex/SessionLog.md` records session changes and decisions (ignored by git).
  - Quick note: this is the canonical session history location.

## Build, Test, and Development Commands
- `idf.py fullclean` — reset build artifacts and managed components (use after config changes).
- `idf.py build` — build with SIT defaults.
- `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.lit" set-target esp32c6 build` — build with LIT defaults.
- `idf.py -p /dev/cu.usbmodemXXXX flash monitor` — flash without erasing NVS (keeps commissioning).
- `idf.py -p /dev/cu.usbmodemXXXX erase-flash flash monitor` — clean flash (resets commissioning).

## Coding Style & Naming Conventions
- C/C++ style: 2-space indentation, `snake_case` for functions, `UPPER_SNAKE` for constants.
- Prefer small, self-contained helpers in `main/TinyENV_Thread.cpp` and local components in `components/`.
- Avoid Unicode unless already present; keep code comments concise and rare.

## Testing Guidelines
- No automated tests are currently defined.
- Verify by flashing and observing Matter commissioning + periodic updates.
- When changing power behavior, include a short note of idle/active current measurements.

## Commit & Pull Request Guidelines
- Commit messages are short, imperative summaries (e.g., “ICD config cleanup and defaults split”).
- PRs should include: purpose, config used (SIT/LIT), flash method (erase or not), and any power/commissioning observations.

## Configuration Tips
- ICD timing is controlled by Kconfig (SIT/LIT defaults). Debug mode still forces fast polling.
- Product/Vendor strings live in `main/chip_project_config.h` and are referenced by `CONFIG_CHIP_PROJECT_CONFIG`.
- LIT commissioning requires full mDNS (`CONFIG_USE_MINIMAL_MDNS=n`); minimal mDNS blocks pairing.
- Recent work resolved LIT commissioning and verified ~4 mA idle draw in LIT mode.
- Low-power candidate toggles for future tests: `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP`, `CONFIG_ESP_SLEEP_POWER_DOWN_FLASH`, `CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP`, and PHY/BLE TX power reduction.
