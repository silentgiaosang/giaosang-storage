#ifndef GIF_PLAYER_H
#define GIF_PLAYER_H
#include <stdint.h>
#include <stddef.h>

typedef enum {
    PLAYER_IDLE, PLAYER_DECODE, PLAYER_DISPLAY,
    PLAYER_DELAY, PLAYER_FINISHED, PLAYER_ERROR
} player_state_t;

void player_init(void);
void player_start(const uint8_t *data, size_t len);
void player_pause(void);
void player_resume(void);
void player_stop(void);
player_state_t player_tick(void);
player_state_t player_get_state(void);
#endif
