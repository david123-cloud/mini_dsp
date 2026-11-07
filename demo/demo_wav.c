#include "demo.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agc.h"
#include "biquad.h"
#include "dsp_block.h"
#include "dsp_buffer.h"
#include "dsp_convert.h"
#include "dsp_error.h"
#include "dsp_pipeline.h"
#include "wav.h"

/* Change to BIQUAD_LOWPASS to run the low-pass variant of the demo. */
#define DEMO_BIQUAD_TYPE BIQUAD_HIGHPASS
#define DEMO_BIQUAD_FC_HZ 100.0f
#define DEMO_BIQUAD_Q 0.70710678f

#define DEMO_PREVIEW_SAMPLES 8u

/* Releases a module the pipeline never accepted (ownership stayed with us). */
static void module_discard(struct dsp_module *module)
{
    if (module == NULL) {
        return;
    }
    if (module->ops != NULL && module->ops->destroy != NULL) {
        module->ops->destroy(module->priv);
    }
    free(module);
}

static void print_float_preview(const char *label, const float *samples, size_t count)
{
    size_t index;

    printf("%s:", label);
    for (index = 0u; index < count && index < (size_t)DEMO_PREVIEW_SAMPLES; ++index) {
        printf(" %+.6f", (double)samples[index]);
    }
    printf("\n");
}

static void print_pcm_preview(const char *label, const int16_t *samples, size_t count)
{
    size_t index;

    printf("%s:", label);
    for (index = 0u; index < count && index < (size_t)DEMO_PREVIEW_SAMPLES; ++index) {
        printf(" %d", (int)samples[index]);
    }
    printf("\n");
}

static float peak_abs(const float *samples, size_t count)
{
    float peak = 0.0f;
    size_t index;

    for (index = 0u; index < count; ++index) {
        float magnitude = samples[index] < 0.0f ? -samples[index] : samples[index];

        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    return peak;
}

int run_wav_demo(const char *input_path, const char *output_path, uint32_t block_frames)
{
    struct wav_file input;
    struct wav_file output;
    struct dsp_pipeline pipe;
    struct dsp_buffer in_view;
    struct dsp_buffer out_view;
    struct biquad_config cfg;
    struct dsp_module *biquad_module = NULL;
    struct dsp_module *agc_module = NULL;
    float *input_samples = NULL;
    float *output_samples = NULL;
    int16_t *output_pcm = NULL;
    size_t sample_count;
    uint32_t frames;
    uint32_t channels;
    uint32_t effective_block;
    int status;
    int exit_code = 1;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    memset(&pipe, 0, sizeof(pipe));
    memset(&in_view, 0, sizeof(in_view));
    memset(&out_view, 0, sizeof(out_view));

    if (input_path == NULL || output_path == NULL) {
        fprintf(stderr, "wav demo: missing path\n");
        return 1;
    }
    /* Never treat the input path as the default output, and never silently
     * overwrite the source file. */
    if (strcmp(input_path, output_path) == 0) {
        fprintf(stderr, "wav demo: refusing to write over the input file (%s)\n", input_path);
        return 1;
    }

    status = wav_read(input_path, &input);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: cannot read %s: %s\n", input_path, dsp_strerror(status));
        return 1;
    }

    frames = input.frames;
    channels = input.header.channels;
    if (channels == 0u || (size_t)frames > SIZE_MAX / (size_t)channels) {
        fprintf(stderr, "wav demo: unsupported frame/channel count\n");
        goto cleanup;
    }
    sample_count = (size_t)frames * (size_t)channels;
    if (sample_count > SIZE_MAX / sizeof(float)) {
        fprintf(stderr, "wav demo: sample count out of range\n");
        goto cleanup;
    }

    printf("input : %s (%u frames, %u ch, %u Hz, %u bit)\n", input_path, (unsigned)frames,
           (unsigned)channels, (unsigned)input.header.sample_rate,
           (unsigned)input.header.bits_per_sample);
    printf("output: %s (overwritten if it already exists)\n", output_path);

    input_samples = (float *)malloc(sample_count * sizeof(float));
    output_samples = (float *)malloc(sample_count * sizeof(float));
    output_pcm = (int16_t *)malloc(sample_count * sizeof(int16_t));
    if (input_samples == NULL || output_samples == NULL || output_pcm == NULL) {
        fprintf(stderr, "wav demo: %s\n", dsp_strerror(DSP_ERR_NO_MEMORY));
        goto cleanup;
    }

    dsp_int16_to_float(input.pcm, input_samples, sample_count);

    /* block_frames == 0 means "one block for the whole file". */
    effective_block = (block_frames == 0u || block_frames > frames) ? frames : block_frames;

    status = pipeline_init(&pipe, effective_block, channels);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: pipeline_init: %s\n", dsp_strerror(status));
        goto cleanup;
    }

    cfg.type = DEMO_BIQUAD_TYPE;
    cfg.fc = DEMO_BIQUAD_FC_HZ;
    cfg.q = DEMO_BIQUAD_Q;
    cfg.gain = 1.0f;

    biquad_module = biquad_module_create(&cfg, (int)input.header.sample_rate);
    if (biquad_module == NULL) {
        fprintf(stderr, "wav demo: biquad_module_create failed\n");
        goto cleanup;
    }
    status = pipeline_add(&pipe, biquad_module);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: pipeline_add(biquad): %s\n", dsp_strerror(status));
        goto cleanup;
    }
    biquad_module = NULL; /* owned by the pipeline from here on */

    agc_module = create_agc_module();
    if (agc_module == NULL) {
        fprintf(stderr, "wav demo: create_agc_module failed\n");
        goto cleanup;
    }
    status = pipeline_add(&pipe, agc_module);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: pipeline_add(agc): %s\n", dsp_strerror(status));
        goto cleanup;
    }
    agc_module = NULL; /* owned by the pipeline from here on */

    in_view.data = input_samples;
    in_view.frames = frames;
    in_view.channels = channels;

    out_view.data = output_samples;
    out_view.frames = frames;
    out_view.channels = channels;

    printf("processing: %u frame(s) per block, %u block(s)\n", (unsigned)effective_block,
           (unsigned)((frames + effective_block - 1u) / effective_block));

    /* History is carried across blocks by the pipeline itself. */
    status = dsp_block_process(&pipe, input_samples, output_samples, frames, effective_block);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: dsp_block_process: %s\n", dsp_strerror(status));
        goto cleanup;
    }

    status = dsp_float_to_int16(out_view.data, output_pcm, sample_count);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: output conversion rejected: %s\n", dsp_strerror(status));
        goto cleanup;
    }

    memset(&output.header, 0, sizeof(output.header));
    memcpy(output.header.riff, "RIFF", 4u);
    memcpy(output.header.wave, "WAVE", 4u);
    memcpy(output.header.fmt, "fmt ", 4u);
    memcpy(output.header.data, "data", 4u);
    output.header.fmt_size = 16u;
    output.header.format = (uint16_t)WAV_SUPPORTED_FORMAT;
    output.header.channels = (uint16_t)channels;
    output.header.sample_rate = input.header.sample_rate;
    output.header.block_align = (uint16_t)(channels * 2u);
    output.header.byte_rate = output.header.sample_rate * (uint32_t)output.header.block_align;
    output.header.bits_per_sample = (uint16_t)WAV_SUPPORTED_BITS;
    output.header.data_size = (uint32_t)(sample_count * 2u);
    output.header.file_size = 36u + output.header.data_size;

    output.pcm = output_pcm;
    output.frames = frames;

    status = wav_write(output_path, &output);
    if (status != DSP_OK) {
        fprintf(stderr, "wav demo: cannot write %s: %s\n", output_path, dsp_strerror(status));
        goto cleanup;
    }

    printf("processed: %u module(s), %u frames x %u ch\n", (unsigned)pipe.module_count,
           (unsigned)frames, (unsigned)channels);
    printf("dry/wet peak: input %.6f -> output %.6f\n", (double)peak_abs(in_view.data, sample_count),
           (double)peak_abs(out_view.data, sample_count));
    printf("buffer views: in.data=%p out.data=%p\n", (const void *)in_view.data,
           (const void *)out_view.data);
    print_float_preview("output float (from out.data)", out_view.data, sample_count);
    print_pcm_preview("output pcm   (written file)", output_pcm, sample_count);
    printf("wav demo: ok\n");

    exit_code = 0;

cleanup:
    free(input.pcm);
    free(input_samples);
    free(output_samples);
    free(output_pcm);
    module_discard(biquad_module);
    module_discard(agc_module);
    pipeline_destroy(&pipe);
    return exit_code;
}
