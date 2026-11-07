#ifndef MINI_DSP_BUFFER_H
#define MINI_DSP_BUFFER_H

#include <stdint.h>

/*
 * Interleaved float sample view.
 *
 *   data       pointer to frames * channels floats, owned by the caller
 *   frames     number of valid frames in this view
 *   channels   number of interleaved channels per frame
 *
 * A dsp_buffer is a *view*: it never owns, allocates or frees the sample
 * memory, and it has no capacity field.  The caller guarantees that the
 * pointed-to storage holds at least frames * channels floats.
 */
struct dsp_buffer {
    float *data;
    uint32_t frames;
    uint32_t channels;
};

#endif /* MINI_DSP_BUFFER_H */
