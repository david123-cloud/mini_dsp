#include "dsp_convert.h"

#include <math.h>

#include "dsp_error.h"

#define DSP_INT16_SCALE 32768.0f
#define DSP_INT16_MAX 32767
#define DSP_INT16_MIN (-32768)

void dsp_int16_to_float(const int16_t *source, float *target, size_t count)
{
    size_t index;

    if (source == NULL || target == NULL) {
        return;
    }
    for (index = 0u; index < count; ++index) {
        target[index] = (float)source[index] / DSP_INT16_SCALE;
    }
}

int dsp_float_to_int16(const float *source, int16_t *target, size_t count)
{
    size_t index;

    if (source == NULL || target == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    for (index = 0u; index < count; ++index) {
        float scaled;

        if (!isfinite(source[index])) {
            return DSP_ERR_RANGE;
        }
        scaled = source[index] * DSP_INT16_SCALE;

        /* Clamp before the cast: converting an out-of-range float to an
         * integer is undefined behaviour in C. */
        if (scaled >= (float)DSP_INT16_MAX) {
            target[index] = (int16_t)DSP_INT16_MAX;
        } else if (scaled <= (float)DSP_INT16_MIN) {
            target[index] = (int16_t)DSP_INT16_MIN;
        } else {
            target[index] = (int16_t)scaled; /* truncates toward zero */
        }
    }
    return DSP_OK;
}
