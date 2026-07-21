#include "bda_types.h"

u32 probe_c_mix(u32 value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}
