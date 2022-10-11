/* MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 */

//#define __DS_TESTPROGRAM

#include <assert.h>
#include <ds/hashtable.h>
#include <errno.h>
#include <string.h>

#ifdef __DS_TESTPROGRAM
#include <stdlib.h>
#include <stdio.h>
#define dsalloc malloc
#define dsfree  free
#define dstrace printf
#else
#include <ds/ds.h>
#endif

struct hashtable_element {
    uint16_t probeCount;
    uint64_t hash;
    uint8_t  payload[];
};

#define SHOULD_GROW(hashtable)        (hashtable->element_count == hashtable->grow_count)
#define SHOULD_SHRINK(hashtable)      (hashtable->element_count == hashtable->shrink_count)

#define GET_ELEMENT_ARRAY(hashtable, elements, index) ((struct hashtable_element*)&((uint8_t*)elements)[index * hashtable->element_size])
#define GET_ELEMENT(hashtable, index)                 GET_ELEMENT_ARRAY(hashtable, hashtable->elements, index)

static int  hashtable_resize(hashtable_t* hashtable, size_t newCapacity);
static void hashtable_remove_and_bump(hashtable_t* hashtable, struct hashtable_element* element, size_t index);

int hashtable_construct(
    _In_ hashtable_t*     hashtable,
    _In_ size_t           requestCapacity,
    _In_ size_t           elementSize,
    _In_ hashtable_hashfn hashFunction,
    _In_ hashtable_cmpfn  cmpFunction)
{
    size_t initialCapacity  = HASHTABLE_MINIMUM_CAPACITY;
    size_t totalElementSize = elementSize + sizeof(struct hashtable_element);
    void*  elementStorage;
    void*  swapElement;

    if (!hashtable || !hashFunction || !cmpFunction) {
        errno = EINVAL;
        return -1;
    }

    // select initial capacity
    if (requestCapacity > initialCapacity) {
        // Make sure we have a power of two
        while (initialCapacity < requestCapacity) {
            initialCapacity <<= 1;
        }
    }

    elementStorage = dsalloc(initialCapacity * totalElementSize);
    if (!elementStorage) {
        return -1;
    }
    memset(elementStorage, 0, initialCapacity * totalElementSize);

    swapElement = dsalloc(totalElementSize);
    if (!swapElement) {
        dsfree(elementStorage);
        return -1;
    }

    hashtable->capacity      = initialCapacity;
    hashtable->element_count = 0;
    hashtable->grow_count    = (initialCapacity * HASHTABLE_LOADFACTOR_GROW) / 100;
    hashtable->shrink_count  = (initialCapacity * HASHTABLE_LOADFACTOR_SHRINK) / 100;
    hashtable->element_size  = totalElementSize;
    hashtable->elements      = elementStorage;
    hashtable->swap          = swapElement;
    hashtable->hash          = hashFunction;
    hashtable->cmp           = cmpFunction;
    return 0;
}

void hashtable_destroy(
    _In_ hashtable_t* hashtable)
{
    if (!hashtable) {
        return;
    }

    if (hashtable->swap) {
        dsfree(hashtable->swap);
    }

    if (hashtable->elements) {
        dsfree(hashtable->elements);
    }
}

void* hashtable_set(
    _In_ hashtable_t* hashtable,
    _In_ const void*  element)
{
    if (!hashtable) {
        errno = EINVAL;
        return NULL;
    }

    uint8_t*                  elementBuffer[hashtable->element_size];
    struct hashtable_element* iterElement = (struct hashtable_element*)&elementBuffer[0];
    size_t                    index;

    // Only resize on entry - that way we avoid any unneccessary resizing
    if (SHOULD_GROW(hashtable) && hashtable_resize(hashtable, hashtable->capacity << 1)) {
        return NULL;
    }

    // build an intermediate object containing our new element
    iterElement->probeCount = 1;
    iterElement->hash       = hashtable->hash(element);
    memcpy(&iterElement->payload[0], element, hashtable->element_size - sizeof(struct hashtable_element));

    index = iterElement->hash & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);

        // few cases to consider when doing this, either the slot is not taken or it is
        if (!current->probeCount) {
            memcpy(current, iterElement, hashtable->element_size);
            hashtable->element_count++;
            return NULL;
        } else {
            // If the slot is taken, we either replace it or we move fit in between
            // Just because something shares hash there is no guarantee that it's an element we want
            // to replace - instead let the user decide. Another strategy here is to use double hashing
            // and try to trust that
            if (current->hash == iterElement->hash &&
                !hashtable->cmp(&current->payload[0], &iterElement->payload[0])) {
                memcpy(hashtable->swap, current, hashtable->element_size);
                memcpy(current, iterElement, hashtable->element_size);
                return &((struct hashtable_element*)hashtable->swap)->payload[0];
            }

            // ok so we instead insert it here if our probe count is lower, we should not stop
            // the iteration though, the element we swap out must be inserted again at the next
            // probe location, and we must continue this charade untill no more elements are displaced
            if (current->probeCount < iterElement->probeCount) {
                memcpy(hashtable->swap, current, hashtable->element_size);
                memcpy(current, iterElement, hashtable->element_size);
                memcpy(iterElement, hashtable->swap, hashtable->element_size);
            }
        }

        iterElement->probeCount++;
        index = (index + 1) & (hashtable->capacity - 1);
    }
}

void* hashtable_get(
    _In_ hashtable_t* hashtable,
    _In_ const void*  key)
{
    uint64_t hash;
    size_t   index;

    if (!hashtable) {
        errno = EINVAL;
        return NULL;
    }

    hash  = hashtable->hash(key);
    index = hash & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);
        
        // termination condition
        if (!current->probeCount) {
            errno = ENOENT;
            return NULL;
        }

        // both hash and compare must match
        if (current->hash == hash && !hashtable->cmp(&current->payload[0], key)) {
            return &current->payload[0];
        }

        index = (index + 1) & (hashtable->capacity - 1);
    }
}

void* hashtable_remove(
    _In_ hashtable_t* hashtable,
    _In_ const void*  key)
{
    uint64_t hash;
    size_t   index;

    if (!hashtable) {
        errno = EINVAL;
        return NULL;
    }

    // Only resize on entry to avoid any unncessary resizes
    if (SHOULD_SHRINK(hashtable) && hashtable_resize(hashtable, hashtable->capacity >> 1)) {
        return NULL;
    }

    hash  = hashtable->hash(key);
    index = hash & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);

        // Does the key event exist, otherwise exit
        if (!current->probeCount) {
            errno = ENOENT;
            return NULL;
        }

        if (current->hash == hash && !hashtable->cmp(&current->payload[0], key)) {
            hashtable_remove_and_bump(hashtable, current, index);
            return &((struct hashtable_element*)hashtable->swap)->payload[0];
        }

        index = (index + 1) & (hashtable->capacity - 1);
    }
}

void hashtable_enumerate(
    _In_ hashtable_t*     hashtable,
    _In_ hashtable_enumfn enumFunction,
    _In_ void*            context)
{
    if (!hashtable || !enumFunction) {
        errno = EINVAL;
        return;
    }

    for (int i = 0; i < (int)hashtable->capacity; i++) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, i);
        if (current->probeCount) {
            enumFunction(i, &current->payload[0], context);
        }
    }
}

static void hashtable_remove_and_bump(
    _In_ hashtable_t*              hashtable,
    _In_ struct hashtable_element* element,
    _In_ size_t                    index)
{
    struct hashtable_element* previous = element;

    // Remove is a bit more extensive, we have to bump up all elements that
    // share the hash
    memcpy(hashtable->swap, element, hashtable->element_size);

    index = (index + 1) & (hashtable->capacity - 1);
    while (1) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, index);
        if (current->probeCount <= 1) {
            // this element is the first in a new chain or a free element.
            // we still need to reset the last entry to 0 in proble count
            previous->probeCount = 0;
            break;
        }

        // reduce the probe count and move it one up
        current->probeCount--;
        memcpy(previous, current, hashtable->element_size);

        // store next space and move to next index
        previous = current;
        index    = (index + 1) & (hashtable->capacity - 1);
    }
    hashtable->element_count--;
}

static void __hashtable_clone(hashtable_t* dst, hashtable_t* src, void* elements, size_t capacity)
{
    dst->capacity      = capacity;
    dst->element_count = 0;
    dst->grow_count    = (capacity * HASHTABLE_LOADFACTOR_GROW) / 100;
    dst->shrink_count  = (capacity * HASHTABLE_LOADFACTOR_SHRINK) / 100;
    dst->element_size  = src->element_size;
    dst->elements      = elements;
    dst->swap          = src->swap;
    dst->hash          = src->hash;
    dst->cmp           = src->cmp;
}

static int hashtable_resize(
    _In_ hashtable_t* hashtable,
    _In_ size_t       newCapacity)
{
    hashtable_t temporaryTable;
    void*       resizedStorage;

    // potentially there can be a too big resize - but practically very unlikely...
    if (newCapacity < HASHTABLE_MINIMUM_CAPACITY) {
        return 0; // ignore resize
    }

    resizedStorage = dsalloc(newCapacity * hashtable->element_size);
    if (!resizedStorage) {
        return -1;
    }
    memset(resizedStorage, 0, newCapacity * hashtable->element_size);

    // initialize the temporary hashtable we'll use to rebuild storage with
    // the new storage and capacity
    __hashtable_clone(&temporaryTable, hashtable, resizedStorage, newCapacity);

    // transfer objects and reset their probeCount
    for (size_t i = 0; i < hashtable->capacity; i++) {
        struct hashtable_element* current = GET_ELEMENT(hashtable, i);
        if (!current->probeCount) {
            continue;
        }
        hashtable_set(&temporaryTable, &current->payload[0]);
    }

    // free the original storage, we are done with that now
    dsfree(hashtable->elements);

    // transfer the relevant data from the temporary hashtable to
    // the original one, we are now done
    hashtable->elements      = temporaryTable.elements;
    hashtable->capacity      = temporaryTable.capacity;
    hashtable->grow_count    = temporaryTable.grow_count;
    hashtable->shrink_count  = temporaryTable.shrink_count;
    return 0;
}

#ifdef __DS_TESTPROGRAM
#include "hash_sip.c"

struct transaction {
    int    id;
    double amount;
};

static uint8_t hashKey[16] = { 196, 179, 43, 202, 48, 240, 236, 199, 229, 122, 94, 143, 20, 251, 63, 66 };

uint64_t test_hash(const void* transactionPointer)
{
    const struct transaction* xaction = transactionPointer;
    return siphash_64((const uint8_t*)&xaction->id, sizeof(int), &hashKey[0]);
}

int test_cmp(const void* transactionPointer1, const void* transactionPointer2)
{
    const struct transaction* xaction1 = transactionPointer1;
    const struct transaction* xaction2 = transactionPointer2;
    return xaction1->id == xaction2->id ? 0 : 1;
}

int main()
{
    struct transaction* pointer;
    hashtable_t         testTable;
    int                 result;
    int                 i;

    dstrace("constructing table\n");
    result = hashtable_construct(&testTable, 0, sizeof(struct transaction), test_hash, test_cmp);
    if (result) {
        dstrace("hashtable_test: construction failed with: %i\n", errno);
        return result;
    }

    dstrace("adding elements to table\n");
    for (i = 0; i < 500; i++) {
        struct transaction xaction = {
            .id = i,
            .amount = (double)(i * 14)
        };
        hashtable_set(&testTable, &xaction);
    }

    dstrace("retrieving test element from table\n");
    pointer = hashtable_get(&testTable, &(struct transaction){ .id = 250 });
    if (!pointer) {
        dstrace("hashtable_test: the insert with id 250 failed, could not retrieve element\n");
        return result;
    }
    
    dstrace("found element with id 250: %f\n", pointer->amount);

    
    dstrace("removing elements from table\n");
    for (i = 0; i < 500; i += 4) {
        hashtable_remove(&testTable, &(struct transaction){ .id = i });
    }

    dstrace("retrieving test element from table\n");
    pointer = hashtable_get(&testTable, &(struct transaction){ .id = 133 });
    if (!pointer) {
        dstrace("hashtable_test: the insert with id 133 failed, could not retrieve element\n");
        return result;
    }
    
    dstrace("found element with id 133: %f\n", pointer->amount);
    return 0;
}

#endif
