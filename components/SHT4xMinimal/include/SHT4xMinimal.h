#pragma once

#include <Arduino.h>
#include <Wire.h>

class SHT4xMinimal {
public:
  bool begin(TwoWire &wire = Wire, uint8_t address = 0x44);
  bool read(float &tempC, float &rh);

private:
  static uint8_t crc8(const uint8_t *data, size_t len);

  TwoWire *m_wire = nullptr;
  uint8_t m_address = 0x44;
  bool m_ready = false;
};
