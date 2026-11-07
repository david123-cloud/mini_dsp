#ifndef MINI_DSP_DEMO_H
#define MINI_DSP_DEMO_H

#include <stdint.h>

/* Demo entry points.  main() only parses the command line; everything else
 * lives in these functions.  Both return 0 on success, non-zero on failure. */

/* Offline WAV round trip: read -> float -> pipeline(biquad, fixed-gain) ->
 * int16 -> write.
 *
 * block_frames == 0 processes the whole file in one pipeline call; any value
 * from 1 to the total frame count splits it into blocks of that size, with
 * module history carried across the block boundaries. */
int run_wav_demo(const char *input_path, const char *output_path, uint32_t block_frames);

/* 256-point FFT / IFFT experiment with a generated test tone. */
int run_fft_demo(void);

#endif /* MINI_DSP_DEMO_H */
