#include "fft.h"

#include <math.h>
#include <stddef.h>

#include "dsp_error.h"
#include "dsp_math.h"

/*
 * Decimation-in-time bit reversal.
 *
 * Indices are deliberately named idx / j / bit / span / half / offset so that
 * "i" is never confused with the imaginary unit.
 */
static void fft_bit_reverse(struct fft_runtime *fft)
{
    uint32_t idx;
    uint32_t j = 0u;

    for (idx = 1u; idx < (uint32_t)FFT_SIZE; ++idx) {
        uint32_t bit = (uint32_t)FFT_SIZE >> 1;

        for (; (j & bit) != 0u; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;

        if (idx < j) {
            float tmp_real = fft->real[idx];
            float tmp_imag = fft->imag[idx];

            fft->real[idx] = fft->real[j];
            fft->imag[idx] = fft->imag[j];
            fft->real[j] = tmp_real;
            fft->imag[j] = tmp_imag;
        }
    }
}

int fft_transform(struct fft_runtime *fft, int inverse)
{
    uint32_t span;
    uint32_t offset;
    uint32_t idx;
    float sign;

    if (fft == NULL) {
        return DSP_ERR_INVALID_ARG;
    }

    sign = (inverse != 0) ? 1.0f : -1.0f;

    fft_bit_reverse(fft);

    for (span = 2u; span <= (uint32_t)FFT_SIZE; span <<= 1) {
        uint32_t half = span >> 1;

        for (offset = 0u; offset < (uint32_t)FFT_SIZE; offset += span) {
            for (idx = 0u; idx < half; ++idx) {
                float angle = sign * 2.0f * DSP_PI_F * (float)idx / (float)span;
                float wr = cosf(angle);
                float wi = sinf(angle);
                uint32_t even = offset + idx;
                uint32_t odd = even + half;
                float tr = wr * fft->real[odd] - wi * fft->imag[odd];
                float ti = wr * fft->imag[odd] + wi * fft->real[odd];

                fft->real[odd] = fft->real[even] - tr;
                fft->imag[odd] = fft->imag[even] - ti;
                fft->real[even] = fft->real[even] + tr;
                fft->imag[even] = fft->imag[even] + ti;
            }
        }
    }

    if (inverse != 0) {
        for (idx = 0u; idx < (uint32_t)FFT_SIZE; ++idx) {
            fft->real[idx] /= (float)FFT_SIZE;
            fft->imag[idx] /= (float)FFT_SIZE;
        }
    }
    return DSP_OK;
}

int fft_forward(struct fft_runtime *fft, const float *input, uint32_t frames)
{
    uint32_t idx;

    if (fft == NULL || input == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (frames != (uint32_t)FFT_SIZE) {
        return DSP_ERR_INVALID_ARG;
    }
    for (idx = 0u; idx < (uint32_t)FFT_SIZE; ++idx) {
        if (!isfinite(input[idx])) {
            return DSP_ERR_RANGE;
        }
        fft->real[idx] = input[idx];
        fft->imag[idx] = 0.0f;
    }
    return fft_transform(fft, 0);
}
