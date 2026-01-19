#include "SHT4xMinimal.h"

namespace {
constexpr uint8_t kCmdMeasureHighNoHeater = 0xFD;
constexpr uint32_t kMeasureDelayMs = 10;
}

bool SHT4xMinimal::begin(TwoWire &wire, uint8_t address) {
  m_wire = &wire;
  m_address = address;

  m_wire->beginTransmission(m_address);
  m_ready = (m_wire->endTransmission() == 0);
  return m_ready;
}

bool SHT4xMinimal::read(float &tempC, float &rh) {
  if (!m_ready || m_wire == nullptr) return false;

  m_wire->beginTransmission(m_address);
  m_wire->write(kCmdMeasureHighNoHeater);
  if (m_wire->endTransmission() != 0) return false;

  delay(kMeasureDelayMs);

  const uint8_t to_read = 6;
  uint8_t buf[to_read] = {0};
  if (m_wire->requestFrom((int)m_address, (int)to_read) != to_read) {
    return false;
  }

  for (uint8_t i = 0; i < to_read; i++) {
    buf[i] = m_wire->read();
  }

  if (crc8(buf, 2) != buf[2]) return false;
  if (crc8(buf + 3, 2) != buf[5]) return false;

  const uint16_t raw_t = (uint16_t)(buf[0] << 8) | buf[1];
  const uint16_t raw_rh = (uint16_t)(buf[3] << 8) | buf[4];

  tempC = -45.0f + (175.0f * (float)raw_t / 65535.0f);
  rh = 100.0f * (float)raw_rh / 65535.0f;
  return true;
}

uint8_t SHT4xMinimal::crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (uint8_t)((crc << 1) ^ 0x31);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}
