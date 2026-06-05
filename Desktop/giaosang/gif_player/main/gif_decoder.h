#ifndef GIF_DECODER_H
#define GIF_DECODER_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum { GIF_OK=0, GIF_ERR_INVALID_SIG, GIF_ERR_CORRUPT, GIF_ERR_OOM } gif_result_t;

typedef struct {
    uint16_t left, top, width, height, delay_ms;
    uint8_t disposal;
    bool has_transparent;
    uint8_t transparent_idx;
    bool interlaced;
} gif_frame_info_t;

typedef struct gif_decoder gif_decoder_t;

gif_result_t gif_decoder_init(gif_decoder_t **dec, const uint8_t *data, size_t len);
gif_result_t gif_decode_frame(gif_decoder_t *dec, int frame_num, uint16_t *canvas, int cw, int ch);
gif_result_t gif_get_frame_info(gif_decoder_t *dec, int frame_num, gif_frame_info_t *info);
int   gif_get_frame_count(gif_decoder_t *dec);
int   gif_get_loop_count(gif_decoder_t *dec);
void  gif_get_size(gif_decoder_t *dec, uint16_t *w, uint16_t *h);
void  gif_decoder_deinit(gif_decoder_t *dec);
#endif
