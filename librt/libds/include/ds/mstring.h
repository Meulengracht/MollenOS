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
#define mstr_const(c_str) { __MSTRING_FLAG_CONST, (sizeof(c_str) / sizeof(mchar_t)) - 1, (mchar_t*)(c_str) };
#define mstr_len(str)     ((str) != NULL ? (str)->__length : 0)
#define mstr_bsize(str)   ((str) != NULL ? (str)->__length*sizeof(mchar_t) : 0)

DSDECL(mstring_t*,  mstr_new_u8(const char* str));
DSDECL(mstring_t*,  mstr_new_u16(const short* str));
DSDECL(mstring_t*,  mstr_clone(mstring_t*));
DSDECL(void,        mstr_delete(mstring_t*));
DSDECL(void,        mstrv_delete(mstring_t**));
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
DSDECL(int,        mstr_split(mstring_t*, mchar_t, mstring_t***));
DSDECL(mstring_t*, mstr_join(mstring_t**, int, const char* sep));
DSDECL(mchar_t,    mstr_at(mstring_t*, int));

DSDECL(mstring_t*, mstr_path_new_u8(const char*));
DSDECL(int,        mstr_path_tokens(mstring_t*, mstring_t***));
DSDECL(mstring_t*, mstr_path_token_at(mstring_t*, int));

/**
 * @brief Joins a series of path tokens to a final path.
 * @param tokens The array of tokens
 * @param count  The number of array tokens
 * @return
 * For instance, an array provided of
 * [0] - foo
 * [1] - bar
 * Will result in the path of foo/bar. Leading and trailing '/' are not
 * added and must explicit be provided as individual tokens to add them.
 */
DSDECL(mstring_t*, mstr_path_tokens_join(mstring_t**, int));

/**
 * @brief Acts like the traditional join, it will build a path from the elements
 * provided, and automatically insert seperators between the elements. The list of
 * elements *MUST* be NULL-terimanted, otherwise this will run in a loop untill it
 * crashes.
 *
 * @param[In] base The base path that following elements should be joined with
 * @param[In] ...  Further join arguments *MUST* be of the type 'mstring_t*'
 * @return
 */
DSDECL(mstring_t*, mstr_path_join(mstring_t* base, ...));

/**
 * @brief Acts like the traditional join, it will build a path from the elements
 * provided, and automatically insert seperators between the elements. The list of
 * elements *MUST* be NULL-terimanted, otherwise this will run in a loop untill it
 * crashes.
 *
 * @param[In] base The base path that following elements should be joined with
 * @param[In] ...  Further join arguments *MUST* be of the type 'char*'
 * @return
 */
DSDECL(mstring_t*, mstr_path_join_u8(mstring_t* base, ...));

/**
 * @brief Returns all the components of a path up to and not including
 * the final '/'. Trailing '/' are ignored.
 * path       dirname
 * /usr/lib   /usr
 * /usr/      /
 * usr        .
 * /          /
 * .          .
 * ..         .
 *
 * @returnThe new string containing the dirname of the path
 */
DSDECL(mstring_t*, mstr_path_dirname(mstring_t*));

/**
 * @brief Returns the filename componenet of a path. In real terms
 * this means that it returns the component after the final '/' which
 * is non-zero in length. All trailing '/' will be ignored.
 * path       basename
 * /usr/lib   lib
 * /usr/      usr
 * usr        usr
 * /          /
 * .          .
 * ..         ..
 * @return The new string containing the basename of the path
 */
DSDECL(mstring_t*, mstr_path_basename(mstring_t*));

/**
 * @brief If present, changes the extension in the path to the one provided. For instance;
 * /usr/lib/ld.so, .dll becomes /usr/lib/ld.dll.
 * /usr/lib, .dll becomes /usr/lib
 * @return A new string with the changed file extension.
 */
DSDECL(mstring_t*, mstr_path_change_extension_u8(mstring_t*, const char*));

_CODE_END

#endif //!__MSTRING_INTERFACE_H__
