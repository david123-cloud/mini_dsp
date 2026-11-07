#include "wav.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_error.h"

static uint16_t read_u16le(const unsigned char *raw)
{
    return (uint16_t)((uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8));
}

static uint32_t read_u32le(const unsigned char *raw)
{
    return (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) |
           ((uint32_t)raw[3] << 24);
}

static void write_u16le(unsigned char *raw, uint16_t value)
{
    raw[0] = (unsigned char)(value & 0xFFu);
    raw[1] = (unsigned char)((value >> 8) & 0xFFu);
}

static void write_u32le(unsigned char *raw, uint32_t value)
{
    raw[0] = (unsigned char)(value & 0xFFu);
    raw[1] = (unsigned char)((value >> 8) & 0xFFu);
    raw[2] = (unsigned char)((value >> 16) & 0xFFu);
    raw[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static int header_is_supported(const struct wav_header *header)
{
    if (memcmp(header->riff, "RIFF", 4u) != 0 || memcmp(header->wave, "WAVE", 4u) != 0) {
        return DSP_ERR_FORMAT;
    }
    /* fmt must come first and be exactly 16 bytes: no extra leading chunk. */
    if (memcmp(header->fmt, "fmt ", 4u) != 0 || header->fmt_size != 16u) {
        return DSP_ERR_FORMAT;
    }
    if ((unsigned)header->format != WAV_SUPPORTED_FORMAT) {
        return DSP_ERR_UNSUPPORTED;
    }
    if ((unsigned)header->channels != WAV_SUPPORTED_CHANNELS ||
        (unsigned)header->bits_per_sample != WAV_SUPPORTED_BITS) {
        return DSP_ERR_UNSUPPORTED;
    }
    if (header->sample_rate != WAV_SUPPORTED_SAMPLE_RATE) {
        return DSP_ERR_UNSUPPORTED;
    }
    if ((uint32_t)header->block_align !=
        (uint32_t)WAV_SUPPORTED_CHANNELS * (uint32_t)(WAV_SUPPORTED_BITS / 8u)) {
        return DSP_ERR_FORMAT;
    }
    if (header->byte_rate != header->sample_rate * (uint32_t)header->block_align) {
        return DSP_ERR_FORMAT;
    }
    if (memcmp(header->data, "data", 4u) != 0) {
        return DSP_ERR_FORMAT;
    }
    if (header->data_size == 0u) {
        return DSP_ERR_RANGE;
    }
    if (header->data_size > WAV_MAX_DATA_BYTES) {
        return DSP_ERR_RANGE;
    }
    if ((header->data_size % (uint32_t)header->block_align) != 0u) {
        return DSP_ERR_FORMAT;
    }
    return DSP_OK;
}

int wav_read(const char *path, struct wav_file *wav)
{
    FILE *fp = NULL;
    unsigned char raw[WAV_HEADER_SIZE];
    unsigned char *payload = NULL;
    struct wav_header header;
    int16_t *pcm = NULL;
    uint32_t frames;
    int status;
    size_t got;

    if (path == NULL || wav == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (wav->pcm != NULL) {
        return DSP_ERR_STATE; /* caller must hand over a fresh object */
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return DSP_ERR_IO;
    }

    got = fread(raw, 1u, (size_t)WAV_HEADER_SIZE, fp);
    if (got != (size_t)WAV_HEADER_SIZE) {
        status = DSP_ERR_IO;
        goto done;
    }

    memcpy(header.riff, raw + 0, 4u);
    header.file_size = read_u32le(raw + 4);
    memcpy(header.wave, raw + 8, 4u);
    memcpy(header.fmt, raw + 12, 4u);
    header.fmt_size = read_u32le(raw + 16);
    header.format = read_u16le(raw + 20);
    header.channels = read_u16le(raw + 22);
    header.sample_rate = read_u32le(raw + 24);
    header.byte_rate = read_u32le(raw + 28);
    header.block_align = read_u16le(raw + 32);
    header.bits_per_sample = read_u16le(raw + 34);
    memcpy(header.data, raw + 36, 4u);
    header.data_size = read_u32le(raw + 40);

    status = header_is_supported(&header);
    if (status != DSP_OK) {
        goto done;
    }

    /* Deliberately does not compare header.file_size with 36 + data_size. */
    frames = header.data_size / (uint32_t)header.block_align;

    payload = (unsigned char *)malloc((size_t)header.data_size);
    if (payload == NULL) {
        status = DSP_ERR_NO_MEMORY;
        goto done;
    }
    got = fread(payload, 1u, (size_t)header.data_size, fp);
    if (got != (size_t)header.data_size) {
        status = DSP_ERR_IO;
        goto done;
    }

    pcm = (int16_t *)malloc((size_t)frames * sizeof(int16_t));
    if (pcm == NULL) {
        status = DSP_ERR_NO_MEMORY;
        goto done;
    }
    {
        uint32_t index;

        for (index = 0u; index < frames; ++index) {
            uint16_t word = read_u16le(payload + (size_t)index * 2u);
            int32_t value = (int32_t)word;

            /* Two's complement: 0x8000..0xFFFF maps to -32768..-1 without
             * relying on implementation-defined narrowing. */
            if (value >= 32768) {
                value -= 65536;
            }
            pcm[index] = (int16_t)value;
        }
    }

    if (fclose(fp) != 0) {
        fp = NULL;
        status = DSP_ERR_IO;
        goto done;
    }
    fp = NULL;

    wav->pcm = pcm;
    wav->frames = frames;
    wav->header = header;
    free(payload);
    return DSP_OK;

done:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    free(payload);
    free(pcm);
    return status;
}

int wav_write(const char *path, const struct wav_file *wav)
{
    FILE *fp = NULL;
    unsigned char raw[WAV_HEADER_SIZE];
    unsigned char *payload = NULL;
    size_t data_size;
    size_t index;
    uint32_t data_size32;
    int status = DSP_OK;

    if (path == NULL || wav == NULL || wav->pcm == NULL) {
        return DSP_ERR_INVALID_ARG;
    }
    if (wav->frames == 0u) {
        return DSP_ERR_RANGE;
    }
    if ((unsigned)wav->header.channels != WAV_SUPPORTED_CHANNELS ||
        (unsigned)wav->header.bits_per_sample != WAV_SUPPORTED_BITS) {
        return DSP_ERR_UNSUPPORTED;
    }
    if ((unsigned)wav->header.format != WAV_SUPPORTED_FORMAT) {
        return DSP_ERR_UNSUPPORTED;
    }
    if (wav->header.sample_rate != WAV_SUPPORTED_SAMPLE_RATE) {
        return DSP_ERR_UNSUPPORTED;
    }

    /* 16-bit mono only, so the payload is exactly frames * 2 bytes; the cap is
     * checked against the frame count to keep the arithmetic overflow-safe. */
    if (wav->frames > (uint32_t)(WAV_MAX_DATA_BYTES / 2u)) {
        return DSP_ERR_RANGE;
    }
    data_size = (size_t)wav->frames * 2u;
    data_size32 = (uint32_t)data_size;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return DSP_ERR_IO;
    }

    memcpy(raw + 0, "RIFF", 4u);
    write_u32le(raw + 4, 36u + data_size32);
    memcpy(raw + 8, "WAVE", 4u);
    memcpy(raw + 12, "fmt ", 4u);
    write_u32le(raw + 16, 16u);
    write_u16le(raw + 20, (uint16_t)wav->header.format);
    write_u16le(raw + 22, (uint16_t)wav->header.channels);
    write_u32le(raw + 24, wav->header.sample_rate);
    write_u32le(raw + 28, wav->header.sample_rate * (uint32_t)(wav->header.channels * (WAV_SUPPORTED_BITS / 8u)));
    write_u16le(raw + 32, (uint16_t)(wav->header.channels * (WAV_SUPPORTED_BITS / 8u)));
    write_u16le(raw + 34, (uint16_t)wav->header.bits_per_sample);
    memcpy(raw + 36, "data", 4u);
    write_u32le(raw + 40, data_size32);

    if (fwrite(raw, 1u, (size_t)WAV_HEADER_SIZE, fp) != (size_t)WAV_HEADER_SIZE) {
        status = DSP_ERR_IO;
        goto done;
    }

    payload = (unsigned char *)malloc(data_size);
    if (payload == NULL) {
        status = DSP_ERR_NO_MEMORY;
        goto done;
    }
    for (index = 0u; index < (size_t)wav->frames; ++index) {
        uint16_t word = (uint16_t)(int32_t)wav->pcm[index]; /* modulo 2^16 */

        write_u16le(payload + index * 2u, word);
    }
    if (fwrite(payload, 1u, data_size, fp) != data_size) {
        status = DSP_ERR_IO;
        goto done;
    }

done:
    free(payload);
    if (fp != NULL && fclose(fp) != 0 && status == DSP_OK) {
        status = DSP_ERR_IO;
    }
    return status;
}
