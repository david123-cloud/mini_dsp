#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agc.h"
#include "biquad.h"
#include "dsp_error.h"
#include "ns.h"

#define RATE 48000
#define FRAMES 24000 /* 0.5 s */
#define PI 3.14159265358979323846

static float g_in[FRAMES];
static float g_out[FRAMES];
static int g_failures;

static void check(int condition, const char *message)
{
    if (condition) {
        printf("  [ok]   %s\n", message);
    } else {
        printf("  [FAIL] %s\n", message);
        g_failures++;
    }
}

/* The pipeline normally owns and frees the module object; a standalone test
 * has to release both the private state and the object itself. */
static void discard(struct dsp_module *module)
{
    if (module == NULL) {
        return;
    }
    if (module->ops != NULL && module->ops->destroy != NULL) {
        module->ops->destroy(module->priv);
    }
    free(module);
}

static void fill_tone(float amplitude, float frequency)
{
    int index;

    for (index = 0; index < FRAMES; ++index) {
        g_in[index] = amplitude * (float)sin(2.0 * PI * frequency * index / RATE);
    }
}

static double rms(const float *data, int from, int to)
{
    double sum = 0.0;
    int index;

    for (index = from; index < to; ++index) {
        sum += (double)data[index] * (double)data[index];
    }
    return sqrt(sum / (double)(to - from));
}

/* Runs one module over the buffer and reports the steady-state gain in dB. */
static double measure(const struct dsp_module *module, int settle)
{
    struct dsp_buffer in_view;
    struct dsp_buffer out_view;
    double before;
    double after;
    int index;

    for (index = 0; index < FRAMES; ++index) {
        g_out[index] = 0.0f;
    }
    in_view.data = g_in;
    in_view.frames = (uint32_t)FRAMES;
    in_view.channels = 1u;
    out_view.data = g_out;
    out_view.frames = (uint32_t)FRAMES;
    out_view.channels = 1u;

    if (module->ops->process(module->priv, &in_view, &out_view) != DSP_OK) {
        return -999.0;
    }
    before = rms(g_in, settle, FRAMES);
    after = rms(g_out, settle, FRAMES);
    if (before <= 0.0) {
        return -999.0;
    }
    return 20.0 * log10(after / before);
}

static struct dsp_module *make_biquad(enum biquad_type type, float fc, float q, float gain_db)
{
    struct biquad_config cfg;

    cfg.type = type;
    cfg.fc = fc;
    cfg.q = q;
    cfg.gain = gain_db;
    return biquad_module_create(&cfg, RATE);
}

static void test_equaliser(void)
{
    struct dsp_module *module;
    double gain;

    printf("biquad: peaking EQ\n");
    fill_tone(0.25f, 1000.0f);
    module = make_biquad(BIQUAD_PEAK, 1000.0f, 1.41f, 6.0f);
    check(module != NULL, "peak filter created");
    gain = measure(module, FRAMES / 2);
    printf("  +6 dB at 1 kHz measured %.2f dB\n", gain);
    check(gain > 5.0 && gain < 7.0, "peaking EQ boosts by the configured amount");
    discard(module);

    printf("biquad: low shelf\n");
    fill_tone(0.25f, 100.0f);
    module = make_biquad(BIQUAD_LOWSHELF, 500.0f, 0.70710678f, 9.0f);
    check(module != NULL, "low shelf created");
    gain = measure(module, FRAMES / 2);
    printf("  +9 dB below 500 Hz measured %.2f dB at 100 Hz\n", gain);
    check(gain > 8.0 && gain < 10.0, "low shelf boosts the low band");
    discard(module);

    printf("biquad: high shelf\n");
    fill_tone(0.25f, 8000.0f);
    module = make_biquad(BIQUAD_HIGHSHELF, 2000.0f, 0.70710678f, -9.0f);
    check(module != NULL, "high shelf created");
    gain = measure(module, FRAMES / 2);
    printf("  -9 dB above 2 kHz measured %.2f dB at 8 kHz\n", gain);
    check(gain > -10.0 && gain < -8.0, "high shelf cuts the high band");
    discard(module);

    printf("biquad: notch\n");
    fill_tone(0.25f, 1000.0f);
    module = make_biquad(BIQUAD_NOTCH, 1000.0f, 4.0f, 0.0f);
    check(module != NULL, "notch created");
    gain = measure(module, FRAMES / 2);
    printf("  notch at 1 kHz measured %.1f dB\n", gain);
    check(gain < -20.0, "notch rejects its centre frequency");
    discard(module);

    printf("biquad: configuration limits\n");
    check(make_biquad(BIQUAD_PEAK, 1000.0f, 1.0f, 100.0f) == NULL, "gain above +60 dB refused");
    check(make_biquad(BIQUAD_LOWSHELF, 30000.0f, 1.0f, 3.0f) == NULL, "fc above Nyquist refused");
}

static void test_agc(void)
{
    struct agc_config cfg;
    struct dsp_module *module;
    double gain;
    double peak;

    printf("agc: quiet input is lifted towards the target\n");
    agc_config_default(&cfg);
    cfg.target_db = -6.0f;
    cfg.max_gain_db = 60.0f;
    fill_tone(0.02f, 500.0f);
    module = agc_module_create(&cfg, RATE);
    check(module != NULL, "agc created with an explicit config");
    gain = measure(module, FRAMES / 4);
    peak = rms(g_out, FRAMES / 4, FRAMES) * 1.41421356;
    printf("  gain %.1f dB, output peak %.3f (target %.3f)\n", gain, peak, 0.501);
    check(gain > 25.0, "quiet signal gets a large boost");
    check(peak > 0.40 && peak < 0.60, "output converges to the target level");
    discard(module);

    printf("agc: loud input is pulled down\n");
    fill_tone(0.80f, 500.0f);
    module = agc_module_create(&cfg, RATE);
    check(module != NULL, "agc created for the loud case");
    gain = measure(module, FRAMES / 4);
    peak = rms(g_out, FRAMES / 4, FRAMES) * 1.41421356;
    printf("  gain %.1f dB, output peak %.3f\n", gain, peak);
    check(gain < -3.0, "loud signal is attenuated");
    check(peak > 0.40 && peak < 0.65, "loud output also lands near the target");
    discard(module);

    printf("agc: configuration limits\n");
    agc_config_default(&cfg);
    cfg.noise_floor_db = 0.0f;
    check(agc_module_create(&cfg, RATE) == NULL, "floor above target refused");
    agc_config_default(&cfg);
    cfg.attack_ms = 0.0f;
    check(agc_module_create(&cfg, RATE) == NULL, "zero attack refused");
}

static void test_ns(void)
{
    struct ns_config cfg;
    struct dsp_module *module;
    double gain;

    printf("ns: quiet signal is attenuated\n");
    ns_config_default(&cfg);
    fill_tone(0.001f, 500.0f);
    module = ns_module_create(&cfg, RATE);
    check(module != NULL, "ns created");
    gain = measure(module, FRAMES / 2);
    printf("  quiet gain %.1f dB (floor %.1f dB)\n", gain, cfg.floor_db);
    check(gain < cfg.floor_db + 3.0f, "quiet signal sits at the floor");
    discard(module);

    printf("ns: loud signal passes\n");
    fill_tone(0.25f, 500.0f);
    module = ns_module_create(&cfg, RATE);
    check(module != NULL, "ns created for the loud case");
    gain = measure(module, FRAMES / 2);
    printf("  loud gain %.2f dB\n", gain);
    check(gain > -1.0 && gain < 0.5, "loud signal is essentially untouched");
    discard(module);

    printf("ns: configuration limits\n");
    ns_config_default(&cfg);
    check(cfg.knee_db > 0.0f, "default knee is non-zero");
    cfg.knee_db = 30.0f;
    check(ns_module_create(&cfg, RATE) == NULL, "knee above 24 dB refused");
}

int main(void)
{
    test_equaliser();
    test_agc();
    test_ns();

    printf("%s\n", g_failures == 0 ? "module selftest: ok" : "module selftest: FAILED");
    return g_failures == 0 ? 0 : 1;
}
