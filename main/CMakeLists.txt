idf_component_register(SRCS "idfzk.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_event nvs_flash json spiffs ssd1306 mqtt esp_driver_gpio)

# SPIFFS配置
spiffs_create_partition_image(assets ../assets FLASH_IN_PROJECT)
