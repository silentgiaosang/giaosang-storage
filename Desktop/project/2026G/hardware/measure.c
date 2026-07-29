#include "measure.h"
#include "ad9220.h"
#include "arm_math.h"
#include "stdio.h"
#include "math.h"

/* Calibration: mV per ADC code at module input. */
#define MV_PER_CODE  2.0f

/* Multi-peak detection */
#define MAX_PEAKS             3
#define NOISE_MULT            5.0f
#define MIN_PEAK_SEPARATION   4

/* Magnitude-to-Vpp: Vpp_mV = mag × 8/N × MV_PER_CODE */
#define MAG_TO_VPP(m)  ((m) * 8.0f / (float32_t)AD9220_BUF_SIZE * MV_PER_CODE)

/* ================================================================
 *  Globals — measurement results (read by main_app via GetLatest)
 * ================================================================ */
AD9220_Result g_result;
PeakResult_t  g_peaks;

static MeasureResult_t g_latest_result;
static PeakResult_t    g_latest_peaks;
static WaveType_t      g_latest_type = WAVE_SINE;
static volatile uint8_t g_data_ready = 0;

/* ================================================================
 *  FFT internals
 * ================================================================ */
static arm_rfft_fast_instance_f32 fft_instance;
static float32_t fft_in[AD9220_BUF_SIZE];
static float32_t fft_out[AD9220_BUF_SIZE];
static float32_t hanning_window[AD9220_BUF_SIZE];
static float32_t magnitude[AD9220_BUF_SIZE / 2];

/* Vpp scratch buffer (reuse fft_in as moving-average output) */
static float32_t ma_buf[AD9220_BUF_SIZE];

/* Peak analysis globals */
static float32_t fund_bin_interp = 0.0f;
static float32_t fund_mag        = 0.0f;
static uint32_t  last_sr_hz      = 0;

/* ================================================================
 *  Auto-loop timing
 * ================================================================ */
#define MEASURE_INTERVAL_MS  1000
static uint32_t last_measure_tick = 0;
static uint8_t  coarse_running = 0;       /* 0=idle, 1=waiting coarse DMA, 2=waiting fine DMA */

/* Forward declarations */
static void build_hanning_window(void);
static float32_t find_dominant_freq(const uint16_t *raw, uint32_t sample_rate_hz);
static float32_t compute_vpp_mv(const uint16_t *raw);
static float32_t compute_vrms_mv(const uint16_t *raw);
static AD9220_Tier select_tier(float32_t freq_khz);
static void analyze_peaks(void);
static WaveType_t detect_wave_type(void);

/* ================================================================
 *  Public API
 * ================================================================ */

void Measure_Init(void)
{
    AD9220_Init();
    build_hanning_window();
    arm_rfft_fast_init_f32(&fft_instance, AD9220_BUF_SIZE);
    memset(&g_result, 0, sizeof(g_result));
    memset(&g_peaks, 0, sizeof(g_peaks));
    memset(&g_latest_result, 0, sizeof(g_latest_result));
    memset(&g_latest_peaks, 0, sizeof(g_latest_peaks));
    printf("Measure Init OK\r\n");
}

uint8_t Measure_DataReady(void)
{
    if (g_data_ready) {
        g_data_ready = 0;
        return 1;
    }
    return 0;
}

void Measure_GetLatest(MeasureResult_t *result, PeakResult_t *peaks, WaveType_t *type)
{
    if (result) memcpy(result, &g_latest_result, sizeof(MeasureResult_t));
    if (peaks)  memcpy(peaks,  &g_latest_peaks,  sizeof(PeakResult_t));
    if (type)   *type = g_latest_type;
}

/* ================================================================
 *  Main process — called from main loop, auto-timed
 * ================================================================ */
void Measure_Process(void)
{
    uint32_t now = HAL_GetTick();

    switch (coarse_running) {

    case 0:  /* Idle — wait for interval */
        if (now - last_measure_tick >= MEASURE_INTERVAL_MS) {
            AD9220_Start(AD9220_TIER_MID);
            coarse_running = 1;
        }
        break;

    case 1:  /* Waiting for coarse DMA */
        if (AD9220_DataReady()) {
            AD9220_ClearReady();
            float32_t freq_khz = find_dominant_freq(ad9220_buffer, 2000000);
            AD9220_Tier tier = select_tier(freq_khz);
            AD9220_Start(tier);
            coarse_running = 2;
        } else if (now - last_measure_tick > MEASURE_INTERVAL_MS + 1000) {
            /* Timeout */
            printf("Coarse DMA timeout\r\n");
            AD9220_DebugDump();
            coarse_running = 0;
            last_measure_tick = now;
        }
        break;

    case 2:  /* Waiting for fine DMA */
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

            /* Wave type detection */
            WaveType_t wtype = detect_wave_type();

            /* Store latest results atomically */
            g_latest_result.vpp_mv      = g_result.vpp_mv;
            g_latest_result.vrms_mv     = g_result.vrms_mv;
            g_latest_result.f_base_hz   = g_result.freq_khz * 1000.0f;
            g_latest_result.harmonic_count = g_peaks.count;
            for (int i = 0; i < g_peaks.count && i < 3; i++) {
                g_latest_result.freq_hz[i] = g_peaks.freq_hz[i];
                g_latest_result.amp_mv[i]  = g_peaks.vpp_mv[i];
            }
            g_latest_peaks = g_peaks;
            g_latest_type  = wtype;
            g_data_ready   = 1;

            /* Debug print */
            printf("Freq: %.3f kHz  Vpp: %.0f mV  Vrms: %.0f mV  Type: %d\r\n",
                   g_result.freq_khz, g_result.vpp_mv, g_result.vrms_mv, (int)wtype);

            coarse_running = 0;
            last_measure_tick = now;
        } else if (now - last_measure_tick > MEASURE_INTERVAL_MS + 3000) {
            printf("Fine DMA timeout\r\n");
            AD9220_DebugDump();
            coarse_running = 0;
            last_measure_tick = now;
        }
        break;
    }
}

/* ================================================================
 *  Hanning window
 * ================================================================ */
static void build_hanning_window(void)
{
    for (uint32_t i = 0; i < AD9220_BUF_SIZE; i++) {
        hanning_window[i] = 0.5f * (1.0f - arm_cos_f32(2.0f * PI * i / (AD9220_BUF_SIZE - 1)));
    }
}

/* ================================================================
 *  FFT → dominant frequency (kHz), with parabolic interpolation
 * ================================================================ */
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

    /* Find peak bin (skip DC) */
    uint32_t peak_bin = 1;
    float32_t peak_val = magnitude[1];
    for (uint32_t i = 2; i < AD9220_BUF_SIZE / 2; i++) {
        if (magnitude[i] > peak_val) {
            peak_val = magnitude[i];
            peak_bin = i;
        }
    }

    /* Parabolic interpolation */
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
    fund_mag        = peak_val;
    last_sr_hz      = sample_rate_hz;

    return bin_f * (float32_t)sample_rate_hz / (float32_t)AD9220_BUF_SIZE / 1000.0f;
}

/* ================================================================
 *  Vpp (mV) from raw ADC data
 * ================================================================ */
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

    /* 5-point moving average */
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

/* ================================================================
 *  Vrms (mV) from raw ADC data
 * ================================================================ */
static float32_t compute_vrms_mv(const uint16_t *raw)
{
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

/* ================================================================
 *  Auto-select sample rate tier
 * ================================================================ */
static AD9220_Tier select_tier(float32_t freq_khz)
{
    if (freq_khz < 50.0f)
        return AD9220_TIER_LOW;
    else if (freq_khz > 200.0f)
        return AD9220_TIER_HIGH;
    else
        return AD9220_TIER_MID;
}

/* ================================================================
 *  Multi-peak harmonic analysis
 * ================================================================ */
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

    /* Collect local maxima above threshold */
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
        cand[nc].bin  = (int32_t)i;
        nc++;
        i += MIN_PEAK_SEPARATION;
    }

    /* Sort by magnitude descending (bubble sort, small N) */
    for (int j = 0; j < (int)nc - 1; j++) {
        for (int k = j + 1; k < (int)nc; k++) {
            if (cand[k].mag > cand[j].mag) {
                Cand t = cand[j]; cand[j] = cand[k]; cand[k] = t;
            }
        }
    }

    /* Take top MAX_PEAKS with Hanning scalloping correction */
    memset(&g_peaks, 0, sizeof(g_peaks));
    for (int j = 0; j < MAX_PEAKS && j < (int)nc; j++) {
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
    }
    g_peaks.count = (nc > MAX_PEAKS) ? MAX_PEAKS : nc;
}

/* ================================================================
 *  Wave type detection
 * ================================================================ */
static WaveType_t detect_wave_type(void)
{
    if (g_peaks.count == 1)
        return WAVE_SINE;

    float f0 = g_peaks.freq_hz[0];
    if (f0 < 1.0f) return WAVE_SINE;

    /* Check 2nd peak at 3× fundamental */
    if (g_peaks.count >= 2) {
        float r2 = g_peaks.freq_hz[1] / f0;
        if (fabsf(r2 - 3.0f) > 0.15f) return WAVE_HARMONIC;
    }

    /* Check 3rd peak at 5× fundamental */
    if (g_peaks.count >= 3) {
        float r3 = g_peaks.freq_hz[2] / f0;
        if (fabsf(r3 - 5.0f) > 0.25f) return WAVE_HARMONIC;
    }

    /* Harmonic pattern — compare amplitudes */
    float a1 = g_peaks.vpp_mv[0];
    if (a1 < 1.0f) return WAVE_SINE;

    /* Square wave: A3 ≈ A1/3, A5 ≈ A1/5 */
    float sq_err = 0.0f;
    if (g_peaks.count >= 2) {
        float e2 = fabsf(g_peaks.vpp_mv[1] - a1 / 3.0f) / (a1 / 3.0f + 0.1f);
        sq_err += e2;
    }
    if (g_peaks.count >= 3) {
        float e3 = fabsf(g_peaks.vpp_mv[2] - a1 / 5.0f) / (a1 / 5.0f + 0.1f);
        sq_err += e3;
    }

    /* Triangle wave: A3 ≈ A1/9, A5 ≈ A1/25 */
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

    return WAVE_HARMONIC;
}
