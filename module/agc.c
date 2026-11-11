#define _POSIX_C_SOURCE 200809L

#include "agc.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "dsp_error.h"

#define AGC_DEFAULT_TARGET_DB (-6.0f)
#define AGC_DEFAULT_MAX_GAIN_DB 30.0f
#define AGC_DEFAULT_MIN_GAIN_DB (-20.0f)
#define AGC_DEFAULT_ATTACK_MS 5.0f
#define AGC_DEFAULT_RELEASE_MS 200.0f
#define AGC_DEFAULT_DETECTOR_ATTACK_MS 1.0f
#define AGC_DEFAULT_DETECTOR_RELEASE_MS 50.0f
#define AGC_DEFAULT_NOISE_FLOOR_DB (-70.0f)

#define AGC_MAX_TIME_MS 5000.0f

static const struct dsp_module_ops g_agc_ops = {
    agc_module_process,
    agc_module_reset,
    agc_module_destroy,
};

static float agc_db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

static float agc_time_coef(float milliseconds, int sample_rate)
{
    float samples = 0.001f * milliseconds * (float)sample_rate;

    if (!(samples > 0.0f)) {
        return 1.0f;
    }
    return 1.0f - expf(-1.0f / samples);
}

void agc_config_default(struct agc_config *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->target_db = AGC_DEFAULT_TARGET_DB;
    cfg->max_gain_db = AGC_DEFAULT_MAX_GAIN_DB;
    cfg->min_gain_db = AGC_DEFAULT_MIN_GAIN_DB;
    cfg->attack_ms = AGC_DEFAULT_ATTACK_MS;
    cfg->release_ms = AGC_DEFAULT_RELEASE_MS;
    cfg->detector_attack_ms = AGC_DEFAULT_DETECTOR_ATTACK_MS;
    cfg->detector_release_ms = AGC_DEFAULT_DETECTOR_RELEASE_MS;
    cfg->noise_floor_db = AGC_DEFAULT_NOISE_FLOOR_DB;
}

static int agc_validate(const struct agc_config *cfg, int sample_rate)
{
    if (cfg == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (sample_rate <= 0) {
        return DSP_ERR_INVALID_ARG;
    }
    if (!isfinite(cfg->target_db) || !isfinite(cfg->max_gain_db) ||
        !isfinite(cfg->min_gain_db) || !isfinite(cfg->attack_ms) ||
        !isfinite(cfg->release_ms) || !isfinite(cfg->detector_attack_ms) ||
        !isfinite(cfg->detector_release_ms) || !isfinite(cfg->noise_floor_db)) {
        return DSP_ERR_INVALID_ARG;
    }
    if (cfg->target_db < -60.0f || cfg->target_db > 0.0f) {
        return DSP_ERR_RANGE;
    }
    if (cfg->max_gain_db < 0.0f || cfg->max_gain_db > 60.0f) {
        return DSP_ERR_RANGE;
    }
    if (cfg->min_gain_db < -60.0f || cfg->min_gain_db > 0.0f) {
        return DSP_ERR_RANGE;
    }
    if (cfg->min_gain_db > cfg->max_gain_db) {
        return DSP_ERR_RANGE;
    }
    if (!(cfg->attack_ms > 0.0f) || cfg->attack_ms > AGC_MAX_TIME_MS ||
        !(cfg->release_ms > 0.0f) || cfg->release_ms > AGC_MAX_TIME_MS ||
        !(cfg->detector_attack_ms > 0.0f) || cfg->detector_attack_ms > AGC_MAX_TIME_MS ||
        !(cfg->detector_release_ms > 0.0f) || cfg->detector_release_ms > AGC_MAX_TIME_MS) {
        return DSP_ERR_RANGE;
    }
    /* The floor exists to stop the gain from chasing silence; above the target
     * it would invert the control law. */
    if (cfg->noise_floor_db < -96.0f || cfg->noise_floor_db >= cfg->target_db) {
        return DSP_ERR_RANGE;
    }
    return DSP_OK;
}

int agc_init(struct agc *agc, const struct agc_config *cfg, int sample_rate)
{
    int status;

    if (agc == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    status = agc_validate(cfg, sample_rate);
    if (status != DSP_OK) {
        return status;
    }

    agc->config = *cfg;
    agc->sample_rate = sample_rate;
    agc->channel_count = 0u;
    agc->attack_coef = agc_time_coef(cfg->attack_ms, sample_rate);
    agc->release_coef = agc_time_coef(cfg->release_ms, sample_rate);
    agc->detector_attack_coef = agc_time_coef(cfg->detector_attack_ms, sample_rate);
    agc->detector_release_coef = agc_time_coef(cfg->detector_release_ms, sample_rate);
    agc->target_linear = agc_db_to_linear(cfg->target_db);
    agc->max_gain_linear = agc_db_to_linear(cfg->max_gain_db);
    agc->min_gain_linear = agc_db_to_linear(cfg->min_gain_db);
    agc->floor_linear = agc_db_to_linear(cfg->noise_floor_db);

    if (!isfinite(agc->target_linear) || !isfinite(agc->max_gain_linear) ||
        !isfinite(agc->min_gain_linear) || !isfinite(agc->floor_linear)) {
        return DSP_ERR_STATE;
    }

    agc_reset(agc);
    return DSP_OK;
}

void agc_reset(struct agc *agc)
{
    uint32_t channel;

    if (agc == NULL) {
        return;
    }
    for (channel = 0u; channel < AGC_MAX_CHANNELS; ++channel) {
        agc->channels[channel].envelope = 0.0f;
        agc->channels[channel].gain = 1.0f;
    }
    agc->channel_count = 0u;
}

int agc_process(struct agc *agc, struct dsp_buffer *in, struct dsp_buffer *out)
{
    uint32_t frame;
    uint32_t channel;

    if (agc == NULL || in == NULL || out == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->data == NULL || out->data == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->frames != out->frames || in->channels != out->channels) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->channels == 0u || in->channels > AGC_MAX_CHANNELS) {
        return DSP_ERR_UNSUPPORTED;
    }
    if (agc->channel_count != in->channels) {
        agc_reset(agc);
        agc->channel_count = in->channels;
    }

    for (frame = 0u; frame < in->frames; ++frame) {
        float loudest = 0.0f;
        float gain;
        float desired;
        float coef;

        /* Detector: envelope per channel, then the loudest one drives the
         * shared gain so a stereo image never shifts. */
        for (channel = 0u; channel < in->channels; ++channel) {
            size_t index = (size_t)frame * (size_t)in->channels + (size_t)channel;
            struct agc_channel_state *state = &agc->channels[channel];
            float level = fabsf(in->data[index]);
            float detector = (level > state->envelope) ? agc->detector_attack_coef
                                                       : agc->detector_release_coef;

            state->envelope += detector * (level - state->envelope);
            if (state->envelope > loudest) {
                loudest = state->envelope;
            }
        }

        /* Control law, with the floor bounding the boost on silence. */
        desired = agc->target_linear / ((loudest > agc->floor_linear) ? loudest
                                                                      : agc->floor_linear);
        if (desired > agc->max_gain_linear) {
            desired = agc->max_gain_linear;
        } else if (desired < agc->min_gain_linear) {
            desired = agc->min_gain_linear;
        }

        gain = agc->channels[0].gain;
        coef = (desired < gain) ? agc->attack_coef : agc->release_coef;
        gain += coef * (desired - gain);

        for (channel = 0u; channel < in->channels; ++channel) {
            size_t index = (size_t)frame * (size_t)in->channels + (size_t)channel;

            agc->channels[channel].gain = gain;
            out->data[index] = in->data[index] * gain;
        }
    }
    return DSP_OK;
}

int agc_module_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out)
{
    return agc_process((struct agc *)ctx, in, out);
}

void agc_module_reset(void *ctx)
{
    agc_reset((struct agc *)ctx);
}

void agc_module_destroy(void *ctx)
{
    free(ctx);
}

struct dsp_module *agc_module_create(const struct agc_config *cfg, int sample_rate)
{
    struct dsp_module *module;
    struct agc *state;
    int status;

    if (cfg == NULL) {
        return NULL;
    }
    status = agc_validate(cfg, sample_rate);
    if (status != DSP_OK) {
        return NULL;
    }

    module = (struct dsp_module *)calloc(1u, sizeof(*module));
    if (module == NULL) {
        return NULL;
    }
    state = (struct agc *)calloc(1u, sizeof(*state));
    if (state == NULL) {
        free(module);
        return NULL;
    }
    if (agc_init(state, cfg, sample_rate) != DSP_OK) {
        free(state);
        free(module);
        return NULL;
    }

    module->name = "agc";
    module->priv = state;
    module->ops = &g_agc_ops;
    return module;
}

struct dsp_module *create_agc_module(void)
{
    struct agc_config cfg;

    agc_config_default(&cfg);
    return agc_module_create(&cfg, 48000);
}
