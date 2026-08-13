idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "esp_lcd_sh8601.c"
    INCLUDE_DIRS "."
    EMBED_FILES "../assets/antena1-logo.png"
    REQUIRES esp_wifi nvs_flash esp_netif esp_lcd esp_driver_gpio esp_driver_spi
)
