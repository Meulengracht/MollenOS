/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * Startup routines
 */

#include <stddef.h>
#include <crtdefs.h>
#include <ctype.h>

static void
__unescape_quotes(
        _InOut_ char* string)
{
    char* lastCharacter = NULL;

    while (*string) {
        if (*string == '"' && (lastCharacter != NULL && *lastCharacter == '\\')) {
            char* currentCharacter     = string;
            char* currentLastCharacter = lastCharacter;

            while (*currentCharacter) {
                *currentLastCharacter = *currentCharacter;
                currentLastCharacter = currentCharacter;
                currentCharacter++;
            }
            *currentLastCharacter = '\0';
        }

        lastCharacter = string;
        string++;
    }
}

int
__crt_parse_cmdline(
        _In_ const char* rawCommandLine,
        _In_ char**      argv)
{
    char* bufferPointer;
    char* lastPointer = NULL;
    int   argc, lastArgc;

    argc = lastArgc = 0;
    for (bufferPointer = (char*)rawCommandLine; *bufferPointer;) {
        /* Skip leading whitespace */
        while (isspace((int)(*bufferPointer))) {
            ++bufferPointer;
        }
        /* Skip over argument */
        if (*bufferPointer == '"') {
            ++bufferPointer;
            if (*bufferPointer) {
                if (argv) {
                    argv[argc] = bufferPointer;
                }
                ++argc;
            }
            /* Skip over word */
            lastPointer = bufferPointer;
            while (*bufferPointer && (*bufferPointer != '"' || *lastPointer == '\\')) {
                lastPointer = bufferPointer;
                ++bufferPointer;
            }
        }
        else {
            if (*bufferPointer) {
                if (argv) {
                    argv[argc] = bufferPointer;
                }
                ++argc;
            }

            /* Skip over word */
            while (*bufferPointer && !isspace((int)(*bufferPointer))) {
                ++bufferPointer;
            }
        }
        if (*bufferPointer) {
            if (argv) {
                *bufferPointer = '\0';
            }
            ++bufferPointer;
        }

        /* Strip out \ from \" sequences */
        if (argv && lastArgc != argc) {
            __unescape_quotes(argv[lastArgc]);
        }
        lastArgc = argc;
    }
    if (argv) {
        argv[argc] = NULL;
    }
    return argc;
}
