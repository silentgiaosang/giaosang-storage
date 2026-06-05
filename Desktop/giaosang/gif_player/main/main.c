#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "gif_player";

void app_main(void)
{
    ESP_LOGI(TAG, "GIF Player starting...");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
