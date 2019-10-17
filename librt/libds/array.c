/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Managed Array Implementation
 *  - Implements a managed array that can auto expand
 */
 
#include <ds/array.h>
#include <string.h>

// TODO: this is not working i think
static OsStatus_t
GrowCapacity(Array_t* Array)
{
    void** CurrentArray = atomic_load(&Array->Elements);
    void*  ElementData  = dsalloc(sizeof(void*) * (Array->Capacity * 2));
    if (!ElementData) {
        return OsOutOfMemory;
    }
    
    memset(ElementData, 0, sizeof(void*) * (Array->Capacity * 2));
    memcpy(ElementData, CurrentArray, sizeof(void*) * Array->Capacity);
    if (!atomic_compare_exchange_strong(&Array->Elements, &CurrentArray, (void**)ElementData)) {
        return OsBusy;
    }
    Array->Capacity *= 2;
    return OsSuccess;
}

OsStatus_t ArrayCreate(Flags_t Flags, size_t Capacity, Array_t** ArrayOut)
{
    void*    ArrayElements;
    Array_t* Array = dsalloc(sizeof(Array_t));
    if (!Array) {
        return OsOutOfMemory;
    }
    
    ArrayElements = dsalloc(Capacity * sizeof(void*));
    if (!ArrayElements) {
        dsfree(Array);
        return OsOutOfMemory;
    }
    
    memset(Array, 0, sizeof(Array_t));
    memset(ArrayElements, 0, Capacity * sizeof(void*));
    atomic_store(&Array->Elements, (void**)ArrayElements);
    Array->Flags    = Flags;
    Array->Capacity = Capacity;
    *ArrayOut       = Array;
    return OsSuccess;
}

void ArrayDestroy(Array_t* Array)
{
    if (!Array) {
        return;
    }
    
    dsfree(atomic_load(&Array->Elements));
    dsfree(Array);
}

UUId_t ArrayAppend(Array_t* Array, void* Element)
{
    void** Elements;
    size_t i;
    
    if (!Array) {
        return UUID_INVALID;
    }
    
    dslock(&Array->SyncObject);
    Elements = atomic_load(&Array->Elements);
TryAgain:
    for (i = Array->LookAt; i < Array->Capacity; i++) {
        if (!Elements[i]) {
            Array->LookAt = i + 1;
            Elements[i]   = Element;
            dsunlock(&Array->SyncObject);
            return (UUId_t)i;
        }
    }
    
    if (Array->Flags & ARRAY_CAN_EXPAND) {
        OsStatus_t Status = GrowCapacity(Array);
        if (Status == OsSuccess || Status == OsBusy) {
            goto TryAgain;
        }
    }
    dsunlock(&Array->SyncObject);
    return UUID_INVALID;
}

void ArrayRemove(Array_t* Array, UUId_t Index)
{
    void** Elements;
    
    if (!Array || Index >= Array->Capacity) {
        return;
    }
    
    dslock(&Array->SyncObject);
    Elements = atomic_load(&Array->Elements);
    
    Elements[Index] = NULL;
    if (Index < Array->LookAt) {
        Array->LookAt = Index;
    }
    dsunlock(&Array->SyncObject);
}
