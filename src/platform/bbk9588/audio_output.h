#ifndef BBK9588_AUDIO_OUTPUT_H
#define BBK9588_AUDIO_OUTPUT_H

#include "bda_types.h"

#include <stddef.h>
#include <stdint.h>

#define BBK_AUDIO_OUTPUT_RATE 22050u
#define BBK_AUDIO_BLOCK_SAMPLES 512u
#define BBK_AUDIO_RING_SAMPLES 4096u

typedef struct bbk_audio_output {
    s16 ring[BBK_AUDIO_RING_SAMPLES];
    s16 block[BBK_AUDIO_BLOCK_SAMPLES];
    u32 read_index;
    u32 write_index;
    u32 queued_samples;
    u32 phase;
    s32 mix_sum;
    u32 mix_count;
    u32 blocks_written;
    u32 dropped_samples;
    u32 short_writes;
    int original_attenuation;
    int muted;
    int opened;
} bbk_audio_output_t;

void bbk_audio_output_init(bbk_audio_output_t *output);
void bbk_audio_output_start(bbk_audio_output_t *output);
void bbk_audio_output_set_muted(bbk_audio_output_t *output, int muted);
size_t bbk_audio_output_push_stereo(
    bbk_audio_output_t *output, const int16_t *samples, size_t frames
);
int bbk_audio_output_service(bbk_audio_output_t *output);
int bbk_audio_output_needs_backpressure(const bbk_audio_output_t *output);
void bbk_audio_output_stop(bbk_audio_output_t *output);

#endif
