#include "bda_types.h"

static u32 g_constructor_value;
extern "C" u32 probe_asm_magic(void);

class ConstructorProbe {
public:
    ConstructorProbe() : value_(probe_asm_magic())
    {
        g_constructor_value = value_;
    }

    u32 add(u32 value) const
    {
        return value_ + value;
    }

private:
    u32 value_;
};

static ConstructorProbe g_constructor_probe;

extern "C" u32 probe_cpp_constructor_value(void)
{
    return g_constructor_value;
}

extern "C" u32 probe_cpp_add(u32 value)
{
    return g_constructor_probe.add(value);
}
