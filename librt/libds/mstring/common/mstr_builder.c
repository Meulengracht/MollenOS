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

#include "private.h"
#include <string.h>

struct mstring_builder* mstring_builder_new(size_t initialCapacity) {
    struct mstring_builder* builder = stralloc(sizeof(struct mstring_builder));
    if (builder == NULL) {
        return NULL;
    }

    builder->size = 0;
    builder->capacity = initialCapacity;
    builder->storage = stralloc(initialCapacity * sizeof(mchar_t));
    if (builder->storage == NULL) {
        strfree(builder);
        return NULL;
    }
    return builder;
}

int mstring_builder_append(struct mstring_builder* builder, mchar_t val) {
    if (builder->size == builder->capacity) {
        // expand by 2x
        mchar_t* storage = stralloc(builder->capacity * 2 * sizeof(mchar_t));
        if (storage == NULL) {
            return -1;
        }

        memcpy(storage, builder->storage, builder->capacity * sizeof(mchar_t));
        strfree(builder->storage);
        builder->capacity *= 2;
        builder->storage = storage;
    }

    builder->storage[builder->size++] = val;
    return 0;
}

int mstring_builder_append_mstring(struct mstring_builder* builder, mstring_t* string)
{
    for (size_t i = 0; i < string->__length; i++) {
        if (mstring_builder_append(builder, string->__data[i])) {
            return -1;
        }
    }
    return 0;
}

int mstring_builder_append_u8(struct mstring_builder* builder, const char* u8, size_t count) {
    int u8i = 0;
    for (size_t i = 0; i < count; i++) {
        mchar_t val = mstr_next(u8, &u8i);
        if (mstring_builder_append(builder, val)) {
            return -1;
        }
    }
    return 0;
}

void mstring_builder_destroy(struct mstring_builder* builder) {
    strfree(builder->storage);
    strfree(builder);
}

mstring_t* mstring_builder_finish(struct mstring_builder* builder) {
    mstring_t* string = stralloc(sizeof(mstring_t));
    if (string == NULL) {
        mstring_builder_destroy(builder);
        return NULL;
    }

    // transfer the storage space, should we instead
    // refit the space to the actual length? Dunno, if we
    // space-optimized probably
    string->__flags = 0;
    string->__length = builder->size;
    string->__data   = builder->storage;
    strfree(builder);
    return string;
}
