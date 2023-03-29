/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_TYPES_SHM_H__
#define __OS_TYPES_SHM_H__

#include <os/osdefs.h>
#include <os/types/memory.h>

/**
 * Configuration flags for creation of a new SHM region
 * SHM_UNCACHEABLE is used to mark the underlying memory as uncachable.
 * SHM_CLEAN       will zero out any allocated memory for the dma buffer
 * SHM_COMMIT      will pre-allocate memory for the region, instead of lazily allocate
 *                 pages on page-faults.
 * SHM_TRAP        will mark the region a trap region. Only in use for OS services.
 * SHM_IPC         will mark region for IPC and needs a kernel accessible copy.
 * SHM_DEVICE      will mark the region device memory accessible. This allows for using
 *                 the different types in SHM_TYPE_* to request specifiy memory attributes for
 *                 the underlying physical memory pages. This flag automatically implies that
 *                 SHM_COMMIT will be set.
 * SHM_PRIVATE     region is intended for private use (private to the process memory space).
 */
#define SHM_COMMIT       0x00000001U
#define SHM_CLEAN        0x00000002U
#define SHM_PRIVATE      0x00000004U
#define SHM_BIGPAGES_2MB 0x00000010U
#define SHM_BIGPAGES_1GB 0x00000020U
#define SHM_CONFORM      0x00000040U
#define SHM_TRAP         0x00000100U
#define SHM_IPC          0x00000200U
#define SHM_DEVICE       0x00000300U
#define SHM_KIND_MASK    0x00000F00U
#define SHM_KIND(_flags) ((_flags) & SHM_KIND_MASK)

/**
 * SHM Access flags that are available when creating and mapping.
 */
#define SHM_ACCESS_READ    0x00000001U
#define SHM_ACCESS_WRITE   0x00000002U
#define SHM_ACCESS_EXECUTE 0x00000004U

// These are only available when calling SHMMap
#define SHM_ACCESS_COMMIT  0x00000008U

/**
 * @brief Flags available when conforming a SHM buffer.
 */
#define SHM_CONFORM_FILL_ON_CREATION  0x1 // Fill the conformed buffer when created.
#define SHM_CONFORM_BACKFILL_ON_UNMAP 0x2 // Fill the source buffer when detached.

typedef struct SHM {
    // Key is the global identifier for this buffer. When listing
    // active shared memory buffers on the system, this is the name
    // that is shown.
    const char* Key;
    // Flags is the buffer capabilities. This describes the functionality
    // and traits of the buffer.
    unsigned int Flags;
    // Access is the allowed access when mapping the buffer. When sharing
    // a buffer with another process, this describes the allowed mapping
    // access.
    unsigned int Access;
    // Conformity is the memory type that should be allocated. For most uses this
    // should always be NONE, and is only supported for mapping device-accessible
    // memory.
    enum OSMemoryConformity Conformity;
    // Size is the size of the memory region. This size will not be backed,
    // but initially only be reserved in memory, unless SHM_COMMIT is specified
    // when creating the buffer. Only when memory is accessed
    // will there be allocated space for it on the underlying storage.
    size_t Size;
} SHM_t;

typedef struct SHMHandle {
    // ID is the global memory region key that identifies the memory
    // buffer. This is set on the first call to SHMAttach.
    uuid_t ID;
    // SourceID is set if the buffer handle was created and then
    // cloned by the conform subsystem.
    uuid_t SourceID;
    // SourceFlags is a set of flags related to the behaviour of the
    // source buffer when conforming.
    unsigned int SourceFlags;
    // Capacity is the maximum size of the buffer region. This is set
    // on the first call to SHMAttach.
    size_t Capacity;
    // Buffer is the raw data pointer to a mapped region of memory
    // representing the buffer. This member is set when calling either of
    // SHMCreate/SHMExport/SHMMap.
    void* Buffer;
    // Offset is the offset into the underlying storage, where the mapping
    // of the buffer starts. This is updated with each call to SHMMap
    size_t Offset;
    // Length is the currently mapped size of <Buffer>. This member is only
    // updated with each call to SHMMap.
    size_t Length;
} SHMHandle_t;

typedef struct SHMSG {
    uintptr_t Address;
    size_t    Length;
} SHMSG_t;

typedef struct SHMSGTable {
    SHMSG_t* Entries;
    int      Count;
} SHMSGTable_t;

#endif //!__OS_TYPES_SHM_H__
