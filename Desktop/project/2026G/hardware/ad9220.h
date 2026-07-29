#ifndef __AD9220_H__
#define __AD9220_H__

#include "main.h"

#define AD9220_BUF_SIZE   4096

typedef enum {
    AD9220_TIER_LOW  = 0,
    AD9220_TIER_MID  = 1,
    AD9220_TIER_HIGH = 2,
    AD9220_TIER_COUNT
} AD9220_Tier;

typedef struct {
    float freq_khz;
    float vpp_mv;
    float vrms_mv;
} AD9220_Result;

extern uint16_t ad9220_buffer[AD9220_BUF_SIZE];
extern volatile uint8_t ad9220_data_ready;

void AD9220_Init(void);
void AD9220_Start(AD9220_Tier tier);
void AD9220_Stop(void);
uint8_t AD9220_DataReady(void);
void AD9220_ClearReady(void);
void AD9220_DebugDump(void);

#endif
