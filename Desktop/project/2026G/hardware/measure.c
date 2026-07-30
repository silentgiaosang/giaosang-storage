#include "measure.h"
#include "ad9220.h"
#include "arm_math.h"
#include "stdio.h"
#include "math.h"
#include "app_interface.h"
#include "tjc_screen.h"

/* Calibration: mV per ADC code at module input. */
#define MV_PER_CODE  4.82f    /* ±5V → 10Vpp → 模组前端2倍衰减? 待校准 */

/* Multi-peak detection */
#define MAX_PEAKS             3
#define NOISE_MULT            8.0f
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
static volatile uint8_t g_button_triggered = 0;

/* --- State machine --- */
typedef enum {
    STATE_IDLE,
    STATE_SCAN_WAIT,
    STATE_SCAN_DONE,
} MeasureState;

static MeasureState state = STATE_IDLE;
static uint32_t last_tick = 0;
static uint32_t wait_start = 0;

/* 三档扫描: 每个采样率都测, 取峰值最多的 */
static uint8_t        scan_tier_idx = 0;
static PeakResult_t   best_peaks;
static AD9220_Result  best_result;
static uint8_t        best_peak_count = 0;
static uint32_t       best_sr = 0;

/* 3轮累积: 减少跳动, 每3轮才输出一次到屏幕 */
#define  OUTPUT_ROUNDS  3
static uint8_t        round_cnt = 0;
static PeakResult_t   round_peaks[OUTPUT_ROUNDS];
static AD9220_Result  round_results[OUTPUT_ROUNDS];
static uint32_t       round_sr[OUTPUT_ROUNDS];
static uint8_t        round_peak_counts[OUTPUT_ROUNDS];

/* --- Forward declarations --- */
static void build_hanning_window(void);
static float32_t find_dominant_freq(const uint16_t *raw, uint32_t sample_rate_hz);
static float32_t compute_vpp_mv(const uint16_t *raw);
static float32_t compute_vrms_mv(const uint16_t *raw);
static void analyze_peaks(void);
static WaveType_t detect_wave_type(void);
static void print_wave_type(WaveType_t type);
static uint16_t synthesize_waveform(float *buf, uint16_t n_cycles, WaveType_t type);
static uint16_t synthesize_harmonic(float *buf, uint16_t n_cycles);

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
    g_button_triggered = 1;
}

void Measure_Process(void)
{
    static const AD9220_Tier tier_list[3] = {
        AD9220_TIER_LOW, AD9220_TIER_MID, AD9220_TIER_HIGH
    };
    static const uint32_t tier_sr[3] = { 200000, 2000000, 9882000 };

    switch (state) {

    case STATE_IDLE:
        if (trigger_pending || (HAL_GetTick() - last_tick >= 3000)) {
            trigger_pending = 0;
            scan_tier_idx   = 0;
            best_peak_count = 0;
            memset(&best_peaks,  0, sizeof(best_peaks));
            memset(&best_result, 0, sizeof(best_result));
            best_sr = tier_sr[0];
            AD9220_Start(tier_list[0]);
            wait_start = HAL_GetTick();
            state = STATE_SCAN_WAIT;
        }
        break;

    case STATE_SCAN_WAIT:
        if (AD9220_DataReady()) {
            AD9220_ClearReady();
            uint32_t sr = tier_sr[scan_tier_idx];

            /* Time-domain */
            float vpp  = compute_vpp_mv(ad9220_buffer);
            float vrms = compute_vrms_mv(ad9220_buffer);
            float f_khz = find_dominant_freq(ad9220_buffer, sr);
            analyze_peaks();

            printf("Tier%d sr=%lu f=%.3fkHz peaks=%d\r\n",
                   (int)scan_tier_idx, (unsigned long)sr,
                   (double)f_khz, (int)g_peaks.count);

            /* Keep best (most peaks; tie-break: higher Vpp) */
            if (g_peaks.count > best_peak_count ||
                (g_peaks.count == best_peak_count && vpp > best_result.vpp_mv)) {
                best_peak_count = g_peaks.count;
                best_peaks      = g_peaks;
                best_result.freq_khz = f_khz;
                best_result.vpp_mv   = vpp;
                best_result.vrms_mv  = vrms;
                best_sr          = sr;
            }

            /* Next tier or done */
            scan_tier_idx++;
            if (scan_tier_idx < 3) {
                AD9220_Start(tier_list[scan_tier_idx]);
                wait_start = HAL_GetTick();
            } else {
                /* Restore best */
                g_peaks  = best_peaks;
                g_result = best_result;
                state = STATE_SCAN_DONE;
            }
        } else if (HAL_GetTick() - wait_start > 1000) {
            printf("DMA timeout tier%d!\r\n", (int)scan_tier_idx);
            AD9220_DebugDump();
            scan_tier_idx++;
            if (scan_tier_idx < 3) {
                AD9220_Start(tier_list[scan_tier_idx]);
            } else {
                state = STATE_SCAN_DONE;
            }
            wait_start = HAL_GetTick();
        }
        break;

    case STATE_SCAN_DONE:
        /* Save this round's best */
        round_peaks[round_cnt]       = best_peaks;
        round_results[round_cnt]     = best_result;
        round_sr[round_cnt]          = best_sr;
        round_peak_counts[round_cnt] = best_peak_count;
        round_cnt++;

        if (round_cnt < OUTPUT_ROUNDS) {
            /* Start next round immediately */
            printf("Round %d/3 done, starting next...\r\n", (int)round_cnt);
            scan_tier_idx   = 0;
            best_peak_count = 0;
            memset(&best_peaks,  0, sizeof(best_peaks));
            memset(&best_result, 0, sizeof(best_result));
            AD9220_Start(AD9220_TIER_LOW);
            wait_start = HAL_GetTick();
            state = STATE_SCAN_WAIT;
        } else {
            /* All 3 rounds done — pick the best (most peaks; tie: higher Vpp) */
            uint8_t pick = 0;
            for (uint8_t r = 1; r < OUTPUT_ROUNDS; r++) {
                if (round_peak_counts[r] > round_peak_counts[pick] ||
                    (round_peak_counts[r] == round_peak_counts[pick] &&
                     round_results[r].vpp_mv > round_results[pick].vpp_mv)) {
                    pick = r;
                }
            }
            g_peaks  = round_peaks[pick];
            g_result = round_results[pick];
            best_sr  = round_sr[pick];

            /* Build result */
            MeasureResult_t res;
            memset(&res, 0, sizeof(res));
            res.vpp_mv      = g_result.vpp_mv;
            res.vrms_mv     = g_result.vrms_mv;
            res.f_base_hz   = g_peaks.freq_hz[0];
            res.harmonic_count = g_peaks.count;
            for (int i = 0; i < g_peaks.count; i++) {
                res.freq_hz[i] = g_peaks.freq_hz[i];
                /* SINE: U1 = Vpp (时域直测); MULTITONE: 用FFT幅值 */
                if (g_peaks.count == 1 && i == 0)
                    res.amp_mv[i] = res.vpp_mv;
                else
                    res.amp_mv[i] = g_peaks.vpp_mv[i];
            }

            /* Type → serial */
            WaveType_t wtype = detect_wave_type();
            print_wave_type(wtype);

            /* Button → waveform */
            if (g_button_triggered) {
                g_button_triggered = 0;

                uint16_t n_cycles = 3;
                uint16_t wave_len;
                if (wtype == WAVE_MULTITONE) {
                    wave_len = synthesize_harmonic(ma_buf, n_cycles);
                } else {
                    wave_len = synthesize_waveform(ma_buf, n_cycles, wtype);
                }

                float vmin = ma_buf[0], vmax = ma_buf[0];
                for (uint16_t i = 1; i < wave_len; i++) {
                    if (ma_buf[i] < vmin) vmin = ma_buf[i];
                    if (ma_buf[i] > vmax) vmax = ma_buf[i];
                }
                float vrange = vmax - vmin;
                if (vrange < 1.0f) vrange = 1.0f;
                float vmargin = vrange * 0.1f;
                vrange += vmargin * 2.0f;
                vmin -= vmargin;

                static uint16_t scr_buf[WAVE_PTS];
                for (uint16_t i = 0; i < wave_len; i++) {
                    float v = (ma_buf[i] - vmin) / vrange;
                    uint16_t y = (uint16_t)(v * 255.0f);
                    if (y > 255) y = 255;
                    scr_buf[i] = y;
                }
                uint16_t show_len = (App_GetCycle() == CYC_1) ? (wave_len / 3) : wave_len;
                TJC_DrawWaveform(scr_buf, show_len);

                printf("Wave: %s sr=%lu cyc=%d len=%d\r\n",
                       (wtype == WAVE_SINE) ? "SINE" : "MULTITONE",
                       (unsigned long)best_sr, (int)n_cycles, (int)show_len);
            }

            printf("Screen: f1=%.1f f2=%.1f f3=%.1f U1=%.0f U2=%.0f U3=%.0f U2/U1=%.3f U3/U1=%.3f cnt=%d\r\n",
                   (double)res.f_base_hz,
                   (double)(res.harmonic_count >= 2 ? res.freq_hz[1] : 0),
                   (double)(res.harmonic_count >= 3 ? res.freq_hz[2] : 0),
                   (double)res.amp_mv[0],
                   (double)(res.harmonic_count >= 2 ? res.amp_mv[1] : 0),
                   (double)(res.harmonic_count >= 3 ? res.amp_mv[2] : 0),
                   (double)(res.harmonic_count >= 2 && res.amp_mv[0] > 1 ? res.amp_mv[1] / res.amp_mv[0] : 0),
                   (double)(res.harmonic_count >= 3 && res.amp_mv[0] > 1 ? res.amp_mv[2] / res.amp_mv[0] : 0),
                   (int)res.harmonic_count);
            App_SubmitResult(&res, NULL, 0, best_sr, NULL, 0);

            /* Reset for next cycle */
            round_cnt = 0;
            last_tick = HAL_GetTick();
            state = STATE_IDLE;
        }
        break;
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

    /* Collect all local maxima above threshold */
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

    /* Bubble sort by frequency ascending (fundamental first) */
    for (int j = 0; j < (int)nc - 1; j++) {
        for (int k = j + 1; k < (int)nc; k++) {
            if (cand[k].freq < cand[j].freq) {
                Cand t = cand[j]; cand[j] = cand[k]; cand[k] = t;
            }
        }
    }

    /* Take top MAX_PEAKS, apply scalloping correction.
       Drop peaks below 5% of the strongest (noise floor guard) */
    memset(&g_peaks, 0, sizeof(g_peaks));
    float main_mag = 0.0f;
    for (int j = 0; j < (int)nc; j++) {
        if (cand[j].mag > main_mag) main_mag = cand[j].mag;
    }
    uint8_t peak_count = 0;
    for (int j = 0; j < MAX_PEAKS && j < (int)nc; j++) {
        if (j > 0 && cand[j].mag < main_mag * 0.05f) continue;
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
        peak_count++;
    }
    g_peaks.count = peak_count;
}

/* ================================================================ */

static void print_wave_type(WaveType_t type)
{
    if (type == WAVE_SINE) {
        printf("Type: SINE, f=%.3fkHz, Vpp=%.1fmV\r\n",
               (double)(g_peaks.freq_hz[0] / 1000.0),
               (double)g_peaks.vpp_mv[0]);
    } else {
        printf("Type: MULTITONE (%d harmonics)\r\n", g_peaks.count);
        for (int i = 0; i < g_peaks.count && i < 3; i++) {
            printf("  %d: f=%.2fkHz, Vpp=%.1fmV\r\n",
                   i + 1,
                   (double)(g_peaks.freq_hz[i] / 1000.0),
                   (double)g_peaks.vpp_mv[i]);
        }
    }
}

static WaveType_t detect_wave_type(void)
{
    if (g_peaks.count <= 1)
        return WAVE_SINE;

    /* 2 or 3 peaks → harmonic (MULTITONE) */
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

        val = sinf(2.0f * PI * phase);

        buf[i] = val * a1_2;
    }

    return WAVE_PTS;
}

/* ================================================================
 * 谐波重建: 用 FFT 峰值(2次或3次谐波)合成实际波形
 * 与理想方波/三角波无关 — 基于实测 freq 和 amp
 * ================================================================ */
static uint16_t synthesize_harmonic(float *buf, uint16_t n_cycles)
{
    float f0   = g_peaks.freq_hz[0];
    float pts_per_cycle = (float)WAVE_PTS / (float)n_cycles;

    for (uint16_t i = 0; i < WAVE_PTS; i++) {
        float phase = (float)i / pts_per_cycle;   /* 0 ~ n_cycles */
        float val = 0.0f;

        for (int k = 0; k < g_peaks.count; k++) {
            float fk = g_peaks.freq_hz[k];
            float ak = g_peaks.vpp_mv[k] * 0.5f;
            val += sinf(2.0f * PI * phase * fk / f0) * ak;
        }

        buf[i] = val;
    }

    return WAVE_PTS;
}
