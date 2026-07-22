#ifndef BBK9588_PERF_COUNTER_H
#define BBK9588_PERF_COUNTER_H

#include "bda_types.h"

#include <stdint.h>

typedef struct bbk_perf_accumulator {
    uint64_t ticks;
    u32 calls;
    u32 max_ticks;
} bbk_perf_accumulator_t;

static inline u32 bbk_perf_counter_read(void)
{
    u32 value;
    __asm__ volatile("mfc0 %0, $9" : "=r"(value));
    return value;
}

static inline u32 bbk_perf_counter_elapsed(u32 start, u32 end)
{
    return end - start;
}

static inline void bbk_perf_accumulator_add(
    bbk_perf_accumulator_t *accumulator, u32 start, u32 end
)
{
    u32 elapsed = bbk_perf_counter_elapsed(start, end);
    accumulator->ticks += elapsed;
    ++accumulator->calls;
    if (elapsed > accumulator->max_ticks) {
        accumulator->max_ticks = elapsed;
    }
}

static inline void bbk_perf_accumulator_reset(
    bbk_perf_accumulator_t *accumulator
)
{
    accumulator->ticks = 0u;
    accumulator->calls = 0u;
    accumulator->max_ticks = 0u;
}

#endif
