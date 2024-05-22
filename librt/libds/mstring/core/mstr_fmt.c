/**
 * Copyright 2024, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../common/private.h"
#include "../unicode/private.h"
#include <sys/types.h>
#include <stdarg.h>

enum fmt_flags {
    FMT_MSTRING  = 0x1,
    FMT_LEFTALIGN = 0x2,
    FMT_PREFIX = 0x4
};

enum fmt_size {
    FMT_DEFAULT,
    FMT_BYTE,
    FMT_SHORT,
    FMT_LONG,
    FMT_LONGLONG,
    FMT_INTMAX,
    FMT_SIZE,
    FMT_PTRDIFF,
    FMT_LONGDEC
};

struct fmt_context {
    enum fmt_flags flags;
    enum fmt_size  size;
    int            precision;
    int            width;
    mchar_t        prefix;
    mchar_t        padding;

    // Used for consumption
    const char* fmt;
    int*        fmtip;
};

#define __FMT_CHECK(__f) if (__f) { mstring_builder_destroy(builder); va_end(args); return NULL; }
static mstring_t g_nullMessage = mstr_const("<null>");
static char*     g_hexValuesLC = "0123456789abcdef";
static char*     g_hexValuesUC = "0123456789ABCDEF";

static int __append_mstring(struct fmt_context* context, struct mstring_builder* builder, mstring_t* string)
{
    if (string == NULL) {
        string = &g_nullMessage;
    }

    for (size_t i = 0; i < string->__length; i++) {
        if (mstring_builder_append(builder, string->__data[i])) {
            return -1;
        }
    }
    return 0;
}

static int __append_u8(struct fmt_context* context, struct mstring_builder* builder, const char* string)
{
    if (string == NULL) {
        return __append_mstring(context, builder, NULL);
    }
    size_t u8len = mstr_len_u8(string);
    return mstring_builder_append_u8(builder, string, u8len);
}

static int __append_hex(struct fmt_context* context, struct mstring_builder* builder, unsigned long long value, int uppercase)
{
    char tmp[64];
    int  i = 0;
    unsigned long long  _val = value;
    const char* hexValues = uppercase > 0 ? g_hexValuesUC : g_hexValuesLC;

    // 0x0000001
    //      +001

    // do this immediately so we get the length
    while (_val) {
        tmp[sizeof(tmp) - (i++)] = hexValues[_val % 16];
        _val >>= 4;
    }

    // print left padding
    if (context->width && !(context->flags & FMT_LEFTALIGN)) {
        for (int j = 0; j < (context->width - i); j++) {
            if (mstring_builder_append(builder, context->padding)) {
                return -1;
            }
        }
    }

    // no characters stored, print 0
    if (i == 0) {
        return mstring_builder_append(builder, U'0');
    }

    // print prefix
    if (context->flags & FMT_PREFIX) {
        if (mstring_builder_append(builder, U'0')) {
            return -1;
        }
        if (mstring_builder_append(builder, uppercase ? U'X' : U'x')) {
            return -1;
        }
    }

    // print value
    for (int j = i-1; j >= 0; j--) {
        if (mstring_builder_append(builder, tmp[sizeof(tmp) - j])) {
            return -1;
        }
    }

    // print right padding
    if (context->width && (context->flags & FMT_LEFTALIGN)) {
        for (int j = 0; j < (context->width - i); j++) {
            if (mstring_builder_append(builder, context->padding)) {
                return -1;
            }
        }
    }

    return 0;
}

static int __append_uint(struct fmt_context* context, struct mstring_builder* builder, unsigned long long value)
{
    char tmp[64];
    int  i = 0;
    unsigned long long  _val = value;

    // do this immediately so we get the length
    while (_val) {
        tmp[sizeof(tmp) - (i++)] = g_hexValuesLC[_val % 10];
        _val /= 10;
    }

    // print prefix
    if (context->prefix != 0) {
        if (mstring_builder_append(builder, context->prefix)) {
            return -1;
        }
    }

    // print left padding
    if (context->width && !(context->flags & FMT_LEFTALIGN)) {
        for (int j = 0; j < (context->width - i); j++) {
            if (mstring_builder_append(builder, context->padding)) {
                return -1;
            }
        }
    }

    // no characters stored, print 0
    if (_val == 0) {
        return mstring_builder_append(builder, U'0');
    }

    for (i--; i >= 0; i--) {
        if (mstring_builder_append(builder, tmp[sizeof(tmp) - i])) {
            return -1;
        }
    }
    return 0;
}

static int __append_int(struct fmt_context* context, struct mstring_builder* builder, long long value)
{
    long long _val = value;

    // Handle negative values
    if (_val < 0) {
        context->prefix = U'-';
        if (mstring_builder_append(builder, U'-')) {
            return -1;
        }
        _val = -_val;
    }
    return __append_uint(context, builder, (unsigned long long)_val);
}

static int __append_octal(struct fmt_context* context, struct mstring_builder* builder, unsigned long long value)
{
    char tmp[64];
    int  i = 0;
    unsigned long long  _val = value;

    if (_val == 0) {
        return mstring_builder_append(builder, U'0');
    }

    if (context->flags & FMT_PREFIX) {
        if (mstring_builder_append(builder, U'0')) {
            return -1;
        }
    }

    while (_val) {
        tmp[sizeof(tmp) - (i++)] = g_hexValuesLC[_val % 8];
        _val >>= 3;
    }

    for (i--; i >= 0; i--) {
        if (mstring_builder_append(builder, tmp[sizeof(tmp) - i])) {
            return -1;
        }
    }
    return 0;
}

static long long __signed_argument(va_list* args, struct fmt_context* context)
{
    switch (context->size) {
        case FMT_DEFAULT: return va_arg(*args, int);
        case FMT_BYTE: return (char)va_arg(*args, int);
        case FMT_SHORT: return (short)va_arg(*args, int);
        case FMT_LONG: return va_arg(*args, long);
        case FMT_LONGLONG: return va_arg(*args, long long);
        case FMT_INTMAX: return va_arg(*args, intmax_t);
        case FMT_SIZE: return va_arg(*args, ssize_t);
        case FMT_PTRDIFF: return va_arg(*args, ptrdiff_t);
        default: break;
    }
    return 0;
}

static unsigned long long __unsigned_argument(va_list* args, struct fmt_context* context)
{
    switch (context->size) {
        case FMT_DEFAULT: return va_arg(*args, unsigned int);
        case FMT_BYTE: return (unsigned char)va_arg(*args, unsigned int);
        case FMT_SHORT: return (unsigned short)va_arg(*args, unsigned int);
        case FMT_LONG: return va_arg(*args, unsigned long);
        case FMT_LONGLONG: return va_arg(*args, unsigned long long);
        case FMT_INTMAX: return va_arg(*args, uintmax_t);
        case FMT_SIZE: return va_arg(*args, size_t);
        case FMT_PTRDIFF: return va_arg(*args, ptrdiff_t);
        default: break;
    }
    return 0;
}

static inline mchar_t __context_next(struct fmt_context* context) {
    return mstr_next(context->fmt, context->fmtip);
}

static inline mchar_t __parse_flags(struct fmt_context* context, mchar_t current) {
    mchar_t val = current;
    switch (val) {
        // Left-justify within the given field width; Right justification is the default (see width sub-specifier).
        case U'-': {
            context->flags |= FMT_LEFTALIGN;
            val = __context_next(context);
        } break;

        // Forces to preceed the result with a plus or minus sign (+ or -) even for positive numbers.
        // By default, only negative numbers are preceded with a - sign.
        case U'+': {
            context->prefix = U'+';
            val = __context_next(context);
        } break;

        // If no sign is going to be written, a blank space is inserted before the value.
        case U' ': {
            context->prefix = U' ';
            val = __context_next(context);
        } break;

        // Used with o, x or X specifiers the value is preceeded with 0, 0x or 0X respectively for values different from zero.
        // Used with a, A, e, E, f, F, g or G it forces the written output to contain a decimal point even if no
        // more digits follow. By default, if no digits follow, no decimal point is written.
        case U'#': {
            context->flags |= FMT_PREFIX;
            val = __context_next(context);
        } break;

        // Left-pads the number with zeroes (0) instead of spaces when padding is specified (see width sub-specifier).
        case U'0': {
            context->padding = U'0';
            val = __context_next(context);
        } break;

        default:
            break;
    }
    return val;
}

static inline uint32_t __parse_uint(struct fmt_context* context, mchar_t* current) {
    mchar_t  val     = *current;
    uint32_t number  = 0;
    uint32_t nums[3] = { 0 };
    int      count   = 0;

    // support up to 3 digits
    while (count < 3) {
        const __unicode_digit_t* num = __lookup_number(val);
        if (num == NULL) {
            // Short-circuit this function for speed, if this was the first character
            // and not a digit, just skip and return 0.
            if (val == *current) {
                return 0;
            }
            break;
        }

        // get next number
        val = __context_next(context);

        if (num->denominator != num->numerator) {
            // fractional value, no, thanks
            continue;
        }
        nums[count++] = (uint32_t)num->numerator;
    }

    // skip all remaining
    while (mstr_isdigit(val)) {
        val = __context_next(context);
    }

    for (int i = 0; i < count; i++) {
        // largest number first
        number += nums[i] * (count - i);
    }

    *current = val;
    return number;
}

// Minimum number of characters to be printed. If the value to be printed is shorter than this number,
// the result is padded with blank spaces. The value is not truncated even if the result is larger.
static inline mchar_t __parse_width(va_list* args, struct fmt_context* context, mchar_t current) {
    mchar_t val = current;
    if (val == U'*') {
        // The width is not specified in the format string, but as an additional integer
        // value argument preceding the argument that has to be formatted.
        context->width = va_arg(*args, int);
        val = __context_next(context);
    } else {
        context->width = (int)__parse_uint(context, &val);
    }
    return val;
}

// For integer specifiers (d, i, o, u, x, X): precision specifies the minimum number of digits to be written.
// If the value to be written is shorter than this number, the result is padded with leading zeros.
// The value is not truncated even if the result is longer. A precision of 0 means that no character is written for the value 0.
// For a, A, e, E, f and F specifiers: this is the number of digits to be printed after the decimal point (by default, this is 6).
// For g and G specifiers: This is the maximum number of significant digits to be printed.
// For s: this is the maximum number of characters to be printed. By default, all characters are printed until the ending null character is encountered.
// If the period is specified without an explicit value for precision, 0 is assumed.
static inline mchar_t __parse_precision(va_list* args, struct fmt_context* context, mchar_t current) {
    mchar_t val = current;
    if (val != U'.') {
        return val;
    }

    // get the next character that determines the type of precision
    val = __context_next(context);
    if (val == U'*') {
        // The precision is not specified in the format string, but as an additional integer
        // value argument preceding the argument that has to be formatted.
        context->precision = va_arg(*args, int);
        val = __context_next(context);
    } else {
        context->precision = (int)__parse_uint(context, &val);
    }
    return val;
}

static inline mchar_t __parse_length(struct fmt_context* context, mchar_t current) {
    mchar_t val = current;
    for (;;) {
        switch (val) {
            case U'm':
            case U'M': {
                context->flags |= FMT_MSTRING;
                val = __context_next(context);
            } break;

            case U'h': {
                if (context->size == FMT_SHORT) {
                    context->size = FMT_BYTE;
                } else {
                    context->size = FMT_SHORT;
                }
                val = __context_next(context);
            } break;

            case U'l': {
                if (context->size == FMT_LONG) {
                    context->size = FMT_LONGLONG;
                } else {
                    context->size = FMT_LONG;
                }
                val = __context_next(context);
            } break;

            case U'j': {
                context->size = FMT_INTMAX;
                val = __context_next(context);
            } break;
            case U'z': {
                context->size = FMT_SIZE;
                val = __context_next(context);
            } break;
            case U't': {
                context->size = FMT_PTRDIFF;
                val = __context_next(context);
            } break;
            case U'L': {
                context->size = FMT_LONGDEC;
                val = __context_next(context);
            } break;

            default:
                return val;
        }
    }
    // unreachable
    return val;
}

// Supports only a subset of the regular printf formats.
// %[flags][width][.precision][length]specifier
// https://cplusplus.com/reference/cstdio/printf/
mstring_t* mstr_fmt(const char* fmt, ...)
{
    struct mstring_builder* builder;
    int                     fmti = 0;
    va_list                 args;

    builder = mstring_builder_new(128);
    if (builder == NULL) {
        return NULL;
    }

    va_start(args, fmt);
    while (fmt[fmti]) {
        struct fmt_context context = { 0, 0, 0, 0, 0, U' ', fmt, &fmti };
        mchar_t            val     = __context_next(&context);
        if (val == U'%') {
            int consumed = 0;
            val = __context_next(&context);
            val = __parse_flags(&context, val);
            val = __parse_width(&args, &context, val);
            val = __parse_precision(&args, &context, val);
            val = __parse_length(&context, val);

            // now handle the actual specifiers
            switch (val) {
                case U'%':
                    break;
                case U's': {
                    if (context.flags & FMT_MSTRING) {
                        mstring_t* string = va_arg(args, mstring_t*);
                        __FMT_CHECK(__append_mstring(&context, builder, string))
                    } else if (context.size == FMT_LONG) {
                        // TODO: wchar_t
                    } else {
                        const char* string = va_arg(args, const char*);
                        __FMT_CHECK(__append_u8(&context, builder, string))
                    }
                    consumed = 1;
                } break;

                // Signed decimal integer
                case U'd':
                case U'i': {
                    long long value = __signed_argument(&args, &context);
                    __FMT_CHECK(__append_int(&context, builder, value));
                    consumed = 1;
                } break;

                // Unsigned decimal integer
                case U'u': {
                    unsigned long long value = __unsigned_argument(&args, &context);
                    __FMT_CHECK(__append_uint(&context, builder, value));
                    consumed = 1;
                } break;

                // Unsigned octal integer
                case U'o': {
                    unsigned long long value = __unsigned_argument(&args, &context);
                    __FMT_CHECK(__append_octal(&context, builder, value));
                    consumed = 1;
                } break;

                // Unsigned hexadecimal integer
                case U'x':
                case U'X': {
                    unsigned long long value = __unsigned_argument(&args, &context);
                    __FMT_CHECK(__append_hex(&context, builder, value, val == U'X'));
                    consumed = 1;
                } break;

                // Pointer address
                case U'p':
                case U'P': {
                    void* value = va_arg(args, void*);
                    __FMT_CHECK(__append_hex(&context, builder, (unsigned long long)value, val == U'P'));
                    consumed = 1;
                } break;

                // TODO: missing identifiers
                // c
                // n
                // f/F/e/E/g/G/a/A

                default:
                    break;
            }
            if (consumed) {
                // Skip to next character
                continue;
            }
        }
        __FMT_CHECK(mstring_builder_append(builder, val))
    }
    va_end(args);

    return mstring_builder_finish(builder);
}
