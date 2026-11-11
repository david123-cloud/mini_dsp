#include "ns.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "dsp_error.h"

#define NS_DEFAULT_THRESHOLD_DB (-50.0f)
#define NS_DEFAULT_FLOOR_DB (-24.0f)
#define NS_DEFAULT_ATTACK_MS 1.0f
#define NS_DEFAULT_RELEASE_MS 50.0f
#define NS_DEFAULT_HYSTERESIS_DB 3.0f
#define NS_DEFAULT_KNEE_DB 6.0f

#define NS_MIN_DB (-96.0f)
#define NS_MAX_DB 0.0f
#define NS_MAX_TIME_MS 1000.0f
#define NS_MAX_HYSTERESIS_DB 24.0f

static const struct dsp_module_ops g_ns_ops = {
    ns_module_process,
    ns_module_reset,
    ns_module_destroy,
};

static float ns_db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

/* One-pole smoothing coefficient for a time constant in milliseconds. */
static float ns_time_coef(float milliseconds, int sample_rate)
{
    float samples = 0.001f * milliseconds * (float)sample_rate;

    if (!(samples > 0.0f)) {
        return 1.0f;
    }
    return 1.0f - expf(-1.0f / samples);
}

static int ns_validate(const struct ns_config *cfg, int sample_rate)
{
    if (cfg == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (sample_rate <= 0) {
        return DSP_ERR_INVALID_ARG;
    }
    if (!isfinite(cfg->threshold_db) || !isfinite(cfg->floor_db) ||
        !isfinite(cfg->attack_ms) || !isfinite(cfg->release_ms) ||
        !isfinite(cfg->hysteresis_db)) {
        return DSP_ERR_INVALID_ARG;
    }
    if (cfg->threshold_db < NS_MIN_DB || cfg->threshold_db > NS_MAX_DB) {
        return DSP_ERR_RANGE;
    }
    if (cfg->floor_db < NS_MIN_DB || cfg->floor_db > NS_MAX_DB) {
        return DSP_ERR_RANGE;
    }
    if (!(cfg->attack_ms > 0.0f) || cfg->attack_ms > NS_MAX_TIME_MS) {
        return DSP_ERR_RANGE;
    }
    if (!(cfg->release_ms > 0.0f) || cfg->release_ms > NS_MAX_TIME_MS) {
        return DSP_ERR_RANGE;
    }
    if (cfg->hysteresis_db < 0.0f || cfg->hysteresis_db > NS_MAX_HYSTERESIS_DB) {
        return DSP_ERR_RANGE;
    }
    if (cfg->knee_db < 0.0f || cfg->knee_db > NS_MAX_HYSTERESIS_DB) {
        return DSP_ERR_RANGE;
    }
    return DSP_OK;
}

void ns_config_default(struct ns_config *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->threshold_db = NS_DEFAULT_THRESHOLD_DB;
    cfg->floor_db = NS_DEFAULT_FLOOR_DB;
    cfg->attack_ms = NS_DEFAULT_ATTACK_MS;
    cfg->release_ms = NS_DEFAULT_RELEASE_MS;
    cfg->hysteresis_db = NS_DEFAULT_HYSTERESIS_DB;
    cfg->knee_db = NS_DEFAULT_KNEE_DB;
}

int ns_init(struct ns *ns, const struct ns_config *cfg, int sample_rate)
{
    int status;

    if (ns == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    status = ns_validate(cfg, sample_rate);
    if (status != DSP_OK) {
        return status;
    }

    ns->config = *cfg;
    ns->sample_rate = sample_rate;
    ns->channel_count = 0u;

    ns->threshold_low = ns_db_to_linear(cfg->threshold_db);
    ns->threshold_high = ns_db_to_linear(cfg->threshold_db + cfg->hysteresis_db);
    ns->knee_high = ns_db_to_linear(cfg->threshold_db + cfg->knee_db);
    ns->floor_gain = ns_db_to_linear(cfg->floor_db);

    ns->attack_coef = ns_time_coef(cfg->attack_ms, sample_rate);
    ns->release_coef = ns_time_coef(cfg->release_ms, sample_rate);

    if (!isfinite(ns->threshold_low) || !isfinite(ns->threshold_high) ||
        !isfinite(ns->floor_gain) || !isfinite(ns->attack_coef) ||
        !isfinite(ns->release_coef)) {
        return DSP_ERR_STATE;
    }

    ns_reset(ns);
    return DSP_OK;
}

void ns_reset(struct ns *ns)
{
    uint32_t index;

    if (ns == NULL) {
        return;
    }
    for (index = 0u; index < NS_MAX_CHANNELS; ++index) {
        ns->channels[index].envelope = 0.0f;
        ns->channels[index].gain = 1.0f;
        ns->channels[index].open = 0;
    }
    ns->channel_count = 0u;
}

/* Soft-knee expansion curve: fully attenuated at or below the threshold,
 * fully open at the top of the knee, linear in amplitude in between.  The
 * hysteresis latch still decides when we call the gate "open" for reporting,
 * but the gain itself follows this curve so it never jumps. */
static float ns_target_gain(const struct ns *ns, float envelope)
{
    float span;

    if (envelope >= ns->knee_high) {
        return 1.0f;
    }
    if (envelope <= ns->threshold_low) {
        return ns->floor_gain;
    }
    span = ns->knee_high - ns->threshold_low;
    if (!(span > 0.0f)) {
        return 1.0f;
    }
    return ns->floor_gain +
           (1.0f - ns->floor_gain) * (envelope - ns->threshold_low) / span;
}

int ns_process(struct ns *ns, struct dsp_buffer *in, struct dsp_buffer *out)
{
    uint32_t frame;
    uint32_t channel;

    if (ns == NULL || in == NULL || out == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->data == NULL || out->data == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->frames != out->frames || in->channels != out->channels) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->channels == 0u || in->channels > NS_MAX_CHANNELS) {
        return DSP_ERR_UNSUPPORTED;
    }
    /* A channel-count change invalidates the per-channel history. */
    if (ns->channel_count != in->channels) {
        ns_reset(ns);
        ns->channel_count = in->channels;
    }

    for (frame = 0u; frame < in->frames; ++frame) {
        for (channel = 0u; channel < in->channels; ++channel) {
            size_t index = (size_t)frame * (size_t)in->channels + (size_t)channel;
            struct ns_channel_state *state = &ns->channels[channel];
            float sample = in->data[index];
            float level = fabsf(sample);
            float target;
            float coef;

            /* Envelope follower: rises with attack, falls with release. */
            coef = (level > state->envelope) ? ns->attack_coef : ns->release_coef;
            state->envelope += coef * (level - state->envelope);

            /* Hysteresis: open above threshold_high, close below threshold_low. */
            if (state->open != 0) {
                if (state->envelope < ns->threshold_low) {
                    state->open = 0;
                }
            } else {
                if (state->envelope > ns->threshold_high) {
                    state->open = 1;
                }
            }

            target = ns_target_gain(ns, state->envelope);
            coef = (target < state->gain) ? ns->attack_coef : ns->release_coef;
            state->gain += coef * (target - state->gain);

            out->data[index] = sample * state->gain;
        }
    }
    return DSP_OK;
}

int ns_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out)
{
    return ns_process((struct ns *)ctx, in, out);
}

void ns_module_reset(void *ctx)
{
    ns_reset((struct ns *)ctx);
}

void ns_module_destroy(void *ctx)
{
    free(ctx);
}

struct dsp_module *ns_module_create(const struct ns_config *cfg, int sample_rate)
{
    struct dsp_module *module;
    struct ns *ns;
    int status;

    if (cfg == NULL) {
        return NULL;
    }

    module = (struct dsp_module *)calloc(1u, sizeof(*module));
    if (module == NULL) {
        return NULL;
    }
    ns = (struct ns *)calloc(1u, sizeof(*ns));
    if (ns == NULL) {
        free(module);
        return NULL;
    }

    status = ns_init(ns, cfg, sample_rate);
    if (status != DSP_OK) {
        free(ns);
        free(module);
        return NULL;
    }

    module->name = "ns";
    module->priv = ns;
    module->ops = &g_ns_ops;
    return module;
}
