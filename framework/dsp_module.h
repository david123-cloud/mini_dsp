#ifndef MINI_DSP_MODULE_H
#define MINI_DSP_MODULE_H

#include <stdint.h>

#include "dsp_buffer.h"

/*
 * A processing module.
 *
 * process(ctx, in, out) contract:
 *   - synchronous; the module must not keep the in/out pointers after return
 *   - in->frames == out->frames and in->channels == out->channels; sample
 *     rate is never changed
 *   - on success every valid output sample is written
 *   - the input samples must not be modified
 *   - history is kept across consecutive calls; only reset() clears it
 *   - the module never reallocates or replaces out->data and never changes
 *     the frame/channel counts
 *   - returns DSP_OK (0) on success, a negative code on failure
 *
 * reset(ctx) may be NULL.  destroy(ctx) frees the private state and may be
 * NULL; the module object itself is freed by the pipeline.
 */
struct dsp_module_ops {
    int (*process)(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out);
    void (*reset)(void *ctx);
    void (*destroy)(void *ctx);
};

struct dsp_module {
    const char *name;
    void *priv;
    const struct dsp_module_ops *ops;
};

#endif /* MINI_DSP_MODULE_H */
