#include "dsp_pipeline.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_error.h"

/*
 * Module ownership registry.
 *
 * The pipeline takes ownership of every module it accepts, and one module
 * object must not be owned twice (not even by two pipelines).  A small static
 * table enforces that without any threading assumptions: mini_dsp is
 * single-threaded by design.
 */
#define DSP_MAX_OWNED_MODULES 64

static struct dsp_module *g_owned_modules[DSP_MAX_OWNED_MODULES];

static int module_is_owned(const struct dsp_module *module)
{
    uint32_t index;

    for (index = 0; index < DSP_MAX_OWNED_MODULES; ++index) {
        if (g_owned_modules[index] == module) {
            return 1;
        }
    }
    return 0;
}

static int module_claim(struct dsp_module *module)
{
    uint32_t index;
    uint32_t free_slot = DSP_MAX_OWNED_MODULES;

    for (index = 0; index < DSP_MAX_OWNED_MODULES; ++index) {
        if (g_owned_modules[index] == module) {
            return DSP_ERR_STATE;
        }
        if (g_owned_modules[index] == NULL && free_slot == DSP_MAX_OWNED_MODULES) {
            free_slot = index;
        }
    }
    if (free_slot == DSP_MAX_OWNED_MODULES) {
        return DSP_ERR_RANGE; /* registry exhausted */
    }
    g_owned_modules[free_slot] = module;
    return DSP_OK;
}

static void module_release(struct dsp_module *module)
{
    uint32_t index;

    for (index = 0; index < DSP_MAX_OWNED_MODULES; ++index) {
        if (g_owned_modules[index] == module) {
            g_owned_modules[index] = NULL;
        }
    }
}

static int pipeline_holds(const struct dsp_pipeline *pipe, const struct dsp_module *module)
{
    uint32_t index;

    for (index = 0; index < pipe->module_count; ++index) {
        if (pipe->modules[index] == module) {
            return 1;
        }
    }
    return 0;
}

int pipeline_init(struct dsp_pipeline *pipe, uint32_t max_frames, uint32_t channels)
{
    size_t samples;

    if (pipe == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (pipe->initialized) {
        return DSP_ERR_STATE;
    }
    if (max_frames == 0u || channels == 0u) {
        return DSP_ERR_INVALID_ARG;
    }
    /* Reject max_frames * channels * sizeof(float) overflowing size_t. */
    if (max_frames > UINT32_MAX / channels) {
        return DSP_ERR_RANGE;
    }
    samples = (size_t)max_frames * (size_t)channels;
    if (samples > SIZE_MAX / sizeof(float)) {
        return DSP_ERR_RANGE;
    }
    pipe->scratch_a = (float *)calloc(samples, sizeof(float));
    pipe->scratch_b = (float *)calloc(samples, sizeof(float));
    if (pipe->scratch_a == NULL || pipe->scratch_b == NULL) {
        free(pipe->scratch_a);
        free(pipe->scratch_b);
        pipe->scratch_a = NULL;
        pipe->scratch_b = NULL;
        return DSP_ERR_NO_MEMORY;
    }

    pipe->max_frames = max_frames;
    pipe->channels = channels;
    pipe->scratch_samples = (uint32_t)samples;
    pipe->module_count = 0u;
    pipe->initialized = 1;
    return DSP_OK;
}

int pipeline_add(struct dsp_pipeline *pipe, struct dsp_module *module)
{
    int status;

    if (pipe == NULL || module == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (!pipe->initialized) {
        return DSP_ERR_STATE;
    }
    if (module->ops == NULL || module->ops->process == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (pipe->module_count >= DSP_PIPELINE_MAX_MODULES) {
        return DSP_ERR_RANGE;
    }
    if (pipeline_holds(pipe, module)) {
        return DSP_ERR_STATE;
    }
    if (module_is_owned(module)) {
        return DSP_ERR_STATE;
    }

    status = module_claim(module);
    if (status != DSP_OK) {
        return status;
    }

    pipe->modules[pipe->module_count] = module;
    pipe->module_count += 1u;
    return DSP_OK;
}

int pipeline_process(struct dsp_pipeline *pipe, struct dsp_buffer *in, struct dsp_buffer *out)
{
    struct dsp_buffer stage_in;
    struct dsp_buffer stage_out;
    float *source;
    uint32_t index;
    size_t samples;

    if (pipe == NULL || in == NULL || out == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (!pipe->initialized) {
        return DSP_ERR_STATE;
    }
    if (in->data == NULL || out->data == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->frames != out->frames) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->channels != pipe->channels || out->channels != pipe->channels) {
        return DSP_ERR_INVALID_ARG;
    }
    if (in->frames > pipe->max_frames) {
        return DSP_ERR_RANGE;
    }
    if (in->frames == 0u) {
        return DSP_OK;
    }
    if (in->frames > UINT32_MAX / in->channels) {
        return DSP_ERR_RANGE;
    }
    samples = (size_t)in->frames * (size_t)in->channels;

    if (pipe->module_count == 0u) {
        memcpy(out->data, in->data, samples * sizeof(float));
        return DSP_OK;
    }

    source = in->data;
    for (index = 0u; index < pipe->module_count; ++index) {
        struct dsp_module *module = pipe->modules[index];
        float *target;
        int status;

        /* Ping-pong: module 0 writes A, module 1 writes B, module 2 writes A, ... */
        target = ((index % 2u) == 0u) ? pipe->scratch_a : pipe->scratch_b;

        stage_in.data = source;
        stage_in.frames = in->frames;
        stage_in.channels = in->channels;

        stage_out.data = target;
        stage_out.frames = in->frames;
        stage_out.channels = in->channels;

        status = module->ops->process(module->priv, &stage_in, &stage_out);
        if (status != DSP_OK) {
            return status;
        }
        /* A module may neither swap the output pointer nor resize the views. */
        if (stage_out.data != target || stage_out.frames != in->frames ||
            stage_out.channels != in->channels) {
            return DSP_ERR_STATE;
        }
        if (stage_in.frames != in->frames || stage_in.channels != in->channels) {
            return DSP_ERR_STATE;
        }

        source = target;
    }

    /* Always copy to the caller's output: nothing depends on module parity. */
    memcpy(out->data, source, samples * sizeof(float));
    return DSP_OK;
}

void pipeline_reset(struct dsp_pipeline *pipe)
{
    uint32_t index;

    if (pipe == NULL || !pipe->initialized) {
        return;
    }
    for (index = 0u; index < pipe->module_count; ++index) {
        struct dsp_module *module = pipe->modules[index];

        if (module != NULL && module->ops != NULL && module->ops->reset != NULL) {
            module->ops->reset(module->priv);
        }
    }
}

void pipeline_destroy(struct dsp_pipeline *pipe)
{
    uint32_t index;

    if (pipe == NULL) {
        return;
    }
    for (index = 0u; index < pipe->module_count; ++index) {
        struct dsp_module *module = pipe->modules[index];

        pipe->modules[index] = NULL;
        if (module == NULL) {
            continue;
        }
        if (module->ops != NULL && module->ops->destroy != NULL) {
            module->ops->destroy(module->priv);
        }
        module_release(module);
        free(module);
    }
    free(pipe->scratch_a);
    free(pipe->scratch_b);
    memset(pipe, 0, sizeof(*pipe));
}
