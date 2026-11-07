#ifndef MINI_DSP_AGC_H
#define MINI_DSP_AGC_H

#include "dsp_module.h"

/*
 * !! This is NOT an automatic gain control !!
 *
 * The module keeps the historical name "AGc", but the current algorithm is a
 * fixed gain stage: y[n] = x[n] * gain, with gain == 1.0 by default.  There is
 * no level detection, no attack/release smoothing and no target tracking.
 *
 * target_level / attack_ms / release_ms are reserved for the future real AGC
 * and take no part in the processing path.
 */
struct agc_config {
    float gain;         /* fixed multiplier, default 1.0 */
    float target_level; /* reserved, unused, default 1.0 */
    float attack_ms;    /* reserved, unused, default 10.0 */
    float release_ms;   /* reserved, unused, default 100.0 */
};

struct agc {
    float gain;
    struct agc_config config;
};

void agc_config_default(struct agc_config *cfg);

/* Applies the default configuration (gain 1.0) to a caller-owned object. */
int agc_init(struct agc *agc);

/* y = x * gain for every frame * channel sample.  No allocation, no printing. */
int agc_process(struct agc *agc, struct dsp_buffer *in, struct dsp_buffer *out);

/* Restores gain to 1.0 (the fixed-gain default). */
void agc_reset(struct agc *agc);

/* Module factory; returns NULL on allocation failure. */
struct dsp_module *create_agc_module(void);

/* Module callbacks, exported so tests can build a module object by hand. */
int agc_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out);
void agc_module_reset(void *ctx);
void agc_module_destroy(void *ctx);

#endif /* MINI_DSP_AGC_H */
