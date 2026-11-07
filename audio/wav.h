#ifndef MINI_DSP_WAV_H
#define MINI_DSP_WAV_H

#include <stdint.h>

/* Version 1 scope: RIFF/WAVE, integer PCM, mono, 16-bit, 48000 Hz,
 * fmt chunk of exactly 16 bytes immediately followed by the data chunk.
 * Anything else is rejected instead of being silently "decoded". */
#define WAV_SUPPORTED_SAMPLE_RATE 48000u
#define WAV_SUPPORTED_CHANNELS 1u
#define WAV_SUPPORTED_BITS 16u
#define WAV_SUPPORTED_FORMAT 1u
#define WAV_HEADER_SIZE 44u
#define WAV_MAX_DATA_BYTES (16u * 1024u * 1024u)

/* On-disk header fields, decoded to host integers.  The byte layout is
 * described by wav.h and produced by wav.c; it is never memcpy'd from the
 * file, so struct padding and host endianness do not matter. */
struct wav_header {
    char riff[4];
    uint32_t file_size; /* RIFF chunk size, i.e. 36 + data_size */
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
};

struct wav_file {
    int16_t *pcm; /* owned by the caller after a successful wav_read */
    uint32_t frames;
    struct wav_header header;
};

/*
 * Reads a supported WAV file.  `wav` must be zero-initialised and must not
 * already hold PCM.  On success the caller owns wav->pcm and frees it; on
 * failure everything allocated here is released and wav is left untouched.
 *
 * Trailing metadata after the data chunk is tolerated: the RIFF size field is
 * not required to be exactly 36 + data_size, because bytes may legally follow
 * the PCM payload.
 */
int wav_read(const char *path, struct wav_file *wav);

/*
 * Writes a supported WAV file, regenerating a canonical 44-byte header from
 * the struct fields rather than copying the caller's header bytes.  The
 * caller's PCM is never modified or freed.  The target file is overwritten if
 * it exists; callers must make that choice explicit.
 */
int wav_write(const char *path, const struct wav_file *wav);

#endif /* MINI_DSP_WAV_H */
