#include "demo.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "dsp_error.h"
#include "dsp_math.h"
#include "fft.h"

#define FFT_DEMO_SAMPLE_RATE 48000.0f
#define FFT_DEMO_AMPLITUDE 0.5f
#define FFT_DEMO_TONE_HZ 1000.0f
#define FFT_DEMO_INTEGER_BIN_HZ 1500.0f /* 1500 * 256 / 48000 = bin 8 exactly */
#define FFT_DEMO_INTEGER_BIN 8u
#define FFT_DEMO_BIN_COUNT ((uint32_t)FFT_SIZE / 2u + 1u)

#define FFT_DEMO_ROUNDTRIP_TOLERANCE 1e-4f
#define FFT_DEMO_LEAKAGE_TOLERANCE 1e-3f
#define FFT_DEMO_PEAK_TOLERANCE 0.15f

static void fill_tone(float *target, uint32_t frames, float frequency, float amplitude)
{
    uint32_t index;

    for (index = 0u; index < frames; ++index) {
        target[index] =
            amplitude * sinf(2.0f * DSP_PI_F * frequency * (float)index / FFT_DEMO_SAMPLE_RATE);
    }
}

static int all_finite(const float *data, uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; ++index) {
        if (!isfinite(data[index])) {
            return 0;
        }
    }
    return 1;
}

/* amplitude = magnitude / N, doubled except for DC and Nyquist. */
static float single_sided_amplitude(const struct fft_runtime *fft, uint32_t bin)
{
    float magnitude = hypotf(fft->real[bin], fft->imag[bin]);
    float amplitude = magnitude / (float)FFT_SIZE;

    if (bin != 0u && bin != (uint32_t)FFT_SIZE / 2u) {
        amplitude *= 2.0f;
    }
    return amplitude;
}

static int run_roundtrip_test(const float *input, const struct fft_runtime *spectrum)
{
    struct fft_runtime work;
    float max_real_error = 0.0f;
    float max_imag_error = 0.0f;
    uint32_t index;

    work = *spectrum;
    /* Halve every complex bin so the inverse must reproduce 0.5 * input. */
    for (index = 0u; index < (uint32_t)FFT_SIZE; ++index) {
        work.real[index] *= 0.5f;
        work.imag[index] *= 0.5f;
    }

    if (fft_transform(&work, 1) != DSP_OK) {
        printf("IFFT: failed\n");
        return 1;
    }

    for (index = 0u; index < (uint32_t)FFT_SIZE; ++index) {
        float expected = 0.5f * input[index];
        float real_error = fabsf(work.real[index] - expected);
        float imag_error = fabsf(work.imag[index]);

        if (!isfinite(work.real[index]) || !isfinite(work.imag[index])) {
            printf("IFFT: non-finite sample at %u\n", (unsigned)index);
            return 1;
        }
        if (real_error > max_real_error) {
            max_real_error = real_error;
        }
        if (imag_error > max_imag_error) {
            max_imag_error = imag_error;
        }
    }

    printf("ifft vs 0.5*input: max |real error| = %.3e, max |imag residual| = %.3e\n",
           (double)max_real_error, (double)max_imag_error);
    printf("ifft first samples:");
    for (index = 0u; index < 4u; ++index) {
        printf(" %+.6f", (double)work.real[index]);
    }
    printf("\n");

    if (max_real_error > FFT_DEMO_ROUNDTRIP_TOLERANCE ||
        max_imag_error > FFT_DEMO_ROUNDTRIP_TOLERANCE) {
        printf("roundtrip check: FAILED (tolerance %.1e)\n", (double)FFT_DEMO_ROUNDTRIP_TOLERANCE);
        return 1;
    }
    printf("roundtrip check: ok\n");
    return 0;
}

static int run_integer_bin_test(void)
{
    float input[FFT_SIZE];
    struct fft_runtime fft;
    uint32_t bin;
    float expected;
    int failed = 0;

    fill_tone(input, (uint32_t)FFT_SIZE, FFT_DEMO_INTEGER_BIN_HZ, FFT_DEMO_AMPLITUDE);
    if (!all_finite(input, (uint32_t)FFT_SIZE)) {
        printf("integer-bin: generated input is not finite\n");
        return 1;
    }
    if (fft_forward(&fft, input, (uint32_t)FFT_SIZE) != DSP_OK) {
        printf("integer-bin: forward FFT failed\n");
        return 1;
    }

    expected = single_sided_amplitude(&fft, FFT_DEMO_INTEGER_BIN);
    printf("integer bin %u (%.2f Hz): amplitude = %.7f (expected %.7f)\n",
           (unsigned)FFT_DEMO_INTEGER_BIN,
           (double)((float)FFT_DEMO_INTEGER_BIN * FFT_DEMO_SAMPLE_RATE / (float)FFT_SIZE),
           (double)expected, (double)FFT_DEMO_AMPLITUDE);

    if (fabsf(expected - FFT_DEMO_AMPLITUDE) > FFT_DEMO_LEAKAGE_TOLERANCE) {
        printf("integer-bin: peak amplitude out of tolerance\n");
        failed = 1;
    }

    for (bin = 0u; bin < FFT_DEMO_BIN_COUNT; ++bin) {
        float amplitude;

        if (bin == FFT_DEMO_INTEGER_BIN) {
            continue;
        }
        amplitude = single_sided_amplitude(&fft, bin);
        if (!isfinite(amplitude)) {
            printf("integer-bin: non-finite amplitude at bin %u\n", (unsigned)bin);
            failed = 1;
            continue;
        }
        if (amplitude > FFT_DEMO_LEAKAGE_TOLERANCE) {
            printf("integer-bin: leakage at bin %u = %.3e\n", (unsigned)bin, (double)amplitude);
            failed = 1;
        }
    }

    printf("integer-bin check: %s\n", failed ? "FAILED" : "ok");
    return failed;
}

int run_fft_demo(void)
{
    float input[FFT_SIZE];
    struct fft_runtime fft;
    uint32_t bin;
    uint32_t peak_bin = 0u;
    float peak_amplitude = 0.0f;

    printf("fft demo: %d-point FFT, fs = %.0f Hz, tone = %.0f Hz, amplitude = %.2f\n", FFT_SIZE,
           (double)FFT_DEMO_SAMPLE_RATE, (double)FFT_DEMO_TONE_HZ, (double)FFT_DEMO_AMPLITUDE);

    fill_tone(input, (uint32_t)FFT_SIZE, FFT_DEMO_TONE_HZ, FFT_DEMO_AMPLITUDE);
    if (!all_finite(input, (uint32_t)FFT_SIZE)) {
        printf("fft demo: generated input is not finite\n");
        return 1;
    }

    if (fft_forward(&fft, input, (uint32_t)FFT_SIZE) != DSP_OK) {
        printf("fft demo: forward FFT failed\n");
        return 1;
    }

    printf("bins 0..%u (single sided):\n", (unsigned)(FFT_DEMO_BIN_COUNT - 1u));
    for (bin = 0u; bin < FFT_DEMO_BIN_COUNT; ++bin) {
        float amplitude = single_sided_amplitude(&fft, bin);
        float frequency = (float)bin * FFT_DEMO_SAMPLE_RATE / (float)FFT_SIZE;

        if (!isfinite(fft.real[bin]) || !isfinite(fft.imag[bin]) || !isfinite(amplitude)) {
            printf("fft demo: non-finite bin %u\n", (unsigned)bin);
            return 1;
        }
        if (amplitude > peak_amplitude) {
            peak_amplitude = amplitude;
            peak_bin = bin;
        }
        printf("  bin %3u  %9.2f Hz  amplitude %12.7f\n", (unsigned)bin, (double)frequency,
               (double)amplitude);
    }

    /* 1000 Hz sits at bin 5.333, so it leaks by design: expect the energy to
     * peak at bin 5, not at an exact 0.5 amplitude. */
    printf("peak bin = %u (%.2f Hz, %.2f bins from %.0f Hz), amplitude = %.7f\n",
           (unsigned)peak_bin, (double)((float)peak_bin * FFT_DEMO_SAMPLE_RATE / (float)FFT_SIZE),
           (double)((float)peak_bin - FFT_DEMO_TONE_HZ * (float)FFT_SIZE / FFT_DEMO_SAMPLE_RATE),
           (double)FFT_DEMO_TONE_HZ, (double)peak_amplitude);

    if (peak_bin != 5u) {
        printf("fft demo: FAILED, expected the peak at bin 5\n");
        return 1;
    }
    if (fabsf(peak_amplitude - FFT_DEMO_AMPLITUDE) > FFT_DEMO_PEAK_TOLERANCE) {
        printf("fft demo: FAILED, peak amplitude %.7f is far from %.2f\n", (double)peak_amplitude,
               (double)FFT_DEMO_AMPLITUDE);
        return 1;
    }

    if (run_roundtrip_test(input, &fft) != 0) {
        return 1;
    }
    if (run_integer_bin_test() != 0) {
        return 1;
    }

    printf("fft demo: ok\n");
    return 0;
}
