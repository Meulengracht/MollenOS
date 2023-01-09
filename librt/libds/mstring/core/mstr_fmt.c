/**
 * Copyright 2022, Philip Meulengracht
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
#include <stdarg.h>

enum fmt_flags {
    FMT_MSTRING = 0x1
};

struct fmt_context {
    enum fmt_flags flags;
};

#define __FMT_CHECK(__f) if (__f) { mstring_builder_destroy(builder); va_end(args); return NULL; }
static mstring_t g_nullMessage = mstr_const("<null>");

static int __append_mstring(struct mstring_builder* builder, mstring_t* string)
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

static int __append_u8(struct mstring_builder* builder, const char* string)
{
    if (string == NULL) {
        return __append_mstring(builder, NULL);
    }
    size_t u8len = mstr_len_u8(string);
    return mstring_builder_append_u8(builder, string, u8len);
}

static int __append_int(struct mstring_builder* builder, int value)
{
    char tmp[32];
    int  i = 0;
    int  _val = value;

    // Handle two special cases, 0 and - values
    if (_val == 0) {
        return mstring_builder_append(builder, U'0');
    } else if (_val < 0) {
        if (mstring_builder_append(builder, U'-')) return -1;
        _val = -_val;
    }

    while (_val) {
        tmp[sizeof(tmp) - (i++)] = (char)'0' + (char)(_val % 10);
        _val /= 10;
    }

    for (i--; i >= 0; i--) {
        if (mstring_builder_append(builder, tmp[sizeof(tmp) - i])) return -1;
    }
    return 0;
}

// Supports %s and %ms
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
        struct fmt_context context = { 0 };
        mchar_t            val     = mstr_next(fmt, &fmti);
        switch (val) {
            case '%': {
                int consumed = 0;

                // consume '%', and consume flags
                while (1) {
                    val = mstr_next(fmt, &fmti);
                    if (val == 'm' || val == 'M') {
                        context.flags |= FMT_MSTRING;
                    } else {
                        // not a recognized flag, break
                        break;
                    }
                }

                // now handle the actual specifiers
                switch (val) {
                    case '%':
                        break;
                    case 's': {
                        if (context.flags & FMT_MSTRING) {
                            mstring_t* string = va_arg(args, mstring_t*);
                            __FMT_CHECK(__append_mstring(builder, string))
                        } else {
                            const char* string = va_arg(args, const char*);
                            __FMT_CHECK(__append_u8(builder, string))
                        }
                        consumed = 1;
                    } break;
                    case 'i': {
                        int value = va_arg(args, int);
                        __FMT_CHECK(__append_int(builder, value));
                        consumed = 1;
                    } break;

                    default:
                        break;
                }
                if (consumed) {
                    continue;
                }
            } break;

            // breaking will add 'val' to string
            default:
                break;
        }
        __FMT_CHECK(mstring_builder_append(builder, val))
    }
    va_end(args);

    return mstring_builder_finish(builder);
}
