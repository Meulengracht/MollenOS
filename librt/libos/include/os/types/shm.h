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
#include <os/types/time.h>

/**
 * @brief CPU access requested by userspace. Zero is not a valid request.
 */
enum SHMCpuAccess {
    // Reads may overlap other CPU or device reads of the same cache lines.
    SHMCpuAccessRead = 1,
    // Read/write access. No other CPU or device access may overlap.
    SHMCpuAccessWrite = 2
};

/**
 * @brief Access kind used by both begin and end. All access kinds must be defined
 * here with unique values, also kinds that are implemented by device-specific
 * records. Existing values are part of the syscall ABI and must not be renumbered.
 */
enum SHMAccessKind {
    // CPU access to the whole session range. Write selects read/write access.
    SHMAccessCpu = 0,
    // Begin submits a DMA range, and end reports how much of that range completed.
    DMAAccessTransfer = 1,
    // Begin and end count the uses of a persistent DMA mapping.
    DMAAccessUse = 2
};

/**
 * @brief How begin calls on a shared range handle conflicts.
 * Waiting requests are served after the earlier requests they conflict with.
 * Requests on separate cache lines can run at the same time. A failed request
 * reserves nothing, and does not end any access that was active before the call.
 *
 * SHMWaitUntilAvailable uses an absolute deadline while blocked. NULL or
 * OS_DEADLINE_INFINITE means wait forever. A zero timestamp has already expired,
 * and does not mean nonblocking. SHMWaitNonBlocking ignores the deadline.
 *
 * Errors:
 * - OS_EBUSY on a conflict with SHMWaitNonBlocking.
 * - OS_ETIMEOUT when a blocked wait reaches its deadline.
 * - OS_EINTERRUPTED when a wait is interrupted while the conflict is still there.
 * - OS_EDEVFAULT when the range overlaps a quarantined range.
 */
enum SHMWaitMode {
    // Return OS_EBUSY on conflict without waiting. The deadline is ignored.
    SHMWaitNonBlocking,
    // Wait for conflicts to finish, limited by an optional absolute deadline.
    SHMWaitUntilAvailable
};

/**
 * @brief Parameters for a begin request. What is required depends on Kind:
 * - SHMAccessCpu: Offset and Length must be zero. Write selects read/write access.
 * - DMAAccessTransfer: Offset and Length select a range within the mapping.
 * - DMAAccessUse: Offset and Length must be zero.
 * For both DMA kinds, Write must be false, as the direction of the mapping decides
 * the device access. Deadline is only used by SHMWaitUntilAvailable.
 */
struct SHMAccessBeginParameters {
    enum SHMAccessKind Kind;
    bool               Write;
    size_t             Offset;
    size_t             Length;
    enum SHMWaitMode   Mode;
    OSTimestamp_t      Deadline;
};

/**
 * @brief Parameters for an end request. What is required depends on Kind:
 * - SHMAccessCpu and DMAAccessUse: Length must be zero.
 * - DMAAccessTransfer: Length is the number of completed bytes, counted from the
 *   start of the submitted range. A Length of zero completes a cancellation without
 *   copying received data.
 */
struct SHMAccessEndParameters {
    enum SHMAccessKind Kind;
    size_t             Length;
};

/**
 * @brief Flags used when creating an SHM region.
 * SHM_CLEAN       zeroes memory allocated for the buffer.
 * SHM_COMMIT      allocates pages when the region is created, instead of waiting
 *                 for page faults.
 * SHM_TRAP        marks a trap region. Used only by OS services.
 * SHM_IPC         marks an IPC region, which needs a kernel-accessible copy.
 * SHM_PRIVATE     makes the region private to the process memory space.
 */
#define SHM_COMMIT       0x00000001U
#define SHM_CLEAN        0x00000002U
#define SHM_PRIVATE      0x00000004U
#define SHM_BIGPAGES_2MB 0x00000010U
#define SHM_BIGPAGES_1GB 0x00000020U
#define SHM_TRAP         0x00000100U
#define SHM_IPC          0x00000200U
// Kind 0x00000300U is reserved for the retired SHM_DEVICE mode. Use DMAAllocate
// for controller storage, or SHM_COMMIT and DMAMapSHM for transfer buffers.
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
    // Capacity is the maximum size of the buffer region. This is set
    // on the first call to SHMAttach.
    size_t Capacity;
    // Buffer is the mapped buffer address. This is set by SHMCreate or SHMMap.
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
