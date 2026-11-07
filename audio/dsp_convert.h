#ifndef MINI_DSP_CONVERT_H
#define MINI_DSP_CONVERT_H

#include <stddef.h>
#include <stdint.h>

/* int16 -> float: sample / 32768.0f */
void dsp_int16_to_float(const int16_t *source, float *target, size_t count);

/*
 * float -> int16: reject non-finite input, scale by 32768.0f, truncate toward
 * zero, clamp to [-32768, 32767].
 *
 * Returns DSP_OK, or DSP_ERR_INVALID_ARG / DSP_ERR_RANGE on bad input.  On
 * failure *target is untouched past the first offending sample.
 */
int dsp_float_to_int16(const float *source, int16_t *target, size_t count);

#endif /* MINI_DSP_CONVERT_H */
