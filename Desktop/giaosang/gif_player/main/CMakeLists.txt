idf_component_register(
    SRCS main.c tft_driver.c frame_buffer.c gif_decoder.c gif_player.c
    INCLUDE_DIRS "."
    REQUIRES spi_flash driver
)
