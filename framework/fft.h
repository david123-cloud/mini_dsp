#ifndef MINI_DSP_FFT_H
#define MINI_DSP_FFT_H

#include <stdint.h>

/* Fixed transform length: baseline-2, iterative, in place. */
#define FFT_SIZE 256

/*
 * Caller-owned FFT work area.  fft_transform() and fft_forward() never
 * allocate memory.
 */
struct fft_runtime {
    float real[FFT_SIZE];
    float imag[FFT_SIZE];
};

/*
 * Copies exactly FFT_SIZE real samples into real[], clears imag[], then runs
 * the forward transform.
 */
int fft_forward(struct fft_runtime *fft, const float *input, uint32_t frames);

/*
 * In-place bit-reversal + butterfly passes over the current complex array.
 *   inverse == 0 : forward, negative angle, no scaling
 *   inverse != 0 : inverse, positive angle, both real and imag divided by
 *                  FFT_SIZE at the end
 */
int fft_transform(struct fft_runtime *fft, int inverse);

/*
 * Runtime-sized transform for callers that cannot live with the fixed 256
 * points (for example a 960-frame period, which is not a multiple of 256).
 *
 * The size must be a power of two; the plan owns its buffers and must be
 * released with fft_plan_destroy().  setup allocates, processing does not.
 */
struct fft_plan {
    uint32_t size;
    float *real;
    float *imag;
};

int fft_plan_init(struct fft_plan *plan, uint32_t size);
void fft_plan_destroy(struct fft_plan *plan);

/* frames must equal the plan size. */
int fft_plan_forward(struct fft_plan *plan, const float *input, uint32_t frames);
int fft_plan_inverse(struct fft_plan *plan);

#endif /* MINI_DSP_FFT_H */
