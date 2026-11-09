#ifndef MINI_DSP_BIQUAD_H
#define MINI_DSP_BIQUAD_H

#include <stdint.h>

#include "dsp_module.h"

enum biquad_type {
    BIQUAD_LOWPASS = 0,
    BIQUAD_HIGHPASS = 1,
    /* Reserved shapes: the enumerators exist, the design does not. */
    BIQUAD_NOTCH = 2,
    BIQUAD_PEAK = 3,
    BIQUAD_LOWSHELF = 4,
    BIQUAD_HIGHSHELF = 5
};

struct biquad_config {
    enum biquad_type type;
    float fc;   /* cutoff frequency in Hz, 0 < fc < fs/2 */
    float q;    /* quality factor, > 0, unitless */
    float gain; /* reserved for a future EQ stage; unused by low/high pass */
};

/*
 * Direct form I state for one channel:
 *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 * The five coefficients are stored already normalised by a0.
 *
 * The struct below is the mono building block.  The *module* wraps one of
 * these per channel, so a pipeline can carry up to 32 interleaved channels
 * while each channel keeps its own history.
 */
struct biquad_state {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float x1;
    float x2;
    float y1;
    float y2;
};

struct biquad {
    struct biquad_state state;
    int sample_rate;
};

/*
 * Designs the coefficients with the RBJ cookbook formulas and writes them into
 * *out with zeroed history.  Rejects NOTCH/PEAK/SHELF with DSP_ERR_UNSUPPORTED.
 */
int biquad_design(const struct biquad_config *cfg, int sample_rate, struct biquad_state *out);

/*
 * Sets pass-through coefficients and zero history first, then applies the
 * design.  On failure the object is left in the pass-through state.
 */
int biquad_init(struct biquad *bq, const struct biquad_config *cfg, int sample_rate);

/* Clears x1/x2/y1/y2 only; the coefficients are kept. */
void biquad_reset(struct biquad *bq);

/* Overwrites the five normalised coefficients; history is left untouched. */
void biquad_set_coefficients(struct biquad *bq, float b0, float b1, float b2, float a1, float a2);

float biquad_process_sample(struct biquad *bq, float sample);

/* In-place processing of a mono block. */
int biquad_process_mono(struct biquad *bq, float *data, uint32_t frames);

/*
 * Module factory.  Returns NULL on invalid configuration or allocation
 * failure; nothing is leaked on the failure paths.
 */
struct dsp_module *biquad_module_create(const struct biquad_config *cfg, int sample_rate);

/* Module callbacks, exported so tests can build a module object by hand. */
int biquad_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out);
void biquad_module_reset(void *ctx);
void biquad_module_destroy(void *ctx);

#endif /* MINI_DSP_BIQUAD_H */
