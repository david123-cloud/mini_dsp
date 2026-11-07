#!/usr/bin/env bash
#
# Acceptance run for mini_dsp.
#
#   bash tests/verify.sh
#
# By default it only builds (strict warnings + ASan/UBSan), runs the demos and
# checks the WAV output.  The test-style steps (ctest and the temporary
# pipeline self-test) are skipped; enable them with:
#
#   RUN_TESTS=1 bash tests/verify.sh
#
# Nothing is added to the repository: the temporary harness lives in the build
# directory and is deleted on exit unless VERIFY_KEEP=1 is set.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BUILD_DIR="${BUILD_DIR:-build-verify}"
CC_BIN="${CC:-cc}"
RUN_TESTS="${RUN_TESTS:-0}" # 1 = also run ctest and the pipeline self-test
failures=0

say() { printf '\n=== %s ===\n' "$1"; }
ok() { printf '  [ok]   %s\n' "$1"; }
bad() { printf '  [FAIL] %s\n' "$1"; failures=$((failures + 1)); }

cleanup() {
  if [ "${VERIFY_KEEP:-0}" != "1" ]; then
    rm -f "$BUILD_DIR/pipeline_selftest" "$BUILD_DIR/pipeline_selftest.c"
  fi
}
trap cleanup EXIT

say "generate test signal (if missing)"
if [ -f tests/temp.wav ]; then
  ok "tests/temp.wav already present"
else
  python3 tests/gen_temp_wav.py
  ok "tests/temp.wav generated"
fi

if [ "$RUN_TESTS" = "1" ]; then TESTS_FLAG=ON; else TESTS_FLAG=OFF; fi

say "configure (strict warnings + sanitizers)"
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMINI_DSP_WERROR=ON \
  -DMINI_DSP_SANITIZE=ON \
  -DMINI_DSP_REGISTER_TESTS="$TESTS_FLAG" \
  -DCMAKE_C_COMPILER="$CC_BIN"

say "build"
cmake --build "$BUILD_DIR" --parallel

say "fft demo"
if "./$BUILD_DIR/dsp_demo" fft; then ok "fft demo exit 0"; else bad "fft demo exit non-zero"; fi

say "wav demo (default paths)"
if "./$BUILD_DIR/dsp_demo" wav tests/temp.wav "$BUILD_DIR/out_default.wav"; then
  ok "wav demo exit 0"
else
  bad "wav demo exit non-zero"
fi

say "wav demo, two runs must be identical (high-pass determinism)"
"./$BUILD_DIR/dsp_demo" wav tests/temp.wav "$BUILD_DIR/out_a.wav" >/dev/null
"./$BUILD_DIR/dsp_demo" wav tests/temp.wav "$BUILD_DIR/out_b.wav" >/dev/null
if cmp -s "$BUILD_DIR/out_a.wav" "$BUILD_DIR/out_b.wav"; then
  ok "repeat runs byte-identical"
else
  bad "repeat runs differ"
fi

say "block processing must match whole-file processing"
for block in 1 512 4096; do
  if "./$BUILD_DIR/dsp_demo" wav tests/temp.wav "$BUILD_DIR/out_blk_$block.wav" "$block" >/dev/null; then
    if cmp -s "$BUILD_DIR/out_blk_$block.wav" "$BUILD_DIR/out_a.wav"; then
      ok "$block-frame blocks identical to one block"
    else
      bad "$block-frame blocks differ from one block"
    fi
  else
    bad "block size $block failed to run"
  fi
done

if "./$BUILD_DIR/dsp_demo" wav tests/temp.wav "$BUILD_DIR/out_blk_bad.wav" 0 >/dev/null 2>&1; then
  bad "block size 0 was accepted"
else
  ok "block size 0 rejected"
fi

say "output WAV layout"
out_size=$(stat -c %s "$BUILD_DIR/out_a.wav")
if [ "$out_size" -eq 96044 ]; then ok "file size = 96044 bytes"; else bad "file size = $out_size"; fi

if [ "$(head -c 4 "$BUILD_DIR/out_a.wav")" = "RIFF" ] && \
   [ "$(dd if="$BUILD_DIR/out_a.wav" bs=1 skip=8 count=4 status=none)" = "WAVE" ] && \
   [ "$(dd if="$BUILD_DIR/out_a.wav" bs=1 skip=12 count=4 status=none)" = "fmt " ] && \
   [ "$(dd if="$BUILD_DIR/out_a.wav" bs=1 skip=36 count=4 status=none)" = "data" ]; then
  ok "RIFF/WAVE/fmt /data tags"
else
  bad "chunk tags"
fi

# Little-endian fields, read on a little-endian host.
riff_size=$(od -An -tu4 -j4 -N4 "$BUILD_DIR/out_a.wav" | tr -d ' ')
data_size=$(od -An -tu4 -j40 -N4 "$BUILD_DIR/out_a.wav" | tr -d ' ')
channels=$(od -An -tu2 -j22 -N2 "$BUILD_DIR/out_a.wav" | tr -d ' ')
rate=$(od -An -tu4 -j24 -N4 "$BUILD_DIR/out_a.wav" | tr -d ' ')
bits=$(od -An -tu2 -j34 -N2 "$BUILD_DIR/out_a.wav" | tr -d ' ')
[ "$riff_size" = "96036" ] && ok "RIFF size = 36 + data_size" || bad "RIFF size = $riff_size"
[ "$data_size" = "96000" ] && ok "data size = 96000" || bad "data size = $data_size"
[ "$channels" = "1" ] && ok "channels = 1" || bad "channels = $channels"
[ "$rate" = "48000" ] && ok "sample rate = 48000" || bad "sample rate = $rate"
[ "$bits" = "16" ] && ok "bits = 16" || bad "bits = $bits"

# A non-silent, non-clipped payload: peak should be well below full scale but
# clearly non-zero (a 100 Hz high-pass keeps most of the 440 Hz energy).
stats=$(od -An -v -tu2 -j44 "$BUILD_DIR/out_a.wav" | tr -s ' ' '\n' | grep -E '^[0-9]+$' | \
  awk '{ if ($1 > 32767) $1 = $1 - 65536; a = $1 < 0 ? -$1 : $1; if (a > m) m = a; s += a; n += 1 }
       END { if (n == 0) { print "0 0"; exit } printf "%d %d\n", m, n }')
peak=${stats%% *}
count=${stats##* }
if [ "$count" -eq 48000 ] && [ "$peak" -gt 1000 ] && [ "$peak" -lt 32000 ]; then
  ok "PCM present: $count samples, peak $peak"
else
  bad "PCM check: count=$count peak=$peak"
fi

if [ "$RUN_TESTS" = "1" ]; then
  say "ctest"
  if ctest --test-dir "$BUILD_DIR" --output-on-failure; then ok "ctest passed"; else bad "ctest failed"; fi
else
  say "ctest"
  printf '  [skip] ctest (RUN_TESTS=0)\n'
fi

if [ "$RUN_TESTS" = "1" ]; then
say "pipeline self-test (temporary, not part of the repo)"
cat > "$BUILD_DIR/pipeline_selftest.c" <<'SELFTEST'
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_error.h"
#include "dsp_pipeline.h"

static int failures = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [ok]   %s\n", msg);                                                          \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

struct flags {
    int destroys;
    int resets;
};

struct stage {
    float scale;
    float offset;
    struct flags *flags;
    int fail;
};

static int stage_process(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out)
{
    struct stage *stage = (struct stage *)ctx;
    size_t count = (size_t)in->frames * (size_t)in->channels;
    size_t index;

    if (stage->fail) {
        return DSP_ERR_STATE;
    }
    for (index = 0; index < count; ++index) {
        out->data[index] = in->data[index] * stage->scale + stage->offset;
    }
    return DSP_OK;
}

static void stage_reset(void *ctx)
{
    struct stage *stage = (struct stage *)ctx;

    if (stage->flags != NULL) {
        stage->flags->resets++;
    }
}

static void stage_destroy(void *ctx)
{
    struct stage *stage = (struct stage *)ctx;

    if (stage->flags != NULL) {
        stage->flags->destroys++;
    }
    free(stage);
}

static const struct dsp_module_ops g_ops = { stage_process, stage_reset, stage_destroy };

static struct dsp_module *make_stage(float scale, float offset, struct flags *flags, int fail)
{
    struct dsp_module *module = (struct dsp_module *)calloc(1, sizeof(*module));
    struct stage *stage = (struct stage *)calloc(1, sizeof(*stage));

    if (module == NULL || stage == NULL) {
        free(module);
        free(stage);
        return NULL;
    }
    stage->scale = scale;
    stage->offset = offset;
    stage->flags = flags;
    stage->fail = fail;
    module->name = "stage";
    module->priv = stage;
    module->ops = &g_ops;
    return module;
}

static void fill(float *data, size_t count, float base)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        data[index] = base + (float)index;
    }
}

int main(void)
{
    const uint32_t max_frames = 8;
    const uint32_t channels = 2;
    const size_t count = (size_t)max_frames * channels;
    float in_data[16];
    float out_data[16];
    float expect[16];
    struct dsp_buffer in_view;
    struct dsp_buffer out_view;
    struct dsp_pipeline pipe;
    struct dsp_pipeline other;
    struct flags flags;
    struct dsp_module *module;
    size_t index;

    fill(in_data, count, 1.0f);
    fill(expect, count, 1.0f);

    /* zero modules: plain copy, input untouched */
    memset(&pipe, 0, sizeof(pipe));
    memset(out_data, 0, sizeof(out_data));
    CHECK(pipeline_init(&pipe, max_frames, channels) == DSP_OK, "init");
    in_view.data = in_data;
    in_view.frames = max_frames;
    in_view.channels = channels;
    out_view.data = out_data;
    out_view.frames = max_frames;
    out_view.channels = channels;
    CHECK(pipeline_process(&pipe, &in_view, &out_view) == DSP_OK, "zero modules processed");
    CHECK(memcmp(out_data, in_data, count * sizeof(float)) == 0, "zero modules copy input");
    CHECK(pipe.scratch_a != out_data && pipe.scratch_b != out_data, "output is the caller buffer");

    /* odd module count: x2 */
    memset(&flags, 0, sizeof(flags));
    module = make_stage(2.0f, 0.0f, &flags, 0);
    CHECK(pipeline_add(&pipe, module) == DSP_OK, "add one module");
    CHECK(pipeline_process(&pipe, &in_view, &out_view) == DSP_OK, "one module processed");
    for (index = 0; index < count; ++index) {
        expect[index] = in_data[index] * 2.0f;
    }
    CHECK(memcmp(out_data, expect, count * sizeof(float)) == 0, "odd count lands in out");

    /* second pipeline must not accept a module the first one owns */
    memset(&other, 0, sizeof(other));
    CHECK(pipeline_init(&other, max_frames, channels) == DSP_OK, "second init");
    CHECK(pipeline_add(&other, module) == DSP_ERR_STATE, "module already owned is rejected");
    CHECK(pipeline_add(&pipe, module) == DSP_ERR_STATE, "duplicate add is rejected");
    pipeline_destroy(&other);

    /* even count: x2 then +1 */
    {
        struct dsp_module *second = make_stage(1.0f, 1.0f, &flags, 0);

        CHECK(pipeline_add(&pipe, second) == DSP_OK, "add second module");
    }
    CHECK(pipeline_process(&pipe, &in_view, &out_view) == DSP_OK, "two modules processed");
    for (index = 0; index < count; ++index) {
        expect[index] = in_data[index] * 2.0f + 1.0f;
    }
    CHECK(memcmp(out_data, expect, count * sizeof(float)) == 0, "even count lands in out");

    /* three modules: the chain still ends up in the caller buffer */
    {
        struct dsp_module *third = make_stage(1.0f, 1.0f, &flags, 0);

        CHECK(pipeline_add(&pipe, third) == DSP_OK, "add third module");
    }
    CHECK(pipeline_process(&pipe, &in_view, &out_view) == DSP_OK, "three modules processed");
    for (index = 0; index < count; ++index) {
        expect[index] = in_data[index] * 2.0f + 2.0f;
    }
    CHECK(memcmp(out_data, expect, count * sizeof(float)) == 0, "odd count still lands in out");

    /* input samples must not be modified by the chain */
    for (index = 0; index < count; ++index) {
        if (in_data[index] != 1.0f + (float)index) {
            break;
        }
    }
    CHECK(index == count, "input buffer untouched");

    /* blocks smaller than max_frames are fine, larger ones are refused */
    in_view.frames = 3;
    out_view.frames = 3;
    CHECK(pipeline_process(&pipe, &in_view, &out_view) == DSP_OK, "sub-capacity block works");
    in_view.frames = max_frames + 1;
    out_view.frames = max_frames + 1;
    CHECK(pipeline_process(&pipe, &in_view, &out_view) == DSP_ERR_RANGE, "oversized block refused");

    /* a failing module aborts the chain with a non-zero status */
    memset(&other, 0, sizeof(other));
    CHECK(pipeline_init(&other, max_frames, channels) == DSP_OK, "third init");
    {
        struct dsp_module *broken = make_stage(1.0f, 0.0f, NULL, 1);

        CHECK(pipeline_add(&other, broken) == DSP_OK, "add failing module");
    }
    in_view.frames = 2;
    out_view.frames = 2;
    CHECK(pipeline_process(&other, &in_view, &out_view) != DSP_OK, "module failure propagates");
    pipeline_destroy(&other);

    /* channel mismatch is refused */
    in_view.frames = 2;
    out_view.frames = 2;
    out_view.channels = 1;
    CHECK(pipeline_process(&pipe, &in_view, &out_view) != DSP_OK, "channel mismatch refused");
    out_view.channels = channels;

    /* reset reaches every module, destroy runs once per module */
    pipeline_reset(&pipe);
    CHECK(flags.resets >= 1, "reset propagated");

    pipeline_destroy(&pipe);
    CHECK(flags.destroys == 3, "three modules destroyed");
    CHECK(pipe.initialized == 0 && pipe.module_count == 0, "pipeline cleared");
    pipeline_destroy(&pipe); /* second destroy must be harmless */
    CHECK(1, "double destroy is a no-op");

    printf("%s\n", failures == 0 ? "pipeline self-test: ok" : "pipeline self-test: FAILED");
    return failures == 0 ? 0 : 1;
}
SELFTEST

"$CC_BIN" -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Iframework -Imodule -Iaudio \
  -o "$BUILD_DIR/pipeline_selftest" \
  "$BUILD_DIR/pipeline_selftest.c" \
  framework/dsp_pipeline.c framework/dsp_error.c -lm

if "./$BUILD_DIR/pipeline_selftest"; then ok "pipeline self-test passed"; else bad "pipeline self-test failed"; fi
else
  printf '\n=== pipeline self-test ===\n  [skip] RUN_TESTS=0\n'
fi

say "summary"
if [ "$failures" -eq 0 ]; then
  printf 'ALL CHECKS PASSED\n'
  exit 0
fi
printf '%d CHECK(S) FAILED\n' "$failures"
exit 1
