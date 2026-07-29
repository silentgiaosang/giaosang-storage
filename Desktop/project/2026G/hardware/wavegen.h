#ifndef __WAVEGEN_H__
#define __WAVEGEN_H__

#include <stdint.h>
#include "app_interface.h"
#include "measure.h"       /* for PeakResult_t, WaveType_t */

#define WAVE_PTS  600      /* fixed points per screen frame */

/* Individual generators. Each returns actual points written (always WAVE_PTS). */
uint16_t WaveGen_Sine(    float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r);
uint16_t WaveGen_Square(  float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r);
uint16_t WaveGen_Triangle(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r);
uint16_t WaveGen_Harmonic(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r, PeakResult_t *p);

/* Dispatcher: picks the right generator based on type. */
uint16_t WaveGen_Generate(float *buf, uint16_t buf_len, WaveType_t type,
                          uint8_t cycles, MeasureResult_t *r, PeakResult_t *p);

#endif
