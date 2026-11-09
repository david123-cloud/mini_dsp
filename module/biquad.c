#define _POSIX_C_SOURCE 200809L

#include "biquad.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_error.h"
#include "dsp_math.h"

/* Direct form I keeps one history set per channel, so the module owns one
 * biquad per channel instead of refusing anything but mono. */
#define BIQUAD_MAX_CHANNELS 32u

struct biquad_module {
    struct biquad_config config;
    int sample_rate;
    uint32_t channel_count; /* 0 until the first process call */
    struct biquad channels[BIQUAD_MAX_CHANNELS];
};

static const struct dsp_module_ops g_biquad_ops = {
    biquad_module_process,
    biquad_module_reset,
    biquad_module_destroy,
};

static int biquad_validate_config(const struct biquad_config *cfg, int sample_rate)
{
    float nyquist;

    if (cfg == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (sample_rate <= 0) {
        return DSP_ERR_INVALID_ARG;
    }
    if (!isfinite(cfg->fc) || !isfinite(cfg->q) || !isfinite(cfg->gain)) {
        return DSP_ERR_INVALID_ARG;
    }
    nyquist = (float)sample_rate * 0.5f;
    if (!(cfg->fc > 0.0f) || !(cfg->fc < nyquist)) {
        return DSP_ERR_RANGE;
    }
    if (!(cfg->q > 0.0f)) {
        return DSP_ERR_RANGE;
    }
    if (cfg->type != BIQUAD_LOWPASS && cfg->type != BIQUAD_HIGHPASS) {
        return DSP_ERR_UNSUPPORTED;
    }
    return DSP_OK;
}

int biquad_design(const struct biquad_config *cfg, int sample_rate, struct biquad_state *out)
{
    float omega;
    float sin_omega;
    float cos_omega;
    float alpha;
    float a0;
    float b0;
    float b1;
    float b2;
    int status;

    if (out == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    status = biquad_validate_config(cfg, sample_rate);
    if (status != DSP_OK) {
        return status;
    }

    omega = 2.0f * DSP_PI_F * cfg->fc / (float)sample_rate;
    sin_omega = sinf(omega);
    cos_omega = cosf(omega);
    alpha = sin_omega / (2.0f * cfg->q);
    a0 = 1.0f + alpha;

    if (!isfinite(a0) || fabsf(a0) < 1e-12f) {
        return DSP_ERR_STATE;
    }

    if (cfg->type == BIQUAD_HIGHPASS) {
        b0 = (1.0f + cos_omega) * 0.5f;
        b1 = -(1.0f + cos_omega);
        b2 = b0;
    } else {
        b0 = (1.0f - cos_omega) * 0.5f;
        b1 = 1.0f - cos_omega;
        b2 = b0;
    }

    out->b0 = b0 / a0;
    out->b1 = b1 / a0;
    out->b2 = b2 / a0;
    out->a1 = (-2.0f * cos_omega) / a0;
    out->a2 = (1.0f - alpha) / a0;
    out->x1 = 0.0f;
    out->x2 = 0.0f;
    out->y1 = 0.0f;
    out->y2 = 0.0f;
    return DSP_OK;
}

void biquad_set_coefficients(struct biquad *bq, float b0, float b1, float b2, float a1, float a2)
{
    if (bq == NULL) {
        return;
    }
    bq->state.b0 = b0;
    bq->state.b1 = b1;
    bq->state.b2 = b2;
    bq->state.a1 = a1;
    bq->state.a2 = a2;
}

void biquad_reset(struct biquad *bq)
{
    if (bq == NULL) {
        return;
    }
    bq->state.x1 = 0.0f;
    bq->state.x2 = 0.0f;
    bq->state.y1 = 0.0f;
    bq->state.y2 = 0.0f;
}

int biquad_init(struct biquad *bq, const struct biquad_config *cfg, int sample_rate)
{
    struct biquad_state designed;
    int status;

    if (bq == NULL) {
        return DSP_ERR_INVALID_ARG;
    }

    /* Start from a valid, silent, pass-through state. */
    biquad_set_coefficients(bq, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    biquad_reset(bq);
    bq->sample_rate = sample_rate;

    status = biquad_design(cfg, sample_rate, &designed);
    if (status != DSP_OK) {
        return status;
    }
    bq->state = designed;
    return DSP_OK;
}

float biquad_process_sample(struct biquad *bq, float sample)
{
    const struct biquad_state *st;
    float y;

    if (bq == NULL) {
        return 0.0f;
    }
    st = &bq->state;
    y = st->b0 * sample + st->b1 * st->x1 + st->b2 * st->x2 - st->a1 * st->y1 -
        st->a2 * st->y2;

    bq->state.x2 = st->x1;
    bq->state.x1 = sample;
    bq->state.y2 = st->y1;
    bq->state.y1 = y;
    return y;
}

int biquad_process_mono(struct biquad *bq, float *data, uint32_t frames)
{
    uint32_t frame;

    if (bq == NULL || data == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    for (frame = 0u; frame < frames; ++frame) {
        data[frame] = biquad_process_sample(bq, data[frame]);
    }
    return DSP_OK;
}

int biquad_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out)
{
    struct biquad_module *module = (struct biquad_module *)ctx;
    uint32_t frame;
    uint32_t channel;

    if (module == NULL || in == NULL || out == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->data == NULL || out->data == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->frames != out->frames || in->channels != out->channels) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->channels == 0u || in->channels > BIQUAD_MAX_CHANNELS) {
        return DSP_ERR_UNSUPPORTED;
    }

    /* The buffer's channel count is only known here, so the per-channel
     * instances are created on the first call and rebuilt if it changes. */
    if (module->channel_count != in->channels) {
        for (channel = 0u; channel < in->channels; ++channel) {
            if (biquad_init(&module->channels[channel], &module->config, module->sample_rate) !=
                DSP_OK) {
                return DSP_ERR_STATE;
            }
        }
        module->channel_count = in->channels;
    }

    for (frame = 0u; frame < in->frames; ++frame) {
        for (channel = 0u; channel < in->channels; ++channel) {
            size_t index = (size_t)frame * (size_t)in->channels + (size_t)channel;

            out->data[index] = biquad_process_sample(&module->channels[channel], in->data[index]);
        }
    }
    return DSP_OK;
}

void biquad_module_reset(void *ctx)
{
    struct biquad_module *module = (struct biquad_module *)ctx;
    uint32_t channel;

    if (module == NULL) {
        return;
    }
    for (channel = 0u; channel < module->channel_count; ++channel) {
        biquad_reset(&module->channels[channel]);
    }
}

void biquad_module_destroy(void *ctx)
{
    free(ctx);
}

struct dsp_module *biquad_module_create(const struct biquad_config *cfg, int sample_rate)
{
    struct dsp_module *module;
    struct biquad_module *state;
    int status;

    if (cfg == NULL) {
        return NULL;
    }
    status = biquad_validate_config(cfg, sample_rate);
    if (status != DSP_OK) {
        return NULL;
    }

    module = (struct dsp_module *)calloc(1u, sizeof(*module));
    if (module == NULL) {
        return NULL;
    }
    state = (struct biquad_module *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        free(module);
        return NULL;
    }

    state->config = *cfg;
    state->sample_rate = sample_rate;
    state->channel_count = 0u;

    module->name = "biquad";
    module->priv = state;
    module->ops = &g_biquad_ops;
    return module;
}
