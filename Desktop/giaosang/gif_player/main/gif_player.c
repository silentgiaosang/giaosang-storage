#include "gif_player.h"
#include "gif_decoder.h"
#include "frame_buffer.h"
#include "tft_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "player";

static struct {
    gif_decoder_t *dec;
    player_state_t state;
    int cur;
    int total;
    int loop_cnt;
    int loop_now;
    bool paused;
    bool need_prev;
    esp_timer_handle_t timer;
    volatile bool delayed;
} ctx;

static void timer_cb(void *arg) { ctx.delayed = true; }

void player_init(void)
{
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = PLAYER_IDLE;
    esp_timer_create_args_t ta = { .callback = timer_cb, .name = "gif_delay" };
    esp_timer_create(&ta, &ctx.timer);
}

void player_start(const uint8_t *data, size_t len)
{
    if (ctx.state != PLAYER_IDLE) player_stop();
    gif_result_t r = gif_decoder_init(&ctx.dec, data, len);
    if (r != GIF_OK) { ESP_LOGE(TAG, "GIF init err %d", r); ctx.state = PLAYER_ERROR; return; }

    uint16_t gw, gh;
    gif_get_size(ctx.dec, &gw, &gh);
    if (gw > TFT_WIDTH || gh > TFT_HEIGHT)
        ESP_LOGW(TAG, "GIF %dx%d exceeds screen", gw, gh);

    ctx.cur = 0;
    ctx.total = gif_get_frame_count(ctx.dec);
    ctx.loop_cnt = gif_get_loop_count(ctx.dec);
    ctx.loop_now = 0;
    ctx.paused = false;
    ctx.need_prev = false;
    ctx.delayed = false;

    if (ctx.total == 0) { ctx.state = PLAYER_FINISHED; return; }
    memset(fb_get_back(), 0, FB_BYTES);
    memset(fb_get_front(), 0, FB_BYTES);
    ctx.state = PLAYER_DECODE;
    ESP_LOGI(TAG, "Play %dframes loop=%d", ctx.total, ctx.loop_cnt);
}

void player_pause(void)  { ctx.paused = true; }
void player_resume(void) { ctx.paused = false; }

void player_stop(void) {
    gif_decoder_deinit(ctx.dec);
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = PLAYER_IDLE;
}

player_state_t player_tick(void)
{
    if (ctx.paused) return ctx.state;

    switch (ctx.state) {
    case PLAYER_DECODE: {
        uint16_t *back = fb_get_back();
        if (ctx.need_prev && ctx.cur > 0) {
            uint16_t *p = fb_get_prev();
            if (p) memcpy(p, fb_get_front(), FB_BYTES);
        }
        gif_result_t r = gif_decode_frame(ctx.dec, ctx.cur, back, TFT_WIDTH, TFT_HEIGHT);
        if (r != GIF_OK) { ctx.state = PLAYER_ERROR; return ctx.state; }
        ctx.state = PLAYER_DISPLAY;
        break;
    }

    case PLAYER_DISPLAY: {
        fb_swap();
        uint16_t *front = fb_get_front();
        gif_frame_info_t inf;
        gif_get_frame_info(ctx.dec, ctx.cur, &inf);
        tft_send_frame(front);

        // Apply disposal: 0/1=NONE, 2=BACKGROUND, 3=PREVIOUS
        switch (inf.disposal) {
        case 2:
            for (int y=inf.top; y<inf.top+inf.height&&y<TFT_HEIGHT; y++)
                for (int x=inf.left; x<inf.left+inf.width&&x<TFT_WIDTH; x++)
                    fb_get_back()[y*TFT_WIDTH+x] = 0;
            ctx.need_prev = false;
            break;
        case 3:
            if (ctx.cur > 0) {
                uint16_t *p = fb_get_prev();
                if (p) memcpy(fb_get_back(), p, FB_BYTES);
            }
            ctx.need_prev = true;
            break;
        default:
            ctx.need_prev = false;
            break;
        }

        uint32_t d = inf.delay_ms;
        if (d < 20) d = 100;
        ctx.delayed = false;
        esp_timer_start_once(ctx.timer, d * 1000);
        ctx.state = PLAYER_DELAY;
        break;
    }

    case PLAYER_DELAY: {
        if (!ctx.delayed) break;
        ctx.delayed = false;
        ctx.cur++;
        if (ctx.cur >= ctx.total) {
            if (ctx.loop_cnt == 0 || ctx.loop_now + 1 < ctx.loop_cnt) {
                ctx.loop_now++;
                ctx.cur = 0;
                memset(fb_get_back(), 0, FB_BYTES);
                memset(fb_get_front(), 0, FB_BYTES);
            } else {
                ctx.state = PLAYER_FINISHED;
                return ctx.state;
            }
        }
        ctx.state = PLAYER_DECODE;
        break;
    }

    default: break;
    }
    return ctx.state;
}

player_state_t player_get_state(void) { return ctx.state; }
