#ifndef BBK9588_CTYPE_H
#define BBK9588_CTYPE_H

static inline int isdigit(int value)
{
    return value >= '0' && value <= '9';
}

static inline int isspace(int value)
{
    return value == ' ' || value == '\t' || value == '\n' ||
        value == '\r' || value == '\f' || value == '\v';
}

static inline int tolower(int value)
{
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static inline int toupper(int value)
{
    return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

#endif
