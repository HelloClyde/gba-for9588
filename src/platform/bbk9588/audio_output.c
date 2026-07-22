#include "platform/bbk9588/audio_output.h"

#include "bda_audio.h"

#include <string.h>

#define GPSP_AUDIO_RATE 65536u
#define MAX_ATTENUATION BDA_AUDIO_ATTENUATION_NEAR_SILENT
#define ATTENUATION_STEP_COUNT \
    (MAX_ATTENUATION / BDA_AUDIO_ATTENUATION_STEP)

_Static_assert(
    (BBK_AUDIO_RING_SAMPLES & (BBK_AUDIO_RING_SAMPLES - 1u)) == 0u,
    "audio ring size must be a power of two"
);
_Static_assert(
    BBK_AUDIO_BLOCK_SAMPLES <= BBK_AUDIO_RING_SAMPLES,
    "audio block must fit in the ring"
);

static s16 average_resample_window(s32 sum, u32 sample_count)
{
    u32 magnitude = sum < 0 ? (u32)(-sum) : (u32)sum;
    u32 quotient;

    if (sample_count == 4u) {
        quotient = magnitude >> 2;
    } else {
        /* 10923/65536 is ceil(1/6); one correction makes it exact. */
        quotient = (magnitude * 10923u) >> 16;
        if (quotient * 6u > magnitude) {
            --quotient;
        }
    }
    return sum < 0 ? (s16)(-(s32)quotient) : (s16)quotient;
}

static u32 attenuation_for_level(const bbk_audio_output_t *output, u32 level)
{
    u32 original = output->original_attenuation < 0 ? 0u :
        (u32)output->original_attenuation;
    u32 original_steps;
    u32 remaining_steps;
    u32 scaled_remaining_steps;

    if (original > MAX_ATTENUATION) {
        original = MAX_ATTENUATION;
    }
    original_steps = original / BDA_AUDIO_ATTENUATION_STEP;
    remaining_steps = ATTENUATION_STEP_COUNT - original_steps;
    scaled_remaining_steps = (remaining_steps * level + 2u) >> 2;
    return (ATTENUATION_STEP_COUNT - scaled_remaining_steps) *
        BDA_AUDIO_ATTENUATION_STEP;
}

static void queue_sample(bbk_audio_output_t *output, s16 sample)
{
    if (output->queued_samples == BBK_AUDIO_RING_SAMPLES) {
        output->read_index =
            (output->read_index + 1u) & (BBK_AUDIO_RING_SAMPLES - 1u);
        --output->queued_samples;
        ++output->dropped_samples;
    }
    output->ring[output->write_index] = sample;
    output->write_index =
        (output->write_index + 1u) & (BBK_AUDIO_RING_SAMPLES - 1u);
    ++output->queued_samples;
}

void bbk_audio_output_init(bbk_audio_output_t *output)
{
    memset(output, 0, sizeof(*output));
    output->volume_level = BBK_AUDIO_VOLUME_MAX;
}

void bbk_audio_output_start(bbk_audio_output_t *output)
{
    if (output->opened) {
        return;
    }
    output->original_attenuation = bda_audio_get_attenuation();
    bda_audio_open_pcm(
        BDA_AUDIO_SAMPLE_RATE_22050,
        BDA_AUDIO_BITS_16,
        BDA_AUDIO_CHANNELS_MONO
    );
    output->opened = 1;
    bbk_audio_output_set_level(output, output->volume_level);
}

void bbk_audio_output_set_level(bbk_audio_output_t *output, u32 volume_level)
{
    if (volume_level > BBK_AUDIO_VOLUME_MAX) {
        volume_level = BBK_AUDIO_VOLUME_MAX;
    }
    output->volume_level = volume_level;
    output->muted = volume_level == 0u;
    if (output->muted) {
        memset(output->block, 0, sizeof(output->block));
    }
    if (output->opened) {
        output->effective_attenuation = attenuation_for_level(
            output, volume_level
        );
        bda_audio_set_attenuation(output->effective_attenuation);
    }
}

size_t bbk_audio_output_push_stereo(
    bbk_audio_output_t *output, const int16_t *samples, size_t frames
)
{
    size_t frame;
    if (!samples) {
        return 0u;
    }
    for (frame = 0; frame < frames; ++frame) {
        output->mix_sum += (s32)samples[frame * 2u] +
            (s32)samples[frame * 2u + 1u];
        output->mix_count += 2u;
        output->phase += BBK_AUDIO_OUTPUT_RATE;
        if (output->phase >= GPSP_AUDIO_RATE) {
            s16 mixed = average_resample_window(
                output->mix_sum, output->mix_count
            );
            output->phase -= GPSP_AUDIO_RATE;
            queue_sample(output, mixed);
            output->mix_sum = 0;
            output->mix_count = 0u;
        }
    }
    return frames;
}

int bbk_audio_output_service(bbk_audio_output_t *output)
{
    const s16 *block;
    u32 first_samples;
    int written;
    if (!output->opened ||
        output->queued_samples < BBK_AUDIO_BLOCK_SAMPLES ||
        !bda_audio_ready()) {
        return 0;
    }
    if (output->muted) {
        block = output->block;
    } else if (output->read_index + BBK_AUDIO_BLOCK_SAMPLES <=
        BBK_AUDIO_RING_SAMPLES) {
        block = &output->ring[output->read_index];
    } else {
        first_samples = BBK_AUDIO_RING_SAMPLES - output->read_index;
        memcpy(
            output->block, &output->ring[output->read_index],
            first_samples * sizeof(*output->block)
        );
        memcpy(
            output->block + first_samples, output->ring,
            (BBK_AUDIO_BLOCK_SAMPLES - first_samples) * sizeof(*output->block)
        );
        block = output->block;
    }
    written = bda_audio_write(block, sizeof(output->block));
    if (written != (int)sizeof(output->block)) {
        ++output->short_writes;
        return -1;
    }
    output->read_index =
        (output->read_index + BBK_AUDIO_BLOCK_SAMPLES) &
        (BBK_AUDIO_RING_SAMPLES - 1u);
    output->queued_samples -= BBK_AUDIO_BLOCK_SAMPLES;
    ++output->blocks_written;
    return 1;
}

int bbk_audio_output_needs_backpressure(const bbk_audio_output_t *output)
{
    return output->queued_samples >=
        BBK_AUDIO_RING_SAMPLES - BBK_AUDIO_BLOCK_SAMPLES * 2u;
}

void bbk_audio_output_stop(bbk_audio_output_t *output)
{
    if (!output->opened) {
        return;
    }
    memset(output->block, 0, sizeof(output->block));
    bda_audio_set_attenuation((u32)output->original_attenuation);
    if (bda_audio_ready()) {
        (void)bda_audio_write(output->block, sizeof(output->block));
    }
    bda_audio_stop();
    output->opened = 0;
    output->queued_samples = 0u;
    output->read_index = 0u;
    output->write_index = 0u;
    output->phase = 0u;
    output->mix_sum = 0;
    output->mix_count = 0u;
}
