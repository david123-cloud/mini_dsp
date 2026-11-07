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

#endif /* MINI_DSP_FFT_H */
