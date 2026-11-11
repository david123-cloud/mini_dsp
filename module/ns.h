#ifndef MINI_DSP_NS_H
#define MINI_DSP_NS_H

#include <stdint.h>

#include "dsp_module.h"

/*
 * Broadband noise gate / downward expander.
 *
 * !! This is NOT spectral noise suppression !!
 *
 * The module works sample by sample: it tracks a per-channel envelope, compares
 * it against a threshold with hysteresis, and fades the gain between 1.0 (open)
 * and a configured floor (closed).  It needs no framing, no FFT and no
 * overlap-add, so it fits the streaming dsp_module contract exactly.
 *
 * Spectral noise estimation with per-bin gains (the "real" NS) requires frame
 * buffering plus overlap-add, which mini_dsp does not implement yet.  This
 * module deliberately does not pretend to do it.
 *
 * Contract, as for every module:
 *   - synchronous, no allocation inside process(), no printing
 *   - in->frames == out->frames, in->channels == out->channels
 *   - every valid output sample is written
 *   - the input samples are never modified
 *   - history survives consecutive calls; only reset() clears it
 *   - multi-channel input is supported: each channel keeps its own state
 */

/* Per-channel state slots; inputs with more channels are refused. */
#define NS_MAX_CHANNELS 32

struct ns_config {
    float threshold_db; /* gate threshold in dBFS, -96..0, default -50 */
    float floor_db;     /* gain applied while closed, -96..0, default -24 */
    float attack_ms;    /* gain closing time constant, > 0, default 1 */
    float release_ms;   /* gain opening time constant, > 0, default 50 */
    float hysteresis_db; /* width between close and open thresholds, 0..24, default 3 */
    float knee_db;       /* soft-knee width above the threshold, 0..24, default 6 */
};

struct ns_channel_state {
    float envelope; /* smoothed magnitude, linear */
    float gain;     /* current smoothed gain, linear 0..1 */
    int open;       /* hysteresis latch: 1 = passing, 0 = attenuated */
};

struct ns {
    struct ns_config config;
    struct ns_channel_state channels[NS_MAX_CHANNELS];
    uint32_t channel_count; /* 0 until the first process call */
    int sample_rate;
    float attack_coef;
    float release_coef;
    float threshold_low;  /* linear, closes below this */
    float threshold_high; /* linear, opens above this */
    float knee_high;      /* linear, fully open at or above this */
    float floor_gain;     /* linear gain while closed */
};

void ns_config_default(struct ns_config *cfg);

/*
 * Validates the configuration, derives the linear thresholds and the smoothing
 * coefficients, and leaves the object fully reset (gain 1.0, envelope 0).
 * Returns DSP_OK or a negative code; on failure the object is left untouched.
 */
int ns_init(struct ns *ns, const struct ns_config *cfg, int sample_rate);

/* Clears every channel's envelope, gain and latch; the configuration stays. */
void ns_reset(struct ns *ns);

/* Runs the gate over frames * channels samples. */
int ns_process(struct ns *ns, struct dsp_buffer *in, struct dsp_buffer *out);

/* Module factory; returns NULL on invalid configuration or allocation failure. */
struct dsp_module *ns_module_create(const struct ns_config *cfg, int sample_rate);

/* Module callbacks, exported so tests can build a module object by hand. */
int ns_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out);
void ns_module_reset(void *ctx);
void ns_module_destroy(void *ctx);

#endif /* MINI_DSP_NS_H */
