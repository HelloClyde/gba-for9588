#include "platform/bbk9588/audio_output.h"

#include "bda_audio.h"

#include <string.h>

#define GPSP_AUDIO_RATE 65536u

static s16 average_stereo(int16_t left, int16_t right)
{
    return (s16)(((s32)left + (s32)right) / 2);
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
    bda_audio_set_attenuation((u32)output->original_attenuation);
    output->opened = 1;
}

void bbk_audio_output_set_muted(bbk_audio_output_t *output, int muted)
{
    output->muted = muted != 0;
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
        output->mix_sum += average_stereo(samples[frame * 2u], samples[frame * 2u + 1u]);
        ++output->mix_count;
        output->phase += BBK_AUDIO_OUTPUT_RATE;
        if (output->phase >= GPSP_AUDIO_RATE) {
            s32 mixed = output->mix_sum / (s32)output->mix_count;
            output->phase -= GPSP_AUDIO_RATE;
            if (mixed > 32767) {
                mixed = 32767;
            } else if (mixed < -32768) {
                mixed = -32768;
            }
            queue_sample(output, (s16)mixed);
            output->mix_sum = 0;
            output->mix_count = 0u;
        }
    }
    return frames;
}

int bbk_audio_output_service(bbk_audio_output_t *output)
{
    u32 index;
    int written;
    if (!output->opened ||
        output->queued_samples < BBK_AUDIO_BLOCK_SAMPLES ||
        !bda_audio_ready()) {
        return 0;
    }
    for (index = 0; index < BBK_AUDIO_BLOCK_SAMPLES; ++index) {
        output->block[index] = output->muted ? 0 : output->ring[
            (output->read_index + index) & (BBK_AUDIO_RING_SAMPLES - 1u)
        ];
    }
    written = bda_audio_write(output->block, sizeof(output->block));
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
