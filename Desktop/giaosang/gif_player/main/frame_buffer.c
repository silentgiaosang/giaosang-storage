#include "frame_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fb";
static uint16_t *ca, *cb, *cp;
static int state;

fb_result_t fb_init(void) {
    if (!esp_psram_is_initialized()) { ESP_LOGE(TAG,"No PSRAM"); return FB_ERR_NO_PSRAM; }
    ca = heap_caps_calloc(1, FB_BYTES, MALLOC_CAP_SPIRAM);
    cb = heap_caps_calloc(1, FB_BYTES, MALLOC_CAP_SPIRAM);
    if (!ca||!cb) { free(ca); free(cb); return FB_ERR_ALLOC; }
    ESP_LOGI(TAG,"FB A=%p B=%p",ca,cb); return FB_OK;
}
uint16_t *fb_get_back(void)  { return state ? ca : cb; }
uint16_t *fb_get_front(void) { return state ? cb : ca; }
void      fb_swap(void)      { state = !state; }
uint16_t *fb_get_prev(void)  { if(!cp) cp=heap_caps_calloc(1,FB_BYTES,MALLOC_CAP_SPIRAM); return cp; }
void      fb_deinit(void)    { free(ca); free(cb); free(cp); ca=cb=cp=NULL; }
bool      fb_has_psram(void) { return esp_psram_is_initialized(); }
