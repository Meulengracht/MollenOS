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

#ifndef __MERR_INTERFACE_H__
#define __MERR_INTERFACE_H__

#include <ds/mstring.h>

/**
 * To print an merr message, you can use the mstring library functions.
 * Use the 'R'/'r' format specifier to print the error code and message.
 * Example: merr_fmt(ms, "Error: %R", err);
 * The 'R' specifier will automatically consume/free the merr_t structure.
 *         After printing, the merr_t structure will no longer be valid.
 * The 'r' specifier behaves similarly but does not automatically free the merr_t structure.
 */
typedef struct merr {
    int        code;
    mstring_t* message;
} merr_t;

/**
 * @brief Format an error message with a code and a printf-style format string.
 * An merr_t must always be passed by reference (pointer) for _fmt.
 * 
 * @param code The error code.
 * @param fmt The format string.
 * @return A merr_t structure containing the formatted error message.
 */
DSDECL(merr_t, merr_fmt(int code, const char* fmt, ...));
DSDECL(int,    merr_code(merr_t* err));
DSDECL(void,   merr_free(merr_t* err));
 
#endif //!__MERR_INTERFACE_H__
