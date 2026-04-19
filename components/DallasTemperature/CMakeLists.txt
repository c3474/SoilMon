idf_component_register(
  SRCS "DallasTemperature.cpp"
  INCLUDE_DIRS "."
  REQUIRES espressif__arduino-esp32 OneWire
)
