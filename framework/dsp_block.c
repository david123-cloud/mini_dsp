#include "dsp_block.h"

#include <stddef.h>
#include <stdint.h>

#include "dsp_error.h"

/* Items (frames * channels) that a full-length buffer occupies, overflow checked. */
static int dsp_block_check_size(const struct dsp_pipeline *pipe, uint32_t frames)
{
    if (pipe->channels == 0u) {
        return DSP_ERR_STATE;
    }
    if ((size_t)frames > SIZE_MAX / (size_t)pipe->channels) {
        return DSP_ERR_RANGE;
    }
    return DSP_OK;
}

static int dsp_block_validate(const struct dsp_pipeline *pipe, const float *input,
                              uint32_t frames, uint32_t block_frames)
{
    if (pipe == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (!pipe->initialized) {
        return DSP_ERR_STATE;
    }
    if (block_frames == 0u) {
        return DSP_ERR_INVALID_ARG;
    }
    /* The pipeline allocates its scratch for max_frames; a larger block would
     * be rejected mid-run, so fail before touching any data. */
    if (block_frames > pipe->max_frames) {
        return DSP_ERR_RANGE;
    }
    if (frames > 0u && input == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    return dsp_block_check_size(pipe, frames);
}

int dsp_block_process(struct dsp_pipeline *pipe, const float *input, float *output,
                      uint32_t frames, uint32_t block_frames)
{
    float *source;
    float *target;
    uint32_t offset = 0u;
    int status;

    status = dsp_block_validate(pipe, input, frames, block_frames);
    if (status != DSP_OK) {
        return status;
    }
    if (frames == 0u) {
        return DSP_OK;
    }
    if (output == NULL) {
        return DSP_ERR_INVALID_ARG;
    }

    /* The module contract forbids writing to the input view; the cast only
     * satisfies the legacy dsp_buffer field type. */
    source = (float *)(void *)input;
    target = output;

    while (offset < frames) {
        uint32_t remaining = frames - offset;
        uint32_t count = (remaining < block_frames) ? remaining : block_frames;
        struct dsp_buffer in_view;
        struct dsp_buffer out_view;

        in_view.data = source + (size_t)offset * (size_t)pipe->channels;
        in_view.frames = count;
        in_view.channels = pipe->channels;

        out_view.data = target + (size_t)offset * (size_t)pipe->channels;
        out_view.frames = count;
        out_view.channels = pipe->channels;

        status = pipeline_process(pipe, &in_view, &out_view);
        if (status != DSP_OK) {
            return status;
        }
        offset += count;
    }
    return DSP_OK;
}

int dsp_block_stream(struct dsp_pipeline *pipe, const float *input, uint32_t frames,
                     uint32_t block_frames, float *block_scratch,
                     const struct dsp_block_sink *sink)
{
    float *source;
    uint32_t offset = 0u;
    int status;

    status = dsp_block_validate(pipe, input, frames, block_frames);
    if (status != DSP_OK) {
        return status;
    }
    if (frames == 0u) {
        return DSP_OK;
    }
    if (block_scratch == NULL || sink == NULL || sink->write == NULL) {
        return DSP_ERR_INVALID_ARG;
    }

    source = (float *)(void *)input;

    while (offset < frames) {
        uint32_t remaining = frames - offset;
        uint32_t count = (remaining < block_frames) ? remaining : block_frames;
        struct dsp_buffer in_view;
        struct dsp_buffer out_view;

        in_view.data = source + (size_t)offset * (size_t)pipe->channels;
        in_view.frames = count;
        in_view.channels = pipe->channels;

        out_view.data = block_scratch;
        out_view.frames = count;
        out_view.channels = pipe->channels;

        status = pipeline_process(pipe, &in_view, &out_view);
        if (status != DSP_OK) {
            return status;
        }

        status = sink->write(sink->user, offset, count, block_scratch);
        if (status != DSP_OK) {
            return status;
        }
        offset += count;
    }
    return DSP_OK;
}
