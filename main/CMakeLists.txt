idf_component_register(
    SRCS "TinyENV_Thread.cpp"
    INCLUDE_DIRS ""
    REQUIRES MatterEndpoints Adafruit_BME280 DallasTemperature OneWire
)
