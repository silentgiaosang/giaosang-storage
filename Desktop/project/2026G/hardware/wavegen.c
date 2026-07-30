#include "wavegen.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ================================================================
 *  Sine wave
 * ================================================================ */
uint16_t WaveGen_Sine(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r)
{
    float amp     = r->vpp_mv * 0.5f;
    float pts_per = (float)buf_len / (float)cycles;

    for (uint16_t i = 0; i < buf_len; i++) {
        float phase = (float)i / pts_per;
        buf[i] = amp * sinf(2.0f * M_PI * phase);
    }
    return buf_len;
}

/* ================================================================
 *  Square wave
 * ================================================================ */
uint16_t WaveGen_Square(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r)
{
    float amp     = r->vpp_mv * 0.5f;
    float pts_per = (float)buf_len / (float)cycles;

    for (uint16_t i = 0; i < buf_len; i++) {
        float phase = (float)i / pts_per;
        float t     = phase - floorf(phase);   /* 0..1 per cycle */
        buf[i] = (t < 0.5f) ? amp : -amp;
    }
    return buf_len;
}

/* ================================================================
 *  Triangle wave
 * ================================================================ */
uint16_t WaveGen_Triangle(float *buf, uint16_t buf_len, uint8_t cycles, MeasureResult_t *r)
{
    float amp     = r->vpp_mv * 0.5f;
    float pts_per = (float)buf_len / (float)cycles;

    for (uint16_t i = 0; i < buf_len; i++) {
        float phase = (float)i / pts_per;
        float t     = phase - floorf(phase);   /* 0..1 per cycle */
        if (t < 0.25f)
            buf[i] = amp * 4.0f * t;
        else if (t < 0.75f)
            buf[i] = amp * (2.0f - 4.0f * t);
        else
            buf[i] = amp * (4.0f * t - 4.0f);
    }
    return buf_len;
}

/* ================================================================
 *  Harmonic (multi-tone) — sum of up to 3 measured sines
 * ================================================================ */
uint16_t WaveGen_Harmonic(float *buf, uint16_t buf_len, uint8_t cycles,
                          MeasureResult_t *r, PeakResult_t *p)
{
    float pts_per = (float)buf_len / (float)cycles;
    float f0      = r->f_base_hz;

    for (uint16_t i = 0; i < buf_len; i++) {
        float phase = (float)i / pts_per;
        float val   = 0.0f;
        for (int k = 0; k < p->count && k < 3; k++) {
            float fk = p->freq_hz[k];
            float ak = p->vpp_mv[k] * 0.5f;
            val += ak * sinf(2.0f * M_PI * phase * fk / f0);
        }
        buf[i] = val;
    }
    return buf_len;
}

/* ================================================================
 *  Dispatcher
 * ================================================================ */
uint16_t WaveGen_Generate(float *buf, uint16_t buf_len, WaveType_t type,
                          uint8_t cycles, MeasureResult_t *r, PeakResult_t *p)
{
    switch (type) {
    case WAVE_SINE:     return WaveGen_Sine(buf, buf_len, cycles, r);
    case WAVE_SQUARE:   return WaveGen_Square(buf, buf_len, cycles, r);
    case WAVE_TRIANGLE: return WaveGen_Triangle(buf, buf_len, cycles, r);
    case WAVE_MULTITONE: return WaveGen_Harmonic(buf, buf_len, cycles, r, p);
    default:
        break;
    }
    memset(buf, 0, buf_len * sizeof(float));
    return buf_len;
}
