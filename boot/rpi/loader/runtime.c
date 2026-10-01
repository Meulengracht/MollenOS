// Copyright, Philip Meulengracht. SPDX-License-Identifier: GPL-3.0-or-later

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
