#include "bda_memory.h"
#include "bda_time.h"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ALLOCATION_MAGIC 0x9588a110u

typedef union allocation_header {
    struct {
        uint32_t magic;
        uint32_t size;
    } fields;
    uint64_t alignment;
} allocation_header_t;

int errno;

struct bbk9588_file {
    int unused;
};

static FILE g_stdout;
FILE *stdout = &g_stdout;

void *malloc(size_t size)
{
    allocation_header_t *header;
    if (size == 0u) {
        size = 1u;
    }
    if (size > 0xffffffffu - sizeof(*header)) {
        return 0;
    }
    header = (allocation_header_t *)bda_alloc((bda_size_t)(size + sizeof(*header)));
    if (!header || (uint32_t)header == 0xffffffffu) {
        return 0;
    }
    header->fields.magic = ALLOCATION_MAGIC;
    header->fields.size = (uint32_t)size;
    return header + 1;
}

void free(void *pointer)
{
    allocation_header_t *header;
    if (!pointer) {
        return;
    }
    header = (allocation_header_t *)pointer - 1;
    if (header->fields.magic != ALLOCATION_MAGIC) {
        return;
    }
    header->fields.magic = 0u;
    bda_free(header);
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *pointer;
    if (size != 0u && count > (size_t)-1 / size) {
        return 0;
    }
    total = count * size;
    pointer = malloc(total);
    if (pointer) {
        memset(pointer, 0, total);
    }
    return pointer;
}

void *realloc(void *pointer, size_t size)
{
    allocation_header_t *header;
    void *replacement;
    size_t copy_size;
    if (!pointer) {
        return malloc(size);
    }
    if (size == 0u) {
        free(pointer);
        return 0;
    }
    header = (allocation_header_t *)pointer - 1;
    if (header->fields.magic != ALLOCATION_MAGIC) {
        return 0;
    }
    replacement = malloc(size);
    if (!replacement) {
        return 0;
    }
    copy_size = header->fields.size < size ? header->fields.size : size;
    memcpy(replacement, pointer, copy_size);
    free(pointer);
    return replacement;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if ((((uintptr_t)out | (uintptr_t)in) & 3u) == 0u) {
        while (count >= 16u) {
            ((uint32_t *)out)[0] = ((const uint32_t *)in)[0];
            ((uint32_t *)out)[1] = ((const uint32_t *)in)[1];
            ((uint32_t *)out)[2] = ((const uint32_t *)in)[2];
            ((uint32_t *)out)[3] = ((const uint32_t *)in)[3];
            out += 16;
            in += 16;
            count -= 16u;
        }
        while (count >= 4u) {
            *(uint32_t *)out = *(const uint32_t *)in;
            out += 4;
            in += 4;
            count -= 4u;
        }
    }
    while (count-- != 0u) {
        *out++ = *in++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    if (out <= in || out >= in + count) {
        return memcpy(destination, source, count);
    }
    out += count;
    in += count;
    while (count-- != 0u) {
        *--out = *--in;
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    uint8_t *out = (uint8_t *)destination;
    uint8_t byte = (uint8_t)value;
    uint32_t word = (uint32_t)byte * 0x01010101u;
    while (count != 0u && ((uintptr_t)out & 3u) != 0u) {
        *out++ = byte;
        --count;
    }
    while (count >= 16u) {
        ((uint32_t *)out)[0] = word;
        ((uint32_t *)out)[1] = word;
        ((uint32_t *)out)[2] = word;
        ((uint32_t *)out)[3] = word;
        out += 16;
        count -= 16u;
    }
    while (count >= 4u) {
        *(uint32_t *)out = word;
        out += 4;
        count -= 4u;
    }
    while (count-- != 0u) {
        *out++ = byte;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    while (count-- != 0u) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        ++a;
        ++b;
    }
    return 0;
}

size_t strlen(const char *text)
{
    const char *end = text;
    while (*end) {
        ++end;
    }
    return (size_t)(end - text);
}

int strcmp(const char *left, const char *right)
{
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    while (count != 0u && *left && *left == *right) {
        ++left;
        ++right;
        --count;
    }
    return count == 0u ? 0 : (int)(uint8_t)*left - (int)(uint8_t)*right;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

char *strncpy(char *destination, const char *source, size_t count)
{
    char *result = destination;
    while (count != 0u && *source) {
        *destination++ = *source++;
        --count;
    }
    while (count-- != 0u) {
        *destination++ = 0;
    }
    return result;
}

char *strcat(char *destination, const char *source)
{
    strcpy(destination + strlen(destination), source);
    return destination;
}

char *strchr(const char *text, int character)
{
    char target = (char)character;
    do {
        if (*text == target) {
            return (char *)text;
        }
    } while (*text++);
    return 0;
}

char *strrchr(const char *text, int character)
{
    const char *match = 0;
    char target = (char)character;
    do {
        if (*text == target) {
            match = text;
        }
    } while (*text++);
    return (char *)match;
}

char *strstr(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    if (length == 0u) {
        return (char *)text;
    }
    while (*text) {
        if (*text == *needle && memcmp(text, needle, length) == 0) {
            return (char *)text;
        }
        ++text;
    }
    return 0;
}

long strtol(const char *text, char **end, int base)
{
    long value = 0;
    int negative = 0;
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '-' || *text == '+') {
        negative = *text++ == '-';
    }
    if (base == 0) {
        base = 10;
    }
    while (*text >= '0' && *text <= '9' && (*text - '0') < base) {
        value = value * base + (*text++ - '0');
    }
    if (end) {
        *end = (char *)text;
    }
    return negative ? -value : value;
}

int atoi(const char *text)
{
    return (int)strtol(text, 0, 10);
}

static void format_character(char *output, size_t size, size_t *offset, char value)
{
    if (*offset + 1u < size) {
        output[*offset] = value;
    }
    ++*offset;
}

static void format_unsigned(
    char *output, size_t size, size_t *offset, uint64_t value,
    unsigned base, int width, char pad, int uppercase
)
{
    char digits[24];
    const char *alphabet = uppercase ?
        "0123456789ABCDEF" : "0123456789abcdef";
    int count = 0;
    do {
        digits[count++] = alphabet[value % base];
        value /= base;
    } while (value != 0u && count < (int)sizeof(digits));
    while (width-- > count) {
        format_character(output, size, offset, pad);
    }
    while (count != 0) {
        format_character(output, size, offset, digits[--count]);
    }
}

int vsnprintf(char *output, size_t size, const char *format, va_list args)
{
    size_t offset = 0;
    while (*format) {
        int width = 0;
        int long_count = 0;
        char pad = ' ';
        char specifier;
        if (*format != '%') {
            format_character(output, size, &offset, *format++);
            continue;
        }
        ++format;
        if (*format == '0') {
            pad = '0';
            ++format;
        }
        while (isdigit((unsigned char)*format)) {
            width = width * 10 + (*format++ - '0');
        }
        while (*format == 'l') {
            ++long_count;
            ++format;
        }
        if (*format == 'z') {
            long_count = 1;
            ++format;
        }
        specifier = *format ? *format++ : 0;
        if (specifier == 's') {
            const char *text = va_arg(args, const char *);
            if (!text) {
                text = "(null)";
            }
            while (*text) {
                format_character(output, size, &offset, *text++);
            }
        } else if (specifier == 'c') {
            format_character(output, size, &offset, (char)va_arg(args, int));
        } else if (specifier == 'd' || specifier == 'i') {
            long long value = long_count >= 2 ? va_arg(args, long long) :
                (long_count == 1 ? va_arg(args, long) : va_arg(args, int));
            uint64_t magnitude;
            if (value < 0) {
                format_character(output, size, &offset, '-');
                magnitude = (uint64_t)(-(value + 1)) + 1u;
            } else {
                magnitude = (uint64_t)value;
            }
            format_unsigned(output, size, &offset, magnitude, 10u, width, pad, 0);
        } else if (specifier == 'u' || specifier == 'x' || specifier == 'X') {
            uint64_t value = long_count >= 2 ? va_arg(args, unsigned long long) :
                (long_count == 1 ? va_arg(args, unsigned long) : va_arg(args, unsigned int));
            format_unsigned(output, size, &offset, value,
                specifier == 'u' ? 10u : 16u, width, pad, specifier == 'X');
        } else if (specifier == 'p') {
            format_character(output, size, &offset, '0');
            format_character(output, size, &offset, 'x');
            format_unsigned(output, size, &offset,
                (uintptr_t)va_arg(args, void *), 16u, 8, '0', 0);
        } else if (specifier == '%') {
            format_character(output, size, &offset, '%');
        }
    }
    if (size != 0u) {
        output[offset < size ? offset : size - 1u] = 0;
    }
    return (int)offset;
}

int snprintf(char *output, size_t size, const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = vsnprintf(output, size, format, args);
    va_end(args);
    return result;
}

int printf(const char *format, ...)
{
    char ignored[192];
    int result;
    va_list args;
    va_start(args, format);
    result = vsnprintf(ignored, sizeof(ignored), format, args);
    va_end(args);
    return result;
}

int sscanf(const char *input, const char *format, ...)
{
    (void)input;
    (void)format;
    return 0;
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}

void abort(void)
{
    for (;;) {
        bda_sys_delay(1u);
    }
}

time_t time(time_t *result)
{
    time_t value = 946684800ll + (time_t)(bda_gui_tick_count_25ms() / 40u);
    if (result) {
        *result = value;
    }
    return value;
}

static int leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

struct tm *localtime(const time_t *value)
{
    static struct tm result;
    static const int month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    time_t days = *value / 86400ll;
    time_t seconds = *value % 86400ll;
    int year = 1970;
    int month = 0;
    int day_of_year;

    if (seconds < 0) {
        seconds += 86400ll;
        --days;
    }
    result.tm_hour = (int)(seconds / 3600ll);
    result.tm_min = (int)((seconds / 60ll) % 60ll);
    result.tm_sec = (int)(seconds % 60ll);
    result.tm_wday = (int)((days + 4ll) % 7ll);
    if (result.tm_wday < 0) {
        result.tm_wday += 7;
    }
    while (days >= (leap_year(year) ? 366 : 365)) {
        days -= leap_year(year) ? 366 : 365;
        ++year;
    }
    day_of_year = (int)days;
    while (month < 11) {
        int length = month_days[month] + (month == 1 && leap_year(year));
        if (days < length) {
            break;
        }
        days -= length;
        ++month;
    }
    result.tm_year = year - 1900;
    result.tm_mon = month;
    result.tm_mday = (int)days + 1;
    result.tm_yday = day_of_year;
    result.tm_isdst = 0;
    return &result;
}
