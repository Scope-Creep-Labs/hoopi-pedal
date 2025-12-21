idf_component_register(SRCS "hoopi.cpp"
                    INCLUDE_DIRS ""
                    PRIV_REQUIRES esp_wifi nvs_flash esp_psram esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_http_server json esp_driver_uart
                    REQUIRES fatfs
                    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/effects_config.json")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-literal-suffix -Wno-narrowing -Wno-deprecated-declarations -Wno-write-strings)