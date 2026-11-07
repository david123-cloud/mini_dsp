#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"

/* Paths are relative to the current working directory; run from the project
 * root (the README documents this). */
#define DEMO_DEFAULT_INPUT "tests/temp.wav"
#define DEMO_DEFAULT_OUTPUT "tests/output.wav"

static int print_usage(const char *program)
{
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s                            run the WAV demo with default paths\n",
            program);
    fprintf(stderr, "  %s wav                        same as no arguments\n", program);
    fprintf(stderr, "  %s wav <input> <output>       run the WAV demo on explicit paths\n",
            program);
    fprintf(stderr, "  %s wav <input> <output> <n>   process in blocks of n frames\n", program);
    fprintf(stderr, "  %s fft                        run the FFT/IFFT demo\n", program);
    fprintf(stderr, "defaults: input=%s output=%s\n", DEMO_DEFAULT_INPUT, DEMO_DEFAULT_OUTPUT);
    fprintf(stderr, "an explicitly given output file is overwritten if it exists\n");
    fprintf(stderr, "omitting <n> processes the whole file in one block\n");
    return 1;
}

/* Parses a positive block size; rejects empty, trailing garbage and overflow. */
static int parse_block_frames(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || out == NULL) {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || end == NULL || *end != '\0') {
        return 0;
    }
    if (value == 0ul || value > 0xFFFFFFFFul) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    const char *program = (argc > 0) ? argv[0] : "dsp_demo";
    uint32_t block_frames = 0u;

    if (argc == 1) {
        /* No arguments: documented default run of the WAV demo. */
        return run_wav_demo(DEMO_DEFAULT_INPUT, DEMO_DEFAULT_OUTPUT, 0u);
    }

    if (strcmp(argv[1], "fft") == 0) {
        if (argc != 2) {
            return print_usage(program);
        }
        return run_fft_demo();
    }

    if (strcmp(argv[1], "wav") == 0) {
        if (argc == 2) {
            return run_wav_demo(DEMO_DEFAULT_INPUT, DEMO_DEFAULT_OUTPUT, 0u);
        }
        if (argc == 4) {
            return run_wav_demo(argv[2], argv[3], 0u);
        }
        if (argc == 5) {
            if (!parse_block_frames(argv[4], &block_frames)) {
                fprintf(stderr, "invalid block size: %s\n", argv[4]);
                return print_usage(program);
            }
            return run_wav_demo(argv[2], argv[3], block_frames);
        }
        return print_usage(program);
    }

    return print_usage(program);
}
