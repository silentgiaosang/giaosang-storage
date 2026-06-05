#include "tft_driver.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tft";
static spi_device_handle_t spi;

static void wcmd(uint8_t c) {
    gpio_set_level(PIN_DC, 0);
    spi_device_polling_transmit(spi, &(spi_transaction_t){.length=8, .tx_buffer=&c});
}
static void wdata(const uint8_t *d, size_t l) {
    gpio_set_level(PIN_DC, 1);
    spi_device_polling_transmit(spi, &(spi_transaction_t){.length=l*8, .tx_buffer=d});
}

esp_err_t tft_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num=PIN_MOSI, .miso_io_num=-1, .sclk_io_num=PIN_SCLK,
        .quadwp_io_num=-1, .quadhd_io_num=-1, .max_transfer_sz=TFT_WIDTH*2
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz=SPI_CLOCK_HZ, .mode=0, .spics_io_num=PIN_CS,
        .queue_size=2, .flags=SPI_DEVICE_HALFDUPLEX
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));
    gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);

    gpio_set_level(PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t cmds[] = {
        0x01,0,150, 0x11,0,255,
        0xB1,3,0x01,0x2C,0x2D,
        0xB2,3,0x01,0x2C,0x2D,
        0xB3,6,0x01,0x2C,0x2D,0x01,0x2C,0x2D,
        0xB4,1,0x07, 0xC0,3,0xA2,0x02,0x84,
        0xC1,1,0xC5, 0xC2,2,0x0A,0x00,
        0xC3,2,0x8A,0x2A, 0xC4,2,0x8A,0xEE,
        0xC5,1,0x0E, 0x3A,1,0x05,
        0x36,1,0xC8,
        0xE0,16,0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10,
        0xE1,16,0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10,
        0x13,0,10, 0x29,0,10, 0xFF
    };
    for (int i=0; cmds[i]!=0xFF;) {
        uint8_t c=cmds[i++], n=cmds[i++];
        wcmd(c); if(n){wdata(cmds+i,n); i+=n;}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "ST7735S OK");
    tft_set_backlight(255);
    return ESP_OK;
}

void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    wcmd(0x2A);
    uint8_t cd[4]={(x0>>8)&0xFF,x0&0xFF,(x1>>8)&0xFF,x1&0xFF};
    wdata(cd,4);
    wcmd(0x2B);
    uint8_t rd[4]={(y0>>8)&0xFF,y0&0xFF,(y1>>8)&0xFF,y1&0xFF};
    wdata(rd,4);
}

esp_err_t tft_send_frame(const uint16_t *frame) {
    tft_set_window(0,0,TFT_WIDTH-1,TFT_HEIGHT-1);
    wcmd(0x2C);
    uint8_t lb[TFT_WIDTH*2] __attribute__((aligned(4)));
    for (int y=0; y<TFT_HEIGHT; y++) {
        memcpy(lb, &frame[y*TFT_WIDTH], TFT_WIDTH*2);
        gpio_set_level(PIN_DC, 1);
        esp_err_t r = spi_device_polling_transmit(spi,
            &(spi_transaction_t){.length=TFT_WIDTH*2*8, .tx_buffer=lb});
        if (r) return r;
    }
    return ESP_OK;
}

void tft_fill_screen(uint16_t color) {
    tft_set_window(0,0,TFT_WIDTH-1,TFT_HEIGHT-1);
    wcmd(0x2C);
    uint8_t lb[TFT_WIDTH*2] __attribute__((aligned(4)));
    for (int i=0; i<TFT_WIDTH; i++) { lb[i*2]=(color>>8)&0xFF; lb[i*2+1]=color&0xFF; }
    gpio_set_level(PIN_DC, 1);
    for (int y=0; y<TFT_HEIGHT; y++)
        spi_device_polling_transmit(spi, &(spi_transaction_t){.length=TFT_WIDTH*2*8, .tx_buffer=lb});
}

void tft_set_backlight(uint8_t b) {
    gpio_set_level(PIN_BL, b ? 1 : 0);
}
