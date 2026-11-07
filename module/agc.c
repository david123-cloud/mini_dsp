#include "agc.h"

#include <stddef.h>
#include <stdlib.h>

#include "dsp_error.h"

#define AGC_DEFAULT_GAIN 1.0f
#define AGC_DEFAULT_TARGET_LEVEL 1.0f
#define AGC_DEFAULT_ATTACK_MS 10.0f
#define AGC_DEFAULT_RELEASE_MS 100.0f

static const struct dsp_module_ops g_agc_ops = {
    agc_module_process,
    agc_module_reset,
    agc_module_destroy,
};

void agc_config_default(struct agc_config *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->gain = AGC_DEFAULT_GAIN;
    cfg->target_level = AGC_DEFAULT_TARGET_LEVEL;
    cfg->attack_ms = AGC_DEFAULT_ATTACK_MS;
    cfg->release_ms = AGC_DEFAULT_RELEASE_MS;
}

int agc_init(struct agc *agc)
{
    if (agc == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    agc_config_default(&agc->config);
    agc->gain = agc->config.gain;
    return DSP_OK;
}

void agc_reset(struct agc *agc)
{
    if (agc == NULL) {
        return;
    }
    agc->gain = AGC_DEFAULT_GAIN;
    agc->config.gain = AGC_DEFAULT_GAIN;
}

int agc_process(struct agc *agc, struct dsp_buffer *in, struct dsp_buffer *out)
{
    size_t sample_count;
    size_t index;

    if (agc == NULL || in == NULL || out == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->data == NULL || out->data == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->frames != out->frames || in->channels != out->channels) {
        return DSP_ERR_INVALID_ARG;
    }
    sample_count = (size_t)in->frames * (size_t)in->channels;
    for (index = 0u; index < sample_count; ++index) {
        out->data[index] = in->data[index] * agc->gain;
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

struct dsp_module *create_agc_module(void)
{
    struct dsp_module *module;
    struct agc *agc;

    module = (struct dsp_module *)calloc(1u, sizeof(*module));
    if (module == NULL) {
        return NULL;
    }
    agc = (struct agc *)calloc(1u, sizeof(*agc));
    if (agc == NULL) {
        free(module);
        return NULL;
    }
    if (agc_init(agc) != DSP_OK) {
        free(agc);
        free(module);
        return NULL;
    }

    module->name = "agc"; /* historical name; fixed gain only */
    module->priv = agc;
    module->ops = &g_agc_ops;
    return module;
}
