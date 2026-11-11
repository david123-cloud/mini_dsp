#ifndef MINI_DSP_AGC_H
#define MINI_DSP_AGC_H

#include <stdint.h>

#include "dsp_module.h"

/*
 * Feed-forward automatic gain control.
 *
 * Per frame: a peak envelope is tracked per channel (fast attack, slow
 * release), the loudest channel drives one shared gain so the stereo image
 * does not move, and that gain is smoothed separately (fast on the way down,
 * slow on the way up).
 *
 * The gain target is `target / max(envelope, noise_floor)`, clamped to
 * [min_gain_db, max_gain_db].  The noise floor keeps silence from being
 * amplified into a noise storm.
 */

#define AGC_MAX_CHANNELS 32

struct agc_config {
    float target_db;      /* wanted output level, -60..0 dBFS, default -6 */
    float max_gain_db;    /* maximum boost, 0..60 dB, default 30 */
    float min_gain_db;    /* maximum cut, -60..0 dB, default -20 */
    float attack_ms;      /* gain reduction time constant, default 5 */
    float release_ms;     /* gain recovery time constant, default 200 */
    float detector_attack_ms;  /* envelope attack, default 1 */
    float detector_release_ms; /* envelope release, default 50 */
    float noise_floor_db;      /* below this the gain stops rising, default -70 */
};

struct agc_channel_state {
    float envelope; /* linear peak envelope */
    float gain;     /* linear, linked across channels */
};

struct agc {
    struct agc_config config;
    int sample_rate;
    uint32_t channel_count; /* 0 until the first process call */
    struct agc_channel_state channels[AGC_MAX_CHANNELS];
    float attack_coef;
    float release_coef;
    float detector_attack_coef;
    float detector_release_coef;
    float target_linear;
    float max_gain_linear;
    float min_gain_linear;
    float floor_linear;
};

void agc_config_default(struct agc_config *cfg);

/* Validates the configuration and derives the coefficients. */
int agc_init(struct agc *agc, const struct agc_config *cfg, int sample_rate);

/* Clears the envelopes and restores gain to 1.0; the configuration stays. */
void agc_reset(struct agc *agc);

/* Runs the AGC over frames * channels samples. */
int agc_process(struct agc *agc, struct dsp_buffer *in, struct dsp_buffer *out);

/* Module factory with the default configuration. */
struct dsp_module *create_agc_module(void);

/* Module factory with an explicit configuration; NULL on invalid config. */
struct dsp_module *agc_module_create(const struct agc_config *cfg, int sample_rate);

/* Module callbacks, exported so tests can build a module object by hand. */
int agc_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out);
void agc_module_reset(void *ctx);
void agc_module_destroy(void *ctx);

#endif /* MINI_DSP_AGC_H */
