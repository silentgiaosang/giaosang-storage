#include "measure.h"
#include "ad9220.h"
#include "arm_math.h"
#include "stdio.h"
#include "math.h"
#include "app_interface.h"
#include "tjc_screen.h"

/* Calibration: mV per ADC code at module input. */
#define MV_PER_CODE  2.0f

/* Multi-peak detection */
#define MAX_PEAKS             3
#define NOISE_MULT            5.0f
#define MIN_PEAK_SEPARATION   4

/* Magnitude-to-Vpp: Vpp_mV = mag × 8/N × MV_PER_CODE */
#define MAG_TO_VPP(m)  ((m) * 8.0f / (float32_t)AD9220_BUF_SIZE * MV_PER_CODE)

/* Waveform synthesis */
#define WAVE_PTS  600   /* fixed points per screen frame */

typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_MULTITONE,
} WaveType_t;

AD9220_Result g_result;
PeakResult_t  g_peaks;

/* --- FFT --- */
static arm_rfft_fast_instance_f32 fft_instance;
static float32_t fft_in[AD9220_BUF_SIZE];
static float32_t fft_out[AD9220_BUF_SIZE];
static float32_t hanning_window[AD9220_BUF_SIZE];
static float32_t magnitude[AD9220_BUF_SIZE / 2];

/* --- Moving average --- */
static float32_t ma_buf[AD9220_BUF_SIZE];

/* --- Saved for peak analysis --- */
static float32_t fund_bin_interp = 0.0f;
static float32_t fund_mag        = 0.0f;
static uint32_t  last_sr_hz      = 0;

/* --- Trigger --- */
static volatile uint8_t trigger_pending = 0;

/* --- State machine --- */
typedef enum {
    STATE_IDLE,
    STATE_COARSE_WAIT,
    STATE_FINE_WAIT,
    STATE_FINE_PROCESS,
} MeasureState;

static MeasureState state = STATE_IDLE;
static MeasureState prev_state = STATE_IDLE;
static uint32_t last_tick = 0;
static uint32_t wait_start = 0;

/* --- Forward declarations --- */
static void build_hanning_window(void);
static float32_t find_dominant_freq(const uint16_t *raw, uint32_t sample_rate_hz);
static float32_t compute_vpp_mv(const uint16_t *raw);
static float32_t compute_vrms_mv(const uint16_t *raw);
static AD9220_Tier select_tier(float32_t freq_khz);
static void analyze_peaks(void);
static WaveType_t detect_wave_type(void);
static uint16_t synthesize_waveform(float *buf, uint16_t n_cycles, WaveType_t type);

/* ================================================================ */

void Measure_Init(void)
{
    AD9220_Init();
    build_hanning_window();
    arm_rfft_fast_init_f32(&fft_instance, AD9220_BUF_SIZE);
    g_result.freq_khz = 0.0f;
    g_result.vpp_mv   = 0.0f;
    memset(&g_peaks, 0, sizeof(g_peaks));
    printf("Measure Init OK\r\n");
}

void Measure_Trigger(void)
{
    trigger_pending = 1;
}

void Measure_Process(void)
{
    float32_t freq_khz;

    if (state != prev_state) {
        wait_start = HAL_GetTick();
        prev_state = state;
    }

    switch (state) {

    case STATE_IDLE:
        if (trigger_pending || (HAL_GetTick() - last_tick >= 1000)) {
            trigger_pending = 0;
            AD9220_Start(AD9220_TIER_MID);
            state = STATE_COARSE_WAIT;
        }
        break;

    case STATE_COARSE_WAIT:
        if (AD9220_DataReady()) {
            AD9220_ClearReady();
            freq_khz = find_dominant_freq(ad9220_buffer, 2000000);
            AD9220_Tier tier = select_tier(freq_khz);
            AD9220_Start(tier);
            state = STATE_FINE_WAIT;
        } else if (HAL_GetTick() - wait_start > 1000) {
            AD9220_DebugDump();
            wait_start = HAL_GetTick();
            state = STATE_IDLE;
        }
        break;

    case STATE_FINE_WAIT:
        if (AD9220_DataReady()) {
            AD9220_ClearReady();

            /* Time-domain: Vpp + Vrms */
            g_result.vpp_mv  = compute_vpp_mv(ad9220_buffer);
            g_result.vrms_mv = compute_vrms_mv(ad9220_buffer);

            /* Sample rate from TIM1 ARR */
            uint32_t sr;
            uint32_t arr = TIM1->ARR;
            if (arr >= 800)       sr = 200000;
            else if (arr >= 80)   sr = 2000000;
            else                  sr = 9882000;

            /* FFT → frequency + peaks */
            g_result.freq_khz = find_dominant_freq(ad9220_buffer, sr);
            analyze_peaks();

            /* ---- build MeasureResult_t for screen ---- */
            MeasureResult_t res;
            memset(&res, 0, sizeof(res));
            res.vpp_mv      = g_result.vpp_mv;
            res.vrms_mv     = g_result.vrms_mv;
            res.f_base_hz   = g_result.freq_khz * 1000.0f;
            res.harmonic_count = g_peaks.count;
            for (int i = 0; i < g_peaks.count; i++) {
                res.freq_hz[i] = g_peaks.freq_hz[i];
                res.amp_mv[i]  = g_peaks.vpp_mv[i];
            }

            /* Synthesize waveform for display */
            WaveType_t wtype = detect_wave_type();
            uint16_t n_cycles = (App_GetCycle() == CYC_1) ? 1 : 3;
            uint16_t wave_len = synthesize_waveform(ma_buf, n_cycles, wtype);

            /* Scale to screen Y, then draw directly (bypass malloc in main_app) */
            {
                float vmin = ma_buf[0], vmax = ma_buf[0];
                for (uint16_t i = 1; i < wave_len; i++) {
                    if (ma_buf[i] < vmin) vmin = ma_buf[i];
                    if (ma_buf[i] > vmax) vmax = ma_buf[i];
                }
                float range = vmax - vmin;
                if (range < 1.0f) range = 1.0f;
                float margin = range * 0.1f;
                range += margin * 2.0f;
                vmin -= margin;

                static uint16_t scr_buf[WAVE_PTS];
                for (uint16_t i = 0; i < wave_len; i++) {
                    float v = (ma_buf[i] - vmin) / range;
                    uint16_t y = (uint16_t)(v * GRAPH_H);
                    if (y > GRAPH_H) y = GRAPH_H;
                    scr_buf[i] = y;
                }
                TJC_DrawWaveform(scr_buf, wave_len);
            }

            /* Submit text params to screen */
            printf("Wave: type=%d cyc=%d len=%d\r\n",
                   (int)wtype, (int)n_cycles, (int)wave_len);
            App_SubmitResult(&res, ma_buf, wave_len, sr,
                             magnitude, AD9220_BUF_SIZE / 2);
            printf("Submit done\r\n");

            state = STATE_FINE_PROCESS;
        } else if (HAL_GetTick() - wait_start > 1000) {
            printf("DMA timeout!\r\n");
            AD9220_DebugDump();
            wait_start = HAL_GetTick();
            state = STATE_IDLE;
        }
        break;

    case STATE_FINE_PROCESS: {
        static uint8_t print_cnt = 0;
        if (++print_cnt >= 5) {
            printf("Freq: %.3f kHz  Vpp: %.0f mV  Vrms: %.0f mV\r\n",
                   g_result.freq_khz, g_result.vpp_mv, g_result.vrms_mv);
            print_cnt = 0;
        }
        last_tick = HAL_GetTick();
        state = STATE_IDLE;
        break;
    }
    }
}

/* ================================================================ */

static void build_hanning_window(void)
{
    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        hanning_window[i] = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (AD9220_BUF_SIZE - 1)));
    }
}

static float32_t find_dominant_freq(const uint16_t *raw, uint32_t sample_rate_hz)
{
    float32_t dc = 0.0f;
    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        float32_t v = (float32_t)(raw[i] & 0x0FFF);
        fft_in[i] = v;
        dc += v;
    }
    dc /= (float32_t)AD9220_BUF_SIZE;

    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        fft_in[i] = (fft_in[i] - dc) * hanning_window[i];
    }

    arm_rfft_fast_f32(&fft_instance, fft_in, fft_out, 0);

    arm_cmplx_mag_f32(&fft_out[2], &magnitude[1], AD9220_BUF_SIZE / 2 - 1);
    magnitude[0] = (fft_out[0] >= 0.0f) ? fft_out[0] : -fft_out[0];

    uint32_t peak_bin = 1;
    float32_t peak_val = magnitude[1];
    for (uint32_t i = 2; i < AD9220_BUF_SIZE / 2; i++) {
        if (magnitude[i] > peak_val) {
            peak_val = magnitude[i];
            peak_bin = i;
        }
    }

    float32_t bin_f = (float32_t)peak_bin;
    if (peak_bin >= 1 && peak_bin < AD9220_BUF_SIZE / 2 - 1) {
        float32_t a = magnitude[peak_bin - 1];
        float32_t b = magnitude[peak_bin];
        float32_t c = magnitude[peak_bin + 1];
        float32_t denom = a - 2.0f * b + c;
        if (denom != 0.0f) {
            float32_t delta = 0.5f * (a - c) / denom;
            if (delta > -0.5f && delta < 0.5f) bin_f += delta;
        }
    }

    fund_bin_interp = bin_f;
    fund_mag        = magnitude[peak_bin];
    last_sr_hz      = sample_rate_hz;

    return bin_f * (float32_t)sample_rate_hz / (float32_t)AD9220_BUF_SIZE / 1000.0f;
}

static float32_t compute_vpp_mv(const uint16_t *raw)
{
    float32_t dc = 0.0f;
    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        uint16_t code = raw[i] & 0x0FFF;
        ma_buf[i] = (float32_t)code;
        dc += (float32_t)code;
    }
    dc /= (float32_t)AD9220_BUF_SIZE;

    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        ma_buf[i] -= dc;
    }

    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        float32_t sum = 0.0f;
        uint32_t n = 0;
        for (int32_t j = (int32_t)i - 2; j <= (int32_t)i + 2; j++) {
            if (j >= 0 && j < (int32_t)AD9220_BUF_SIZE) {
                sum += ma_buf[j];
                n++;
            }
        }
        fft_in[i] = sum / (float32_t)n;
    }

    float32_t vmax = fft_in[0], vmin = fft_in[0];
    for (uint32_t i = 1; i < AD9220_BUF_SIZE; i++) {
        if (fft_in[i] > vmax) vmax = fft_in[i];
        if (fft_in[i] < vmin) vmin = fft_in[i];
    }

    return (vmax - vmin) * MV_PER_CODE;
}

static float32_t compute_vrms_mv(const uint16_t *raw)
{
    /* Compute DC, then RMS of AC component in mV */
    float32_t dc = 0.0f;
    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        dc += (float32_t)(raw[i] & 0x0FFF);
    }
    dc /= (float32_t)AD9220_BUF_SIZE;

    float32_t sum_sq = 0.0f;
    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        float32_t ac = (float32_t)(raw[i] & 0x0FFF) - dc;
        sum_sq += ac * ac;
    }

    float32_t rms_codes = sqrtf(sum_sq / (float32_t)AD9220_BUF_SIZE);
    return rms_codes * MV_PER_CODE;
}

static AD9220_Tier select_tier(float32_t freq_khz)
{
    if (freq_khz < 50.0f)
        return AD9220_TIER_LOW;
    else if (freq_khz > 200.0f)
        return AD9220_TIER_HIGH;
    else
        return AD9220_TIER_MID;
}

/* ================================================================ */

static void analyze_peaks(void)
{
    uint32_t n_bins  = AD9220_BUF_SIZE / 2;
    float32_t bin_res = (float32_t)last_sr_hz / (float32_t)AD9220_BUF_SIZE;

    /* 2-pass noise floor */
    float32_t sum = 0.0f;
    for (uint32_t i = 1; i < n_bins; i++) sum += magnitude[i];
    float32_t mean1 = sum / (float32_t)(n_bins - 1);

    float32_t sum2 = 0.0f;
    uint32_t  cnt2 = 0;
    for (uint32_t i = 1; i < n_bins; i++) {
        if (magnitude[i] <= mean1 * 2.0f) { sum2 += magnitude[i]; cnt2++; }
    }
    float32_t noise_floor = (cnt2 > 100) ? (sum2 / (float32_t)cnt2) : mean1;
    float32_t threshold   = noise_floor * NOISE_MULT;

    /* Debug: show max magnitude overall */
    float32_t mag_max = 0.0f;
    uint32_t mag_max_bin = 0;
    for (uint32_t i = 1; i < n_bins; i++) {
        if (magnitude[i] > mag_max) { mag_max = magnitude[i]; mag_max_bin = i; }
    }
    printf("FFT max: bin=%lu mag=%.0f -> Vpp=%.0fmV\r\n",
           (unsigned long)mag_max_bin, (double)mag_max,
           (double)MAG_TO_VPP(mag_max));

    /* Collect all local maxima above threshold, sort by magnitude, take top 3 */
    #define MAX_CANDIDATES 64
    typedef struct { float mag; float freq; int32_t bin; } Cand;
    static Cand cand[MAX_CANDIDATES];
    uint8_t nc = 0;

    for (uint32_t i = 5; i < n_bins - 1 && nc < MAX_CANDIDATES; i++) {
        if (magnitude[i] <= magnitude[i - 1] || magnitude[i] <= magnitude[i + 1])
            continue;
        if (magnitude[i] <= threshold)
            continue;

        /* Parabolic interpolation */
        float32_t a = magnitude[i - 1], b = magnitude[i], c = magnitude[i + 1];
        float32_t bin_f = (float32_t)i;
        float32_t denom = a - 2.0f * b + c;
        if (denom != 0.0f) {
            float32_t d = 0.5f * (a - c) / denom;
            if (d > -0.5f && d < 0.5f) bin_f += d;
        }

        cand[nc].mag  = magnitude[i];
        cand[nc].freq = bin_f * bin_res;
        cand[nc].bin  = i;
        nc++;
        i += MIN_PEAK_SEPARATION;
    }

    /* Bubble sort by magnitude descending */
    for (int j = 0; j < (int)nc - 1; j++) {
        for (int k = j + 1; k < (int)nc; k++) {
            if (cand[k].mag > cand[j].mag) {
                Cand t = cand[j]; cand[j] = cand[k]; cand[k] = t;
            }
        }
    }

    /* Take top MAX_PEAKS, apply scalloping correction */
    memset(&g_peaks, 0, sizeof(g_peaks));
    for (int j = 0; j < MAX_PEAKS && j < (int)nc; j++) {
        /* Amplitude correction for Hanning scalloping loss */
        int32_t bi = cand[j].bin;
        float32_t delta_f = cand[j].freq / bin_res - (float32_t)bi;
        if (delta_f > 0.5f) delta_f -= 1.0f;
        if (delta_f < -0.5f) delta_f += 1.0f;
        float32_t abs_d = (delta_f >= 0.0f) ? delta_f : -delta_f;
        float32_t sinc_corr;
        if (abs_d < 0.001f) {
            sinc_corr = 1.0f;
        } else {
            sinc_corr = sinf(PI * abs_d) / (PI * abs_d) / (1.0f - abs_d * abs_d);
        }
        if (sinc_corr < 0.3f) sinc_corr = 0.3f;
        float32_t mag_corr = cand[j].mag / sinc_corr;

        g_peaks.freq_hz[j] = cand[j].freq;
        g_peaks.vpp_mv[j]  = MAG_TO_VPP(mag_corr);
        printf("  peak[%d]: bin=%ld freq=%.0fHz Vpp=%.0fmV\r\n",
               j, (long)bi, (double)cand[j].freq, (double)g_peaks.vpp_mv[j]);
    }
    g_peaks.count = (nc > MAX_PEAKS) ? MAX_PEAKS : nc;
    printf("peaks: %d (thr=%.0f cand=%d)\r\n", (int)g_peaks.count, (double)threshold, (int)nc);
}

/* ================================================================ */

static WaveType_t detect_wave_type(void)
{
    if (g_peaks.count == 1)
        return WAVE_SINE;

    float f0 = g_peaks.freq_hz[0];
    if (f0 < 1.0f) return WAVE_SINE;

    /* Check 2nd peak at 3× fundamental */
    if (g_peaks.count >= 2) {
        float r2 = g_peaks.freq_hz[1] / f0;
        if (fabsf(r2 - 3.0f) > 0.15f) return WAVE_MULTITONE;  /* not harmonic */
    }

    /* Check 3rd peak at 5× fundamental */
    if (g_peaks.count >= 3) {
        float r3 = g_peaks.freq_hz[2] / f0;
        if (fabsf(r3 - 5.0f) > 0.25f) return WAVE_MULTITONE;
    }

    /* Harmonic pattern detected — check amplitudes */
    float a1 = g_peaks.vpp_mv[0];
    if (a1 < 1.0f) return WAVE_SINE;

    /* Compare with square wave: A3 ≈ A1/3, A5 ≈ A1/5 */
    float sq_err = 0.0f;
    if (g_peaks.count >= 2) {
        float e2 = fabsf(g_peaks.vpp_mv[1] - a1 / 3.0f) / (a1 / 3.0f + 0.1f);
        sq_err += e2;
    }
    if (g_peaks.count >= 3) {
        float e3 = fabsf(g_peaks.vpp_mv[2] - a1 / 5.0f) / (a1 / 5.0f + 0.1f);
        sq_err += e3;
    }

    /* Compare with triangle wave: A3 ≈ A1/9, A5 ≈ A1/25 */
    float tri_err = 0.0f;
    if (g_peaks.count >= 2) {
        float e2 = fabsf(g_peaks.vpp_mv[1] - a1 / 9.0f) / (a1 / 9.0f + 0.1f);
        tri_err += e2;
    }
    if (g_peaks.count >= 3) {
        float e3 = fabsf(g_peaks.vpp_mv[2] - a1 / 25.0f) / (a1 / 25.0f + 0.1f);
        tri_err += e3;
    }

    if (sq_err < tri_err && sq_err < 1.0f)
        return WAVE_SQUARE;
    if (tri_err < sq_err && tri_err < 1.0f)
        return WAVE_TRIANGLE;

    return WAVE_MULTITONE;
}

static uint16_t synthesize_waveform(float *buf, uint16_t n_cycles, WaveType_t type)
{
    float f0   = g_peaks.freq_hz[0];
    float a1_2 = g_peaks.vpp_mv[0] * 0.5f;   /* amplitude (half Vpp) */
    float pts_per_cycle = (float)WAVE_PTS / (float)n_cycles;

    for (uint16_t i = 0; i < WAVE_PTS; i++) {
        float phase = (float)i / pts_per_cycle;   /* 0 ~ n_cycles */
        float val = 0.0f;

        switch (type) {
        case WAVE_SINE:
            val = sinf(2.0f * PI * phase);
            break;

        case WAVE_SQUARE:
            val = sinf(2.0f * PI * phase) >= 0.0f ? 1.0f : -1.0f;
            break;

        case WAVE_TRIANGLE: {
            float t = phase - floorf(phase);   /* 0~1 per cycle */
            if (t < 0.25f)       val = 4.0f * t;
            else if (t < 0.75f)  val = 2.0f - 4.0f * t;
            else                 val = 4.0f * t - 4.0f;
            break;
        }

        case WAVE_MULTITONE:
            for (int k = 0; k < g_peaks.count; k++) {
                float fk = g_peaks.freq_hz[k];
                float ak = g_peaks.vpp_mv[k] * 0.5f;
                val += sinf(2.0f * PI * phase * fk / f0) * ak;
            }
            val /= a1_2;   /* normalize to fundamental amplitude */
            break;
        }

        buf[i] = val * a1_2;
    }

    return WAVE_PTS;
}
