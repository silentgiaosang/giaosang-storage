#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tft_driver.h"
#include "frame_buffer.h"
#include "gif_player.h"
#include "gif_data.h"

static const char *TAG = "gif_player";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting...");

    // 1. Initialize TFT
    ESP_ERROR_CHECK(tft_init());
    tft_fill_screen(0x001F);  // blue while loading
    ESP_LOGI(TAG, "TFT OK");

    // 2. Initialize frame buffer (PSRAM)
    fb_result_t fbr = fb_init();
    if (fbr != FB_OK) {
        ESP_LOGE(TAG, "Frame buffer init failed: %d", fbr);
        return;
    }

    // 3. Start GIF player
    player_init();
    player_start(gif_animation, GIF_ANIMATION_LEN);
    ESP_LOGI(TAG, "Playback started");

    // 4. Main loop: tick player at ~60 Hz
    while (1) {
        player_state_t st = player_tick();
        if (st == PLAYER_ERROR) {
            ESP_LOGE(TAG, "Playback error");
            tft_fill_screen(0xF800);  // red = error
            break;
        }
        if (st == PLAYER_FINISHED) {
            ESP_LOGI(TAG, "Playback finished");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
