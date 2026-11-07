#ifndef MINI_DSP_PIPELINE_H
#define MINI_DSP_PIPELINE_H

#include <stdint.h>

#include "dsp_buffer.h"
#include "dsp_module.h"

/* Maximum number of modules that can be chained in one pipeline. */
#define DSP_PIPELINE_MAX_MODULES 16

/*
 * Callers must pass a zero-initialised object:
 *
 *   struct dsp_pipeline pipe;
 *   memset(&pipe, 0, sizeof(pipe));
 *
 * The pipeline owns two scratch blocks of max_frames * channels floats each.
 * They are used as ping-pong buffers, by module index, so pipeline_process()
 * never allocates.  A single instance must not be processed concurrently.
 */
struct dsp_pipeline {
    struct dsp_module *modules[DSP_PIPELINE_MAX_MODULES];
    uint32_t module_count;
    uint32_t max_frames;
    uint32_t channels;
    uint32_t scratch_samples; /* max_frames * channels, overflow-checked */
    float *scratch_a;
    float *scratch_b;
    int initialized;
};

/* Allocates both scratch blocks.  Returns DSP_ERR_STATE if already initialised. */
int pipeline_init(struct dsp_pipeline *pipe, uint32_t max_frames, uint32_t channels);

/*
 * Appends a module.  On success the pipeline takes ownership of the module
 * object and of its private state (freed by pipeline_destroy); on failure the
 * caller still owns it and must release it.  The same module object cannot be
 * added twice, nor be owned by two pipelines at the same time.
 */
int pipeline_add(struct dsp_pipeline *pipe, struct dsp_module *module);

/*
 * Runs every module in order.  in->frames must be <= max_frames and the
 * channel count must match the pipeline.  The final result is always copied
 * into out->data, whatever the module count is.  With zero modules the input
 * is copied to the output unchanged.  No allocation happens here.
 */
int pipeline_process(struct dsp_pipeline *pipe, struct dsp_buffer *in, struct dsp_buffer *out);

/* Clears module history; the scratch blocks are kept. */
void pipeline_reset(struct dsp_pipeline *pipe);

/* Calls each module's destroy, frees the module objects, frees the scratch,
 * then zeroes the pipeline so a second call is a no-op. */
void pipeline_destroy(struct dsp_pipeline *pipe);

#endif /* MINI_DSP_PIPELINE_H */
