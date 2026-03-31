idf_component_register(SRCS "rotary.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver freertos log esp_timer
                       )
