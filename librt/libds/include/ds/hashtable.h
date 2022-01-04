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
 *
 *
 * - Open addressed hashtable implementation using round robin for balancing.
 */

#ifndef __LIBDS_HASHTABLE_H__
#define __LIBDS_HASHTABLE_H__

#include <ds/dsdefs.h>

#define HASHTABLE_LOADFACTOR_GROW   75 // Equals 75 percent load
#define HASHTABLE_LOADFACTOR_SHRINK 20 // Equals 20 percent load
#define HASHTABLE_MINIMUM_CAPACITY  16

typedef uint64_t (*hashtable_hashfn)(const void* element);
typedef int      (*hashtable_cmpfn)(const void* lh, const void* rh);
typedef void     (*hashtable_enumfn)(int index, const void* element, void* userContext);

typedef struct hashtable {
    size_t capacity;
    size_t element_count;
    size_t grow_count;
    size_t shrink_count;
    size_t element_size;
    void*  swap;
    void*  elements;

    hashtable_hashfn hash;
    hashtable_cmpfn  cmp;
} hashtable_t;

/**
 * Constructs a new hashtable that can be used to store and retrieve elements. The hashtable is constructed
 * in such a way that variable sized elements are supported, and the allows for inline keys in the element.
 * @param hashtable       The hashtable pointer that will be initialized.
 * @param requestCapacity The initial capacity of the hashtable, will automatically be set to HASHTABLE_MINIMUM_CAPACITY if less.
 * @param elementSize     The size of the elements that will be stored in the hashtable.
 * @param hashFunction    The hash function that will be used to hash the element data.
 * @param cmpFunction     The function that will be invoked when comparing the keys of two elements.
 * @return                Status of the hashtable construction.
 */
DSDECL(int, hashtable_construct(
    _In_ hashtable_t*     hashtable,
    _In_ size_t           requestCapacity,
    _In_ size_t           elementSize,
    _In_ hashtable_hashfn hashFunction,
    _In_ hashtable_cmpfn  cmpFunction));

/**
 * Destroys the hashtable and frees up any resources previously allocated. The structure itself is not freed.
 * @param hashtable The hashtable to cleanup.
 */
DSDECL(void, hashtable_destroy(
    _In_ hashtable_t* hashtable));

/**
 * Inserts or replaces the element with the calculated hash. 
 * @param hashtable The hashtable the element should be inserted into.
 * @param element   The element that should be inserted into the hashtable.
 * @return          The replaced element is returned, or NULL if element was inserted.
 */
DSDECL(void*, hashtable_set(
    _In_ hashtable_t* hashtable,
    _In_ const void*  element));

/**
 * Retrieves the element with the corresponding key.
 * @param hashtable The hashtable to use for the lookup.
 * @param key       The key to retrieve an element for.
 * @return          A pointer to the object.
 */
DSDECL(void*, hashtable_get(
    _In_ hashtable_t* hashtable,
    _In_ const void*  key));

/**
 * Removes the element from the hashtable with the given key.
 * @param hashtable The hashtable to remove the element from.
 * @param key       Key of the element to lookup.
 */
DSDECL(void*, hashtable_remove(
    _In_ hashtable_t* hashtable,
    _In_ const void*  key));

/**
 * Enumerates all elements in the hashtable.
 * @param hashtable    The hashtable to enumerate elements in.
 * @param enumFunction Callback function to invoke on each element.
 * @param context      A user-provided callback context.
 */
DSDECL(void, hashtable_enumerate(
    _In_ hashtable_t*     hashtable,
    _In_ hashtable_enumfn enumFunction,
    _In_ void*            context));

#endif //!__LIBDS_HASHTABLE_H__
