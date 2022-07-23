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

#ifndef __MSTRING_PRIVATE_H__
#define __MSTRING_PRIVATE_H__

#include <ds/mstring.h>
#include <stddef.h>

#if defined(MSTRING_KERNEL)
#include <heap.h>
static inline void* stralloc(size_t size) { return kmalloc(size); }
static inline void  strfree(void* mem) { kfree(mem); }
#else
#include <stdlib.h>
static inline void* stralloc(size_t size) { return malloc(size); }
static inline void  strfree(void* mem) { free(mem); }
#endif

extern size_t  mstr_len_u8(const char* u8);
extern void    mstr_to_internal(const char* u8, mchar_t* out);
extern mchar_t mstr_next(const char* u8, int* indexp);
extern int     mstr_cmp_u8_index(mstring_t* string, const char* u8, size_t startIndex, size_t length);

struct mstring_builder {
    size_t   size;
    size_t   capacity;
    mchar_t* storage;
};

extern struct mstring_builder* mstring_builder_new(size_t initialCapacity);
extern void mstring_builder_destroy(struct mstring_builder* builder);
extern mstring_t* mstring_builder_finish(struct mstring_builder* builder);

extern int mstring_builder_append(struct mstring_builder* builder, mchar_t val);
extern int mstring_builder_append_u8(struct mstring_builder* builder, const char* u8, size_t count);
extern int mstring_builder_append_mstring(struct mstring_builder* builder, mstring_t* string);


#endif //!__MSTRING_PRIVATE_H__
