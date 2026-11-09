#define _POSIX_C_SOURCE 200809L

#include "fft.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_error.h"
#include "dsp_math.h"

/*
 * Decimation-in-time bit reversal, shared by the fixed and the sized API.
 *
 * Indices are deliberately named idx / j / bit so that "i" is never confused
 * with the imaginary unit.
 */
static void fft_bit_reverse(float *real, float *imag, uint32_t size)
{
    uint32_t idx;
    uint32_t j = 0u;

    for (idx = 1u; idx < size; ++idx) {
        uint32_t bit = size >> 1;

        for (; (j & bit) != 0u; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;

        if (idx < j) {
            float tmp_real = real[idx];
            float tmp_imag = imag[idx];

            real[idx] = real[j];
            imag[idx] = imag[j];
            real[j] = tmp_real;
            imag[j] = tmp_imag;
        }
    }
}

static void fft_core(float *real, float *imag, uint32_t size, int inverse)
{
    uint32_t span;
    uint32_t offset;
    uint32_t idx;
    float sign = (inverse != 0) ? 1.0f : -1.0f;
    float scale = 1.0f / (float)size;

    fft_bit_reverse(real, imag, size);

    for (span = 2u; span <= size; span <<= 1) {
        uint32_t half = span >> 1;

        for (offset = 0u; offset < size; offset += span) {
            for (idx = 0u; idx < half; ++idx) {
                float angle = sign * 2.0f * DSP_PI_F * (float)idx / (float)span;
                float wr = cosf(angle);
                float wi = sinf(angle);
                uint32_t even = offset + idx;
                uint32_t odd = even + half;
                float tr = wr * real[odd] - wi * imag[odd];
                float ti = wr * imag[odd] + wi * real[odd];

                real[odd] = real[even] - tr;
                imag[odd] = imag[even] - ti;
                real[even] = real[even] + tr;
                imag[even] = imag[even] + ti;
            }
        }
    }

    if (inverse != 0) {
        for (idx = 0u; idx < size; ++idx) {
            real[idx] *= scale;
            imag[idx] *= scale;
        }
    }
}

int fft_transform(struct fft_runtime *fft, int inverse)
{
    if (fft == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    fft_core(fft->real, fft->imag, (uint32_t)FFT_SIZE, inverse);
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
    fft_core(fft->real, fft->imag, (uint32_t)FFT_SIZE, 0);
    return DSP_OK;
}

int fft_plan_init(struct fft_plan *plan, uint32_t size)
{
    if (plan == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    memset(plan, 0, sizeof(*plan));
    if (size < 2u || (size & (size - 1u)) != 0u) {
        return DSP_ERR_RANGE;
    }
    plan->real = (float *)calloc((size_t)size, sizeof(float));
    plan->imag = (float *)calloc((size_t)size, sizeof(float));
    if (plan->real == NULL || plan->imag == NULL) {
        free(plan->real);
        free(plan->imag);
        memset(plan, 0, sizeof(*plan));
        return DSP_ERR_NO_MEMORY;
    }
    plan->size = size;
    return DSP_OK;
}

void fft_plan_destroy(struct fft_plan *plan)
{
    if (plan == NULL) {
        return;
    }
    free(plan->real);
    free(plan->imag);
    memset(plan, 0, sizeof(*plan));
}

int fft_plan_forward(struct fft_plan *plan, const float *input, uint32_t frames)
{
    uint32_t idx;

    if (plan == NULL || plan->real == NULL || input == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (frames != plan->size) {
        return DSP_ERR_INVALID_ARG;
    }
    for (idx = 0u; idx < plan->size; ++idx) {
        if (!isfinite(input[idx])) {
            return DSP_ERR_RANGE;
        }
        plan->real[idx] = input[idx];
        plan->imag[idx] = 0.0f;
    }
    fft_core(plan->real, plan->imag, plan->size, 0);
    return DSP_OK;
}

int fft_plan_inverse(struct fft_plan *plan)
{
    if (plan == NULL || plan->real == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    fft_core(plan->real, plan->imag, plan->size, 1);
    return DSP_OK;
}
