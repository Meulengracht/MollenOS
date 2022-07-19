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

#ifndef __MSTRING_INTERFACE_H__
#define __MSTRING_INTERFACE_H__

#include <ds/dsdefs.h>

typedef uint32_t mchar_t;
#define MSTRING_EOS 0xFFFD // Marker for identifying end of string
#define MSTRING_U8BYTES 10

typedef struct mstring {
    unsigned int __flags;
    size_t       __length;
    mchar_t*     __data;
} mstring_t;

#define __MSTRING_FLAG_CONST 0x1

_CODE_BEGIN

// Length of const strings include the zero terminator
// mstr_const *MUST* be used with the 'U' keyword
#define mstr_const(c_str) { __MSTRING_FLAG_CONST, sizeof(c_str), (mchar_t*)(c_str) };
#define mstr_len(str)     ((str) != NULL ? (str)->__length : 0)
#define mstr_bsize(str)   ((str) != NULL ? (str)->__length*sizeof(mchar_t) : 0)

DSDECL(mstring_t*,  mstr_new_u8(const char* str));
DSDECL(mstring_t*,  mstr_clone(mstring_t*));
DSDECL(void,        mstr_delete(mstring_t*));
DSDECL(uint32_t,    mstr_hash(mstring_t*));

/**
 * @brief Converts a mstring to an UTF8 formatted sequence string, which will
 * be zero-terminated. This is useful for interacting with the standard C library
 * and enables it to be used with printf. The returned string must be free'd.
 *
 * @return
 */
DSDECL(char*, mstr_u8(mstring_t*));

/**
 * @brief Converts an UTF8 sequence to an UTF32 code point. If the routine
 * fails to convert the sequence, the function returns 0.
 *
 * @param[In] u8 The UTF8 sequence that should be converted.
 * @return 0 if the functions fails to convert, otherwise an UTF32 code point.
 */
DSDECL(mchar_t, mstr_tochar(const char* u8));

/**
 * @breif Converts an mstring character (i.e from mstr_at) to a valid UTF-8 sequence
 * if possible. The buffer provided by the caller should be atleast MSTRING_U8BYTES long
 * to support any character conversion.
 *
 * @param[In]  character The character that should be converted to UTF-8.
 * @param[In]  u8     The buffer to store the UTF-8 sequence in.
 * @param[Out] length The number of bytes written to the buffer.
 * @return
 */
DSDECL(int, mstr_fromchar(mchar_t character, char* u8, size_t* length));

/**
 * @brief Creates a new string built up according to the format string. Works exactly
 * like the classic C sprintf, except that the modifier %ms is supported to support mstring_t
 *
 * @param[In] fmt The format string, must be in UTF8
 * @param[In] ... Arguments for the format string
 * @return
 */
DSDECL(mstring_t*, mstr_fmt(const char* fmt, ...));

DSDECL(mchar_t,    mstr_clower(mchar_t));
DSDECL(mchar_t,    mstr_cupper(mchar_t));
DSDECL(int,        mstr_cmp(mstring_t*, mstring_t*));
DSDECL(int,        mstr_cmp_u8(mstring_t* string, const char* u8));
DSDECL(int,        mstr_icmp(mstring_t*, mstring_t*));
DSDECL(mstring_t*, mstr_substr(mstring_t*, int start, int length));
DSDECL(int,        mstr_find_u8(mstring_t*, const char*, int startIndex));
DSDECL(int,        mstr_rfind_u8(mstring_t*, const char*, int startIndex));
DSDECL(mstring_t*, mstr_replace_u8(mstring_t*, const char* find, const char* with));
DSDECL(mchar_t,    mstr_at(mstring_t*, int));

_CODE_END

#endif //!__MSTRING_INTERFACE_H__
