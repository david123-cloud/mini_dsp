#ifndef MINI_DSP_BLOCK_H
#define MINI_DSP_BLOCK_H

#include <stdint.h>

#include "dsp_buffer.h"
#include "dsp_pipeline.h"

/*
 * Offline block driver.
 *
 * Runs a whole PCM region through an already configured pipeline in fixed-size
 * blocks.  Every block is a fresh pipeline_process() call, so module history
 * (biquad state, NS envelopes, ...) carries across block boundaries exactly as
 * it would during continuous streaming.  This is offline processing of
 * already-decoded PCM, not device streaming: the driver never sleeps, never
 * allocates, and never touches files.
 */

/*
 * Processes `frames` frames block by block.
 *
 *   input   frames * pipe->channels interleaved floats, read only
 *   output  frames * pipe->channels interleaved floats, fully written
 *   block_frames  frames per pipeline_process() call, 1..pipe->max_frames
 *
 * `output` may be the same buffer as `input` (in place): the pipeline never
 * writes into the input view.  Overlapping but non-identical buffers are not
 * supported.
 *
 * Returns DSP_OK, or the first failing status.  On failure some leading blocks
 * may already be written, but no error is ever silently swallowed.
 */
int dsp_block_process(struct dsp_pipeline *pipe, const float *input, float *output,
                      uint32_t frames, uint32_t block_frames);

/*
 * Same walk, but each block's result is handed to a sink instead of being
 * copied into a full-length output buffer.  `block_scratch` must hold
 * block_frames * pipe->channels floats; the driver writes each block there and
 * calls sink->write() with the same pointer.
 *
 * Returning anything other than DSP_OK from sink->write() aborts the run and
 * that value is propagated.
 */
struct dsp_block_sink {
    int (*write)(void *user, uint32_t frame_offset, uint32_t frames, const float *data);
    void *user;
};

int dsp_block_stream(struct dsp_pipeline *pipe, const float *input, uint32_t frames,
                     uint32_t block_frames, float *block_scratch,
                     const struct dsp_block_sink *sink);

#endif /* MINI_DSP_BLOCK_H */
