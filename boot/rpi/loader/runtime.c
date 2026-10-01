/**
 * Copyright, Philip Meulengracht
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

#include <stddef.h>

// The wrapper has no kernel libc or allocator. These integer-only routines
// satisfy the shared parser and compiler-generated structure copies without
// introducing SIMD state, runtime initialization, or PE import dependencies.
void*
memcpy(void* destination, const void* source, size_t length)
{
    unsigned char* out = destination;
    const unsigned char* in = source;
    for (size_t i = 0; i < length; i++) {
        out[i] = in[i];
    }
    return destination;
}

void*
memset(void* destination, int value, size_t length)
{
    unsigned char* out = destination;
    for (size_t i = 0; i < length; i++) {
        out[i] = (unsigned char)value;
    }
    return destination;
}

void*
memchr(const void* source, int value, size_t length)
{
    const unsigned char* in = source;
    for (size_t i = 0; i < length; i++) {
        if (in[i] == (unsigned char)value) {
            return (void*)(in + i);
        }
    }
    return NULL;
}

int
strcmp(const char* first, const char* second)
{
    while (*first && *first == *second) {
        first++;
        second++;
    }
    return (unsigned char)*first - (unsigned char)*second;
}

int
strncmp(const char* first, const char* second, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        int difference = (unsigned char)first[i] - (unsigned char)second[i];
        if (difference || !first[i]) {
            return difference;
        }
    }
    return 0;
}

int
memcmp(
    const void* first,
    const void* second,
    size_t      length)
{
    const unsigned char* left = first;
    const unsigned char* right = second;

    for (size_t i = 0; i < length; i++) {
        if (left[i] != right[i]) {
            return left[i] - right[i];
        }
    }
    return 0;
}
